import assert from "node:assert/strict";
import {
  applyTuning,
  createFactoryPreset,
  METADATA_BYTE_LIMITS,
  randomizeBowlValues,
  setBankValueForAllModes,
  setPresetValue,
  validatePreset,
} from "../preset-model.js";
import { TUNINGS } from "../tunings.js";
import {
  PresetTransferClient,
  decode21,
  decodeFrame,
  encode21,
  encodeFrame,
} from "../preset-transfer.js";

const factory = createFactoryPreset();
assert.equal(validatePreset(factory), factory);
assert.equal(factory.version, 1);
assert.equal(factory.shimmer, 0);
assert.equal(factory.reverb_decay, 4);
assert.equal("volume" in factory, false);
assert.equal("delay_mix" in factory, false);
assert.equal("delay_time" in factory, false);
assert.deepEqual(factory.beat.strikes, Array(25).fill(8));
assert.equal(factory.name, "");
assert.equal(factory.comment, "");
const legacyPreset = { ...factory };
delete legacyPreset.reverb_decay;
assert.equal(validatePreset(legacyPreset).reverb_decay, 4);
assert.equal("reverb_decay" in legacyPreset, false);
assert.deepEqual(factory.pitch_hz.strikes, factory.pitch_hz.rimming);
assert.deepEqual(factory.pitch_hz.strikes, factory.pitch_hz.split);
const presetWithLegacyVolume = { ...factory, volume: 9 };
const normalizedPreset = validatePreset(presetWithLegacyVolume);
assert.equal("volume" in normalizedPreset, false);
assert.equal(presetWithLegacyVolume.volume, 9);

assert.equal(TUNINGS.length, 6);
assert.ok(!TUNINGS.some((tuning) => ["arabic-24tet", "thai-7tet"].includes(tuning.id)));
for (const tuning of TUNINGS) {
  assert.equal(tuning.pitches.length, 25);
  assert.ok(tuning.pitches.every((pitch) => pitch >= 40 && pitch <= 10000));
}
const twelveTet = TUNINGS.find((tuning) => tuning.id === "12tet");
assert.ok(Math.abs(twelveTet.pitches[0] - 130.8128) < 0.001);
assert.ok(Math.abs(twelveTet.pitches[21] - 440) < 1e-9);

const unchanged = JSON.stringify(factory);
const tuned = applyTuning(factory, TUNINGS[2].pitches);
assert.equal(JSON.stringify(factory), unchanged);
assert.deepEqual(tuned.pitch_hz.strikes, TUNINGS[2].pitches);
assert.deepEqual(tuned.pitch_hz.rimming, TUNINGS[2].pitches);
assert.deepEqual(tuned.pitch_hz.split, [
  ...TUNINGS[2].pitches.slice(0, 12),
  ...TUNINGS[2].pitches.slice(0, 12),
  TUNINGS[2].pitches[12],
]);
const edited = setPresetValue(tuned, "pitch_hz", "rimming", 4, 444.125);
assert.equal(edited.pitch_hz.rimming[4], 444.125);
assert.equal(edited.pitch_hz.strikes[4], TUNINGS[2].pitches[4]);
assert.equal(edited.pitch_hz.split[4], TUNINGS[2].pitches[4]);

const bankValues = { pan: 22, bvol: 13, profile: 3, bright: 9, decay: 18, beat: 11 };
let globalized = factory;
for (const [field, value] of Object.entries(bankValues)) {
  globalized = setBankValueForAllModes(globalized, field, value);
  for (const mode of ["strikes", "rimming", "split"]) {
    assert.deepEqual(globalized[field][mode], Array(25).fill(value));
  }
}
assert.deepEqual(factory.pan.strikes, Array(25).fill(16));
const randomized = randomizeBowlValues(factory);
const randomRanges = {
  pan: [8, 24],
  profile: [0, 4],
  bright: [8, 24],
  beat: [0, 16],
  decay: [8, 24],
};
for (const [field, [min, max]] of Object.entries(randomRanges)) {
  for (const mode of ["strikes", "rimming", "split"]) {
    const values = randomized[field][mode];
    assert.equal(values.length, 25);
    assert.ok(values.every((value) => value >= min && value <= max));
    assert.ok(new Set(values).size >= (field === "profile" ? 5 : 15));
  }
}
assert.deepEqual(randomized.bvol, factory.bvol);
assert.deepEqual(randomized.pitch_hz, factory.pitch_hz);
assert.deepEqual(factory, createFactoryPreset());
const afterGlobal = setPresetValue(globalized, "bright", "rimming", 4, 20);
assert.equal(afterGlobal.bright.rimming[4], 20);
assert.equal(afterGlobal.bright.rimming[3], 9);
assert.equal(afterGlobal.bright.strikes[4], 9);
assert.deepEqual(afterGlobal.pitch_hz, factory.pitch_hz);
assert.throws(
  () => setBankValueForAllModes(factory, "strike", 4),
  /Unknown per-key field/,
);
assert.throws(
  () => setBankValueForAllModes(factory, "bright", 33),
  /Invalid bright\./,
);

assert.throws(
  () => applyTuning(factory, Array(25).fill(10001)),
  /40 to 10,000 Hz/,
);
assert.throws(() => validatePreset({ ...factory, version: 2 }), /version 1/);

let namedPreset = setPresetValue(factory, "name", null, null, 'Bowl "One" \\copy');
namedPreset = setPresetValue(namedPreset, "comment", null, null, "Line one\nCafé 🥣");
const metadataRoundTrip = validatePreset(JSON.parse(JSON.stringify(namedPreset)));
assert.equal(metadataRoundTrip.name, 'Bowl "One" \\copy');
assert.equal(metadataRoundTrip.comment, "Line one\nCafé 🥣");
assert.equal("volume" in metadataRoundTrip, false);
assert.throws(
  () => setPresetValue(factory, "name", null, null, "🥣".repeat(13)),
  /48 UTF-8 bytes/,
);
const maxLengthName = setPresetValue(factory, "name", null, null, "🥣".repeat(12));
assert.equal(new TextEncoder().encode(maxLengthName.name).length, METADATA_BYTE_LIMITS.name);
assert.throws(
  () => setPresetValue(factory, "comment", null, null, "c".repeat(METADATA_BYTE_LIMITS.comment + 1)),
  /128 UTF-8 bytes/,
);

const packet = encodeFrame(6, 42, [0, 3, 127, 65, 66]);
const decoded = decodeFrame(packet);
assert.deepEqual(decoded, {
  command: 6,
  sequence: 42,
  payload: [0, 3, 127, 65, 66],
});
const damaged = Uint8Array.from(packet);
damaged[8] ^= 1;
assert.equal(decodeFrame(damaged), null);

for (const value of [0, 127, 128, 16383, 16384, 0x4000, 0x1fffff]) {
  const encoded = encode21(value);
  assert.ok(encoded.every((byte) => byte <= 0x7f));
  assert.equal(decode21(...encoded), value);
}
assert.throws(() => encode21(0x200000), /Invalid 21-bit value/);

const client = new PresetTransferClient();
const input = { id: "in-1", name: "CHOMPI-BOWLS" };
const output = { id: "out-1", name: "CHOMPI-BOWLS" };
client.access = {
  inputs: new Map([[input.id, input]]),
  outputs: new Map([[output.id, output]]),
};
assert.deepEqual(client.findChompiPorts(), { input, output });
client.access.inputs.set("removed", { id: "removed", name: "CHOMPI-BOWLS", state: "disconnected" });
client.access.inputs.delete(input.id);
client.access.outputs.set(output.id, output);
assert.equal(client.findChompiPorts(), null);

const compatibleClient = new PresetTransferClient();
compatibleClient.request = async () => ({ payload: [0, 1, 1, 15, 1] });
await compatibleClient.handshake();
compatibleClient.request = async () => ({ payload: [0, 1, 2, 15, 1] });
await assert.rejects(compatibleClient.handshake(), /protocol v1/);

const slotClient = new PresetTransferClient();
slotClient.request = async () => ({
  payload: [0, ...encode21(1 << 14), ...encode21(0), ...encode21(0), 0],
});
const slots = await slotClient.listSlots();
assert.equal(slots.valid, 1 << 14);

console.log("Web preset model and SysEx protocol checks passed.");
