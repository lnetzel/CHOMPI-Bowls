import {
  applyTuning,
  createFactoryPreset,
  setPresetValue,
  validatePreset,
  setBankValueForAllModes,
} from "./preset-model.js";
import { TUNINGS } from "./tunings.js";
import { PresetTransferClient } from "./preset-transfer.js";

const byId = (id) => document.getElementById(id);
const elements = {
  connectMidi: byId("connect-midi"),
  disconnectMidi: byId("disconnect-midi"),
  deviceMessage: byId("device-message"),
  connectionState: byId("connection-state"),
  slotButtons: byId("slot-buttons"),
  slotStatus: byId("slot-status"),
  saveSlot: byId("save-slot"),
  progress: byId("transfer-progress"),
  newPreset: byId("new-preset"),
  importPreset: byId("import-preset"),
  exportPreset: byId("export-preset"),
  editor: byId("editor-controls"),
  metadataControls: byId("metadata-controls"),
  draftState: byId("draft-state"),
  tuningSelect: byId("tuning-select"),
  tuningDescription: byId("tuning-description"),
  tuningPreview: byId("tuning-preview"),
  applyTuning: byId("apply-tuning"),
  startupMode: byId("startup-mode"),
  presetName: byId("preset-name"),
  presetComment: byId("preset-comment"),
  globalBowlControls: byId("global-bowl-controls"),
  editMode: byId("edit-mode"),
  bowlKeybed: byId("bowl-keybed"),
  selectedKeyLabel: byId("selected-key-label"),
  keyControls: byId("key-controls"),
};

const state = {
  client: null,
  slots: { valid: 0, incompatible: 0, dirty: 0, busy: false },
  slotsLoaded: false,
  connected: false,
  connecting: false,
  accessEnabled: false,
  manualDisconnect: false,
  midiPermission: null,
  deviceKey: null,
  lastAttemptKey: null,
  lastAttemptTime: 0,
  lastSlotRefresh: 0,
  refreshingSlots: false,
  transferBusy: false,
  pollTimer: null,
  preset: null,
  selectedSlot: 0,
  mode: "strikes",
  key: 0,
  dirty: false,
};

const BOWL_PROFILES = [
  { value: 0, name: "Temple", description: "Measured 180 mm bowl ratios; balanced resonance with a long fundamental ring." },
  { value: 1, name: "Deep", description: "Tighter resonance ratios, gentler beating, and the longest ring." },
  { value: 2, name: "Bright", description: "Wider resonance ratios, quicker decay, and stronger upper partials." },
  { value: 3, name: "Wide", description: "Temple-like resonance with stronger detuning for pronounced beating." },
  { value: 4, name: "Thin", description: "Shorter decay and stronger upper partials for a thin, bright tone." },
];

const FIELD_CONFIG = [
  { field: "bvol", label: "Bowl volume", encoder: 1, page: 1, kind: "range", min: 0, max: 32, step: 1 },
  { field: "pan", label: "Pan", encoder: 1, page: 2, kind: "range", min: 0, max: 32, step: 1 },
  { field: "pitch_hz", label: "Pitch (Hz)", encoder: 1, page: 3, kind: "number", min: 40, max: 10000, step: 0.001 },
  { field: "bright", label: "Brightness", encoder: 2, page: 1, kind: "range", min: 0, max: 32, step: 1 },
  { field: "beat", label: "Beating", encoder: 2, page: 2, kind: "range", min: 0, max: 32, step: 1 },
  { field: "decay", label: "Decay", encoder: 3, page: 1, kind: "range", min: 0, max: 32, step: 1 },
  { field: "profile", label: "Bowl profile", encoder: 3, page: 2, kind: "select", options: BOWL_PROFILES },
];

const NOTE_NAMES = ["C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B"];
const BLACK_KEY_SEMITONES = new Set([1, 3, 6, 8, 10]);

function getKeyLabel(key) {
  const note = 48 + key;
  const noteName = NOTE_NAMES[note % 12];
  const octave = Math.floor(note / 12) - 1;
  return `Key ${key + 1} · ${noteName}${octave}`;
}

function getProfileDescription(value) {
  const profile = BOWL_PROFILES.find((item) => item.value === Number(value));
  return profile
    ? profile.description
    : "Different profiles are used across the preset.";
}

function renderFieldControl(config, dataAttribute, idPrefix, includeMixedOption) {
  const label = `Page ${config.page}: ${config.label}`;
  const descriptionId = `${idPrefix}-profile-description`;
  const control = config.kind === "select"
    ? `<select ${dataAttribute}="${config.field}" aria-label="${label}" aria-describedby="${descriptionId}">${config.options.map((option) => `<option value="${option.value}">${option.name}</option>`).join("")}${includeMixedOption ? '<option value="" disabled>Mixed values</option>' : ""}</select>`
    : `<input ${dataAttribute}="${config.field}" type="${config.kind}" min="${config.min}" max="${config.max}" step="${config.step}">`;
  const description = config.field === "profile"
    ? `<span id="${descriptionId}" class="profile-description" data-profile-description>${BOWL_PROFILES[0].description}</span>`
    : "";
  const output = config.kind === "select" ? "" : "<output></output>";
  return `<label>${label}${control}${description}${output}</label>`;
}

function renderEncoderGroups(configs, dataAttribute, idPrefix, includeMixedOption = false) {
  const encoders = [...new Set(configs.map((config) => config.encoder))];
  return encoders.map((encoder) => {
    const fields = configs
      .filter((config) => config.encoder === encoder)
      .sort((left, right) => left.page - right.page);
    const headingId = `${idPrefix}-encoder-${encoder}-heading`;
    return `<section class="encoder-control-row" aria-labelledby="${headingId}">
      <h4 id="${headingId}">Encoder ${encoder}</h4>
      <div class="encoder-control-fields" data-columns="${fields.length}">${fields.map((config) => renderFieldControl(config, dataAttribute, idPrefix, includeMixedOption)).join("")}</div>
    </section>`;
  }).join("");
}

function setMessage(message, type = "info") {
  elements.deviceMessage.textContent = message;
  elements.deviceMessage.dataset.state = type;
}

function setConnectionLabel(message, status = "") {
  elements.connectionState.textContent = message;
  elements.connectionState.dataset.state = status;
}

function setDraftDirty(dirty) {
  state.dirty = dirty;
  elements.draftState.textContent = !state.preset
    ? "No preset loaded"
    : dirty ? "Unsaved changes" : "Draft ready";
  elements.draftState.dataset.state = dirty ? "dirty" : "";
  updateEditorAvailability();
}

function updateEditorAvailability() {
  elements.editor.disabled = !state.preset;
  elements.metadataControls.disabled = !state.preset;
  const slotsUnavailable = !state.connected || !state.slotsLoaded
    || state.slots.busy || state.refreshingSlots || state.transferBusy;
  for (const button of elements.slotButtons.querySelectorAll("[data-slot]")) {
    button.disabled = slotsUnavailable;
  }
  elements.saveSlot.disabled = !state.connected || !state.slotsLoaded
    || !state.preset || state.slots.busy || state.refreshingSlots || state.transferBusy;
  elements.exportPreset.disabled = !state.preset;
}

async function connectToMidi({ automatic = false } = {}) {
  if (!automatic) {
    state.manualDisconnect = false;
    state.lastAttemptKey = null;
    state.lastAttemptTime = 0;
  }
  const alreadyEnabled = state.accessEnabled;
  try {
    if (!alreadyEnabled) {
      elements.connectMidi.disabled = true;
      await state.client.openAccess();
      state.accessEnabled = true;
      elements.disconnectMidi.disabled = false;
      setConnectionLabel("Waiting for CHOMPI-BOWLS");
      setMessage("MIDI/SysEx access granted. Searching for CHOMPI-BOWLS. Check USB and close other MIDI apps if it is not found.");
    } else if (!state.connected) {
      setConnectionLabel("Waiting for CHOMPI-BOWLS");
      setMessage("Searching for CHOMPI-BOWLS. Check USB and close other MIDI apps if it is not found.");
    }
    elements.connectMidi.disabled = false;
    startPolling();
    await scanForChompi();
  } catch (error) {
    if (!state.accessEnabled) {
      setConnectionLabel("MIDI access required");
      setMessage(automatic
        ? `Could not enable MIDI/SysEx automatically: ${error.message} Select Connect to retry.`
        : error.message, "error");
    } else {
      setMessage(error.message, "error");
    }
  } finally {
    elements.connectMidi.disabled = false;
  }
}

function disconnectFromMidi() {
  if (state.pollTimer !== null) {
    window.clearInterval(state.pollTimer);
    state.pollTimer = null;
  }
  state.client.disconnect();
  state.accessEnabled = false;
  state.manualDisconnect = true;
  state.connected = false;
  state.connecting = false;
  state.deviceKey = null;
  state.lastAttemptKey = null;
  state.slotsLoaded = false;
  state.slots = { valid: 0, incompatible: 0, dirty: 0, busy: false };
  elements.connectMidi.disabled = false;
  elements.disconnectMidi.disabled = true;
  setConnectionLabel("Disconnected");
  renderSlotKeybed();
  setMessage("Disconnected. Connect when you are ready to use CHOMPI-BOWLS.");
}

async function checkMidiPermission() {
  if (!navigator.permissions?.query) {
    await connectToMidi({ automatic: true });
    return;
  }
  let permission;
  try {
    permission = await navigator.permissions.query({ name: "midi", sysex: true });
  } catch (error) {
    setMessage(`Could not check MIDI/SysEx permission: ${error.message} Trying to connect automatically.`, "error");
    await connectToMidi({ automatic: true });
    return;
  }

  state.midiPermission = permission;
  permission.onchange = () => {
    if (permission.state === "granted" && !state.accessEnabled && !state.manualDisconnect) {
      void connectToMidi({ automatic: true });
    } else if (permission.state === "denied") {
      if (state.accessEnabled) {
        disconnectFromMidi();
        state.manualDisconnect = false;
      }
      setConnectionLabel("MIDI/SysEx access blocked", "error");
      setMessage("Allow MIDI/SysEx access in browser settings, then select Connect.", "error");
    }
  };

  if (permission.state === "denied") {
    setConnectionLabel("MIDI/SysEx access blocked", "error");
    setMessage("Allow MIDI/SysEx access in browser settings, then select Connect.", "error");
    return;
  }
  await connectToMidi({ automatic: true });
}

function startPolling() {
  if (state.pollTimer !== null) return;
  state.pollTimer = window.setInterval(() => {
    void scanForChompi();
    if (state.connected && !state.transferBusy
        && Date.now() - state.lastSlotRefresh >= 10000) {
      void refreshSlots({ quiet: true });
    }
  }, 2000);
}

async function scanForChompi() {
  if (!state.accessEnabled || state.connecting) return;
  const ports = state.client.findChompiPorts();
  if (!ports) {
    if (state.connected) {
      state.client.clearPorts();
      state.connected = false;
      state.deviceKey = null;
      state.slotsLoaded = false;
      state.slots = { valid: 0, incompatible: 0, dirty: 0, busy: false };
      renderSlotKeybed();
      setMessage("CHOMPI-BOWLS disconnected. Waiting for the device to return.");
    }
    setConnectionLabel("Waiting for CHOMPI-BOWLS");
    return;
  }

  const deviceKey = `${ports.input.id}:${ports.output.id}`;
  if (state.connected && state.deviceKey === deviceKey) return;
  if (state.lastAttemptKey === deviceKey
      && Date.now() - state.lastAttemptTime < 5000) return;

  state.connecting = true;
  state.lastAttemptKey = deviceKey;
  state.lastAttemptTime = Date.now();
  elements.disconnectMidi.disabled = true;
  try {
    state.client.selectPorts(ports.input.id, ports.output.id);
    setConnectionLabel("CHOMPI-BOWLS found");
    setMessage("Checking firmware compatibility…");
    await state.client.handshake();
    state.connected = true;
    state.deviceKey = deviceKey;
    state.slotsLoaded = false;
    state.slots = { valid: 0, incompatible: 0, dirty: 0, busy: false };
    setConnectionLabel("Connected · protocol v1", "connected");
    renderSlotKeybed();
    if (await refreshSlots({ quiet: true })) {
      setMessage("Connected. Select a saved slot to load it, or select an empty slot as the save destination.");
    }
  } catch (error) {
    state.client.clearPorts();
    state.connected = false;
    state.deviceKey = null;
    state.slotsLoaded = false;
    setConnectionLabel("CHOMPI-BOWLS connection failed", "error");
    renderSlotKeybed();
    const message = error.message === "The instrument did not respond."
      ? `${error.message} Check USB and other MIDI apps; the editor will keep retrying.`
      : error.message;
    setMessage(message, "error");
  } finally {
    state.connecting = false;
    elements.disconnectMidi.disabled = !state.accessEnabled;
  }
}

async function refreshSlots({ quiet = false } = {}) {
  if (!state.connected || state.transferBusy || state.refreshingSlots) return false;
  state.refreshingSlots = true;
  state.lastSlotRefresh = Date.now();
  updateEditorAvailability();
  try {
    state.slots = await state.client.listSlots();
    state.slotsLoaded = true;
    renderSlotKeybed();
    if (!quiet) {
      setMessage(state.slots.busy
        ? "Instrument storage is busy; try again shortly."
        : "Slot list updated.");
    }
    return true;
  } catch (error) {
    setMessage(quiet ? `Could not refresh slots: ${error.message}` : error.message, "error");
    return false;
  } finally {
    state.refreshingSlots = false;
    updateEditorAvailability();
  }
}

function setSlotStatus(message, stateName = "") {
  elements.slotStatus.textContent = message;
  elements.slotStatus.dataset.state = stateName;
}

function renderSlotKeybed() {
  elements.slotButtons.replaceChildren();
  let occupiedCount = 0;
  for (let slot = 0; slot < 15; slot += 1) {
    const bit = 1 << slot;
    const incompatible = (state.slots.incompatible & bit) !== 0;
    const valid = (state.slots.valid & bit) !== 0;
    if (incompatible || valid) occupiedCount += 1;
    const status = !state.slotsLoaded
      ? "unknown"
      : incompatible
        ? "incompatible"
        : valid
          ? (state.slots.dirty & bit) !== 0 ? "pending" : "saved"
          : "empty";
    const statusLabel = {
      unknown: "Unknown",
      incompatible: "Incompatible",
      pending: "Pending save",
      saved: "Saved",
      empty: "Empty",
    }[status];
    const button = document.createElement("button");
    button.type = "button";
    button.className = "slot-key";
    button.dataset.slot = String(slot);
    button.dataset.state = status;
    button.setAttribute("aria-label", `Slot ${slot + 1}, ${statusLabel}`);
    button.setAttribute("aria-pressed", String(state.selectedSlot === slot));
    button.title = `Slot ${String(slot + 1).padStart(2, "0")} · ${statusLabel}`;
    button.disabled = !state.connected || !state.slotsLoaded
      || state.slots.busy || state.refreshingSlots || state.transferBusy;
    const indicator = document.createElement("span");
    indicator.className = "slot-key-indicator";
    indicator.dataset.state = status;
    indicator.setAttribute("aria-hidden", "true");
    const number = document.createElement("span");
    number.className = "slot-key-number";
    number.textContent = String(slot + 1).padStart(2, "0");
    button.append(indicator, number);
    elements.slotButtons.append(button);
  }

  if (!state.connected) {
    setSlotStatus("Waiting for CHOMPI-BOWLS", "disconnected");
  } else if (!state.slotsLoaded) {
    setSlotStatus("Loading preset slots…", "loading");
  } else if (state.slots.busy) {
    setSlotStatus(`Storage busy · ${occupiedCount} of 15 slots occupied`, "busy");
  } else {
    setSlotStatus(`${occupiedCount} of 15 slots occupied · Slot ${String(state.selectedSlot + 1).padStart(2, "0")} selected`, "ready");
  }
  updateEditorAvailability();
}

function selectSlot(slot) {
  const slotBit = 1 << slot;
  const incompatible = (state.slots.incompatible & slotBit) !== 0;
  const canRead = (state.slots.valid & slotBit) !== 0
    && !incompatible && !state.slots.busy;
  if (canRead && state.dirty
      && !window.confirm(`Discard your unsaved draft and load slot ${slot + 1}?`)) {
    return;
  }

  state.selectedSlot = slot;
  renderSlotKeybed();
  if (incompatible) {
    setSlotStatus(`Slot ${slot + 1} is incompatible`, "error");
    setMessage(`Slot ${slot + 1} is incompatible and cannot be loaded.`);
  } else if (canRead) {
    setSlotStatus(`Loading slot ${slot + 1}…`, "loading");
    void readSelectedSlot();
  } else if (state.slots.busy) {
    setSlotStatus("Instrument storage is busy", "busy");
    setMessage("Instrument storage is busy; try again shortly.");
  } else {
    setSlotStatus(`Slot ${slot + 1} is empty`, "empty");
    setMessage(`Slot ${slot + 1} is empty. Select an empty slot as the save destination.`);
  }
}

function prepareEditor() {
  for (const tuning of TUNINGS) {
    elements.tuningSelect.add(new Option(tuning.name, tuning.id));
  }
  let whiteIndex = 0;
  for (let key = 0; key < 25; key += 1) {
    const black = BLACK_KEY_SEMITONES.has(key % 12);
    const button = document.createElement("button");
    button.type = "button";
    button.className = `piano-key ${black ? "piano-black-key" : "piano-white-key"}`;
    button.dataset.keyIndex = String(key);
    button.style.left = `${(whiteIndex / 15) * 100}%`;
    button.style.width = black ? `${(100 / 15) * 0.62}%` : `${100 / 15}%`;
    const label = document.createElement("span");
    label.className = "piano-key-label";
    button.append(label);
    button.setAttribute("aria-pressed", String(key === state.key));
    button.disabled = !state.preset;
    elements.bowlKeybed.append(button);
    if (!black) whiteIndex += 1;
  }
  elements.globalBowlControls.innerHTML = renderEncoderGroups(
    FIELD_CONFIG.filter((config) => config.field !== "pitch_hz"),
    "data-bank-global",
    "global",
    true,
  );
  elements.keyControls.innerHTML = renderEncoderGroups(
    FIELD_CONFIG,
    "data-key-field",
    "key",
  );
  renderKeybed();
}

function renderPreset() {
  if (!state.preset) {
    renderKeybed();
    updateEditorAvailability();
    setDraftDirty(false);
    return;
  }
  elements.presetName.value = state.preset.name;
  elements.presetComment.value = state.preset.comment;
  elements.startupMode.value = String(state.preset.mode);
  for (const input of document.querySelectorAll("[data-global]")) {
    input.value = String(state.preset[input.dataset.global]);
    const output = input.parentElement.querySelector("output");
    if (output) output.value = input.value;
  }
  elements.editMode.value = state.mode;
  renderGlobalBowlValues();
  renderKeyValues();
  updateEditorAvailability();
}

function renderKeybed() {
  for (const button of elements.bowlKeybed.querySelectorAll("[data-key-index]")) {
    const key = Number(button.dataset.keyIndex);
    const pitch = state.preset ? state.preset.pitch_hz[state.mode][key] : null;
    const label = pitch === null ? "—" : pitch.toFixed(1);
    const accessibleLabel = pitch === null
      ? `${getKeyLabel(key)} · No preset loaded`
      : `${getKeyLabel(key)} · ${label} Hz`;
    button.querySelector(".piano-key-label").textContent = label;
    button.title = accessibleLabel;
    button.setAttribute("aria-label", accessibleLabel);
    button.disabled = !state.preset;
    button.setAttribute("aria-pressed", String(key === state.key));
  }
  elements.selectedKeyLabel.textContent = `${getKeyLabel(state.key)} selected`;
}

function renderGlobalBowlValues() {
  if (!state.preset) return;
  for (const input of elements.globalBowlControls.querySelectorAll("[data-bank-global]")) {
    const field = input.dataset.bankGlobal;
    const values = ["strikes", "rimming", "split"].flatMap((mode) => state.preset[field][mode]);
    const firstValue = values[0];
    const uniform = values.every((value) => value === firstValue);
    input.value = input.tagName === "SELECT" && !uniform ? "" : String(firstValue);
    const output = input.parentElement.querySelector("output");
    if (output) output.value = uniform ? String(firstValue) : "Mixed";
    const description = input.parentElement.querySelector("[data-profile-description]");
    if (description) {
      description.textContent = uniform
        ? getProfileDescription(firstValue)
        : "Different profiles are used across the preset.";
    }
  }
}

function renderKeyValues() {
  if (!state.preset) return;
  for (const input of elements.keyControls.querySelectorAll("[data-key-field]")) {
    const field = input.dataset.keyField;
    const value = state.preset[field][state.mode][state.key];
    input.value = String(value);
    const output = input.parentElement.querySelector("output");
    if (output) output.value = field === "pitch_hz" ? `${Number(value).toFixed(3)} Hz` : String(value);
    const description = input.parentElement.querySelector("[data-profile-description]");
    if (description) description.textContent = getProfileDescription(value);
  }
  renderKeybed();
}

function markChanged() {
  setDraftDirty(true);
}

async function readSelectedSlot() {
  if (!state.connected || state.refreshingSlots || state.transferBusy) return;
  elements.progress.hidden = false;
  elements.progress.value = 0;
  state.transferBusy = true;
  updateEditorAvailability();
  setMessage("Reading preset…");
  try {
    const preset = validatePreset(await state.client.readPreset(state.selectedSlot));
    state.preset = preset;
    state.mode = ["strikes", "rimming", "split"][preset.mode];
    setDraftDirty(false);
    renderPreset();
    setSlotStatus(`Slot ${state.selectedSlot + 1} loaded`, "ready");
    setMessage(`Read slot ${state.selectedSlot + 1}.`);
  } catch (error) {
    setSlotStatus(`Could not read slot ${state.selectedSlot + 1}`, "error");
    setMessage(error.message, "error");
  } finally {
    state.transferBusy = false;
    updateEditorAvailability();
    elements.progress.hidden = true;
  }
}

async function saveToSelectedSlot() {
  if (!state.connected || !state.preset || state.refreshingSlots) return;
  const slot = state.selectedSlot;
  const occupied = ((state.slots.valid | state.slots.incompatible) & (1 << slot)) !== 0;
  if (occupied && !window.confirm(`Overwrite existing data in slot ${slot + 1}?`)) return;
  let written = false;
  try {
    validatePreset(state.preset);
    elements.progress.hidden = false;
    elements.progress.value = 0;
    state.transferBusy = true;
    updateEditorAvailability();
    setMessage("Sending preset to the instrument…");
    await state.client.writePreset(slot, state.preset, (progress) => {
      elements.progress.value = progress;
      setMessage(progress < 1 ? `Writing preset… ${Math.round(progress * 100)}%` : "Waiting for SD-card save…");
    });
    written = true;
  } catch (error) {
    setMessage(error.message, "error");
  } finally {
    state.transferBusy = false;
    updateEditorAvailability();
    elements.progress.hidden = true;
  }
  if (written) {
    const refreshed = await refreshSlots({ quiet: true });
    setDraftDirty(false);
    elements.draftState.textContent = "Saved to instrument";
    elements.draftState.dataset.state = "saved";
    if (refreshed && (state.slots.valid & (1 << slot)) !== 0) {
      setMessage(`Saved preset to slot ${slot + 1}.`);
    } else {
      setMessage(`Preset saved to slot ${slot + 1}, but the slot list did not confirm it. Retrying automatically.`, "error");
    }
  }
}

function createDraft() {
  if (state.dirty && !window.confirm("Discard your unsaved draft?")) return;
  state.preset = createFactoryPreset();
  state.preset.mode = 0;
  state.mode = "strikes";
  state.key = 0;
  setDraftDirty(true);
  renderPreset();
  setMessage("Factory-default draft created locally. Nothing has been written to the instrument.");
}

async function importPreset(file) {
  try {
    if (state.dirty && !window.confirm("Replace your unsaved draft with this file?")) return;
    const parsed = JSON.parse(await file.text());
    if (parsed && typeof parsed === "object") {
      delete parsed.delay_mix;
      delete parsed.delay_time;
    }
    state.preset = validatePreset(parsed);
    state.mode = ["strikes", "rimming", "split"][state.preset.mode];
    state.key = 0;
    setDraftDirty(true);
    renderPreset();
    setMessage(`Imported ${file.name}.`);
  } catch (error) {
    setMessage(error.message, "error");
  } finally {
    elements.importPreset.value = "";
  }
}

function exportPreset() {
  if (!state.preset) return;
  const blob = new Blob([`${JSON.stringify(state.preset, null, 2)}\n`], {
    type: "application/json",
  });
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = `CHOMPI-BOWLS-slot-${String(state.selectedSlot + 1).padStart(2, "0")}.json`;
  link.click();
  URL.revokeObjectURL(url);
}

function renderTuningPreview(tuning) {
  elements.tuningDescription.textContent = tuning?.description || "No tuning selected.";
  elements.tuningPreview.replaceChildren();
  if (!tuning) return;
  tuning.pitches.forEach((pitch, index) => {
    const cell = document.createElement("span");
    cell.className = "pitch-cell";
    cell.textContent = `${index + 1}: ${pitch.toFixed(3)} Hz`;
    elements.tuningPreview.append(cell);
  });
}

function applySelectedTuning() {
  const tuning = TUNINGS.find((item) => item.id === elements.tuningSelect.value);
  if (!tuning || !state.preset) return;
  state.preset = applyTuning(state.preset, tuning.pitches);
  markChanged();
  renderKeyValues();
  setMessage(`${tuning.name} applied to all 25 bowls in Strikes, Rimming, and Split. Individual pitches remain editable.`);
}

elements.connectMidi.addEventListener("click", connectToMidi);
elements.disconnectMidi.addEventListener("click", disconnectFromMidi);
elements.saveSlot.addEventListener("click", saveToSelectedSlot);
elements.newPreset.addEventListener("click", createDraft);
elements.exportPreset.addEventListener("click", exportPreset);
elements.importPreset.addEventListener("change", (event) => {
  const [file] = event.target.files;
  if (file) importPreset(file);
});
elements.slotButtons.addEventListener("click", (event) => {
  const button = event.target.closest("[data-slot]");
  if (!button || button.disabled) return;
  selectSlot(Number(button.dataset.slot));
});
elements.tuningSelect.addEventListener("change", () => {
  const tuning = TUNINGS.find((item) => item.id === elements.tuningSelect.value);
  renderTuningPreview(tuning);
  elements.applyTuning.disabled = !tuning || !state.preset;
});
elements.applyTuning.addEventListener("click", applySelectedTuning);
elements.editMode.addEventListener("change", (event) => {
  state.mode = event.target.value;
  renderKeyValues();
});
elements.bowlKeybed.addEventListener("click", (event) => {
  const button = event.target.closest("[data-key-index]");
  if (!button || button.disabled) return;
  state.key = Number(button.dataset.keyIndex);
  renderKeyValues();
});

document.addEventListener("input", (event) => {
  const input = event.target;
  if (input.matches("[data-metadata]") && state.preset) {
    const field = input.dataset.metadata;
    try {
      state.preset = setPresetValue(state.preset, field, null, null, input.value);
      markChanged();
      setMessage("");
    } catch (error) {
      input.value = state.preset[field];
      setMessage(error.message, "error");
    }
  }
  if (input.matches("[data-bank-global]") && state.preset) {
    const value = Number(input.value);
    if (!Number.isFinite(value)) return;
    try {
      state.preset = setBankValueForAllModes(state.preset, input.dataset.bankGlobal, value);
      renderGlobalBowlValues();
      renderKeyValues();
      markChanged();
    } catch (error) {
      setMessage(error.message, "error");
    }
  }
  if (input.matches("[data-global]") && state.preset) {
    const value = Number(input.value);
    if (!Number.isFinite(value)) return;
    try {
      state.preset = setPresetValue(state.preset, input.dataset.global, null, null, value);
      const output = input.parentElement.querySelector("output");
      if (output) output.value = String(value);
      markChanged();
    } catch (error) {
      setMessage(error.message, "error");
    }
  }
  if (input.matches("[data-key-field]") && state.preset) {
    const value = Number(input.value);
    if (!Number.isFinite(value)) return;
    const field = input.dataset.keyField;
    try {
      state.preset = setPresetValue(state.preset, field, state.mode, state.key, value);
      const output = input.parentElement.querySelector("output");
      if (output) output.value = field === "pitch_hz" ? `${value.toFixed(3)} Hz` : String(value);
      const description = input.parentElement.querySelector("[data-profile-description]");
      if (description) description.textContent = getProfileDescription(value);
      if (field === "pitch_hz") renderKeybed();
      renderGlobalBowlValues();
      markChanged();
    } catch (error) {
      setMessage(error.message, "error");
    }
  }
});

state.client = new PresetTransferClient(
  (message) => {
    if (!state.connected) setMessage(message);
  },
  () => { void scanForChompi(); },
);
prepareEditor();
renderSlotKeybed();
setDraftDirty(false);
void checkMidiPermission();
