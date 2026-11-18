export const COMMANDS = Object.freeze({
  hello: 1,
  listSlots: 2,
  readBegin: 3,
  readNext: 4,
  writeBegin: 5,
  writeData: 6,
  writeCommit: 7,
  cancel: 8,
});

export const STATUS = Object.freeze({
  ok: 0,
  pending: 1,
});

const RESPONSE_BIT = 0x40;
const MAX_PAYLOAD = 119;
const CHUNK_BYTES = 96;

function crc16(bytes) {
  let crc = 0xffff;
  for (const byte of bytes) {
    crc ^= byte << 8;
    for (let bit = 0; bit < 8; bit += 1) {
      crc = crc & 0x8000 ? ((crc << 1) ^ 0x1021) & 0xffff : (crc << 1) & 0xffff;
    }
  }
  return crc;
}

export function encodeFrame(command, sequence, payload = []) {
  if (!Number.isInteger(command) || command < 0 || command > 127
      || !Number.isInteger(sequence) || sequence < 0 || sequence > 127
      || payload.length > MAX_PAYLOAD
      || payload.some((value) => !Number.isInteger(value) || value < 0 || value > 127)) {
    throw new Error("Invalid SysEx frame.");
  }
  const body = [0x7d, 0x43, 0x42, 1, command, sequence, ...payload];
  const checksum = crc16(body);
  body.push(checksum & 0x7f, (checksum >> 7) & 0x7f, (checksum >> 14) & 0x03);
  return Uint8Array.from([0xf0, ...body, 0xf7]);
}

export function decodeFrame(message) {
  let bytes = Array.from(message);
  if (bytes[0] === 0xf0) bytes = bytes.slice(1);
  if (bytes[bytes.length - 1] === 0xf7) bytes = bytes.slice(0, -1);
  if (bytes.length < 9 || bytes.length > MAX_PAYLOAD + 9
      || bytes.some((value) => value > 127)
      || bytes[0] !== 0x7d || bytes[1] !== 0x43 || bytes[2] !== 0x42
      || bytes[3] !== 1 || bytes[bytes.length - 1] > 3) {
    return null;
  }
  const payloadLength = bytes.length - 9;
  const bodyLength = bytes.length - 3;
  const expected = crc16(bytes.slice(0, bodyLength));
  const received = bytes[bodyLength]
    | (bytes[bodyLength + 1] << 7)
    | (bytes[bodyLength + 2] << 14);
  if (expected !== received) return null;
  return {
    command: bytes[4],
    sequence: bytes[5],
    payload: bytes.slice(6, 6 + payloadLength),
  };
}

function encode14(value) {
  return [value & 0x7f, (value >> 7) & 0x7f];
}

function decode14(low, high) {
  return low | (high << 7);
}

export function encode21(value) {
  if (!Number.isInteger(value) || value < 0 || value > 0x1fffff) {
    throw new Error("Invalid 21-bit value.");
  }
  return [value & 0x7f, (value >> 7) & 0x7f, (value >> 14) & 0x7f];
}

export function decode21(low, middle, high) {
  return low | (middle << 7) | (high << 14);
}

function toSysExText(value) {
  return JSON.stringify(value).replace(/[\u007f-\uffff]/g, (character) => (
    `\\u${character.charCodeAt(0).toString(16).padStart(4, "0")}`
  ));
}

export class PresetTransferClient {
  constructor(onMessage = () => {}, onPortsChanged = () => {}) {
    this.onMessage = onMessage;
    this.onPortsChanged = onPortsChanged;
    this.access = null;
    this.accessPromise = null;
    this.input = null;
    this.output = null;
    this.sequence = 0;
    this.pending = new Map();
  }

  async openAccess() {
    if (this.access) return this.access;
    if (this.accessPromise) return this.accessPromise;
    if (!navigator.requestMIDIAccess) {
      throw new Error("Web MIDI is unavailable. Use desktop Chrome or Edge.");
    }
    this.accessPromise = navigator.requestMIDIAccess({ sysex: true })
      .then((access) => {
        this.access = access;
        this.access.onstatechange = () => this.onPortsChanged();
        return access;
      })
      .finally(() => {
        this.accessPromise = null;
      });
    return this.accessPromise;
  }

  findChompiPorts() {
    if (!this.access) return null;
    const isChompi = (port) => {
      if (port.state === "disconnected") return false;
      const description = `${port.name || ""} ${port.manufacturer || ""}`.toLowerCase();
      return description.includes("chompi") && description.includes("bowl");
    };
    const input = [...this.access.inputs.values()].find(isChompi);
    if (!input) return null;
    const outputs = [...this.access.outputs.values()].filter(isChompi);
    const output = outputs.find((port) => port.name === input.name) || outputs[0];
    return output ? { input, output } : null;
  }

  selectPorts(inputId, outputId) {
    if (!this.access) throw new Error("Connect to MIDI first.");
    const input = this.access.inputs.get(inputId);
    const output = this.access.outputs.get(outputId);
    if (!input || !output) throw new Error("Select an available MIDI input and output.");
    if (this.input) this.input.onmidimessage = null;
    this.input = input;
    this.output = output;
    this.input.onmidimessage = (event) => this.receive(event.data);
  }

  clearPorts() {
    if (this.input) this.input.onmidimessage = null;
    this.input = null;
    this.output = null;
    for (const key of [...this.pending.keys()]) {
      this.finishPending(key, new Error("The MIDI device disconnected."));
    }
  }

  disconnect() {
    this.clearPorts();
    if (this.access) this.access.onstatechange = null;
    this.access = null;
    this.accessPromise = null;
  }

  receive(message) {
    const frame = decodeFrame(message);
    if (!frame) return;
    const key = `${frame.command}:${frame.sequence}`;
    const pending = this.pending.get(key);
    if (pending) {
      if (pending.waitForFinal && frame.payload[0] === STATUS.pending) {
        clearTimeout(pending.timer);
        pending.timer = setTimeout(
          () => this.finishPending(key, new Error("Preset save timed out.")),
          30000,
        );
        pending.onPending?.();
        return;
      }
      this.finishPending(key, null, frame);
      return;
    }
    this.onMessage("Received an unrelated MIDI message.");
  }

  finishPending(key, error, frame) {
    const pending = this.pending.get(key);
    if (!pending) return;
    clearTimeout(pending.timer);
    this.pending.delete(key);
    if (error) pending.reject(error);
    else pending.resolve(frame);
  }

  request(command, payload = [], { waitForFinal = false, onPending } = {}) {
    if (!this.input || !this.output) {
      return Promise.reject(new Error("Select a MIDI device first."));
    }
    const sequence = this.sequence;
    this.sequence = (this.sequence + 1) & 0x7f;
    const frame = encodeFrame(command, sequence, payload);
    const key = `${command | RESPONSE_BIT}:${sequence}`;

    return new Promise((resolve, reject) => {
      let attempts = 0;
      const pending = {
        resolve,
        reject,
        waitForFinal,
        onPending,
        timer: null,
      };
      const transmit = () => {
        try {
          this.output.send(frame);
        } catch (error) {
          this.finishPending(key, error);
          return;
        }
        pending.timer = setTimeout(() => {
          attempts += 1;
          if (attempts >= 3) {
            this.finishPending(key, new Error("The instrument did not respond."));
          } else {
            transmit();
          }
        }, 2500);
      };
      this.pending.set(key, pending);
      transmit();
    });
  }

  async checkResponse(frame) {
    const status = frame.payload[0];
    if (status !== STATUS.ok && status !== STATUS.pending) {
      const names = [
        "Success", "Pending", "Bad request", "Unsupported protocol",
        "No SD card", "Instrument busy", "Invalid slot", "Slot is empty",
        "Incompatible preset", "Invalid preset", "SD write failed",
        "Transfer sequence error", "Preset too large", "Transfer expired",
      ];
      throw new Error(names[status] || `Instrument error ${status}.`);
    }
    return frame.payload.slice(1);
  }

  async handshake() {
    const frame = await this.request(COMMANDS.hello);
    const [protocol, schema, slots, sdPresent] = await this.checkResponse(frame);
    if (protocol !== 1 || schema !== 12 || slots !== 15) {
      throw new Error("This firmware does not support preset editor protocol v1.");
    }
    if (!sdPresent) throw new Error("No SD card is available in the instrument.");
  }

  async listSlots() {
    const frame = await this.request(COMMANDS.listSlots);
    const values = await this.checkResponse(frame);
    if (values.length !== 10) {
      throw new Error("This firmware returned an unsupported slot-list response.");
    }
    return {
      valid: decode21(values[0], values[1], values[2]),
      incompatible: decode21(values[3], values[4], values[5]),
      dirty: decode21(values[6], values[7], values[8]),
      busy: values[9] === 1,
    };
  }

  async readPreset(slot) {
    const begin = await this.request(COMMANDS.readBegin, [slot]);
    const [lengthLow, lengthHigh] = await this.checkResponse(begin);
    const length = decode14(lengthLow, lengthHigh);
    if (length <= 0 || length >= 4096) throw new Error("Invalid preset size.");
    const bytes = [];
    try {
      for (let offset = 0; offset < length; offset += CHUNK_BYTES) {
        const next = await this.request(COMMANDS.readNext, encode14(offset));
        const payload = await this.checkResponse(next);
        const receivedOffset = decode14(payload[0], payload[1]);
        if (receivedOffset !== offset || payload.length < 3) {
          throw new Error("The instrument returned an invalid preset chunk.");
        }
        bytes.push(...payload.slice(2));
      }
      if (bytes.length !== length) throw new Error("Preset transfer was incomplete.");
      return JSON.parse(String.fromCharCode(...bytes));
    } finally {
      await this.request(COMMANDS.cancel).catch(() => {});
    }
  }

  async writePreset(slot, preset, onProgress = () => {}) {
    try {
      const text = toSysExText(preset);
      const bytes = Array.from(text, (character) => character.charCodeAt(0));
      if (bytes.length >= 4096 || bytes.some((byte) => byte > 127)) {
        throw new Error("Preset cannot be represented in the bounded SysEx transfer.");
      }
      const length = encode14(bytes.length);
      const begin = await this.request(COMMANDS.writeBegin, [slot, ...length]);
      await this.checkResponse(begin);
      for (let offset = 0; offset < bytes.length; offset += CHUNK_BYTES) {
        const chunk = bytes.slice(offset, offset + CHUNK_BYTES);
        const data = await this.request(
          COMMANDS.writeData,
          [...encode14(offset), ...chunk],
        );
        const [nextLow, nextHigh] = await this.checkResponse(data);
        if (decode14(nextLow, nextHigh) !== offset + chunk.length) {
          throw new Error("The instrument acknowledged an unexpected byte offset.");
        }
        onProgress(Math.min(1, (offset + chunk.length) / bytes.length));
      }
      const commit = await this.request(COMMANDS.writeCommit, [], {
        waitForFinal: true,
        onPending: () => onProgress(1),
      });
      await this.checkResponse(commit);
    } finally {
      await this.request(COMMANDS.cancel).catch(() => {});
    }
  }
}
