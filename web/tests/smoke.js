import assert from "node:assert/strict";
import {
  applyTuning,
  createFactoryPreset,
  setPresetValue,
  validatePreset,
} from "../preset-model.js";
import { TUNINGS } from "../tunings.js";
import { decodeFrame, encodeFrame } from "../preset-transfer.js";

const factory = createFactoryPreset();
assert.equal(validatePreset(factory), factory);
assert.equal(factory.version, 11);
assert.deepEqual(factory.pitch_hz.strikes, factory.pitch_hz.rimming);
assert.deepEqual(factory.pitch_hz.strikes, factory.pitch_hz.split);

assert.equal(TUNINGS.length, 8);
for (const tuning of TUNINGS) {
  assert.equal(tuning.pitches.length, 25);
  assert.ok(tuning.pitches.every((pitch) => pitch >= 40 && pitch <= 10000));
}

const unchanged = JSON.stringify(factory);
const tuned = applyTuning(factory, TUNINGS[2].pitches);
assert.equal(JSON.stringify(factory), unchanged);
for (const mode of ["strikes", "rimming", "split"]) {
  assert.deepEqual(tuned.pitch_hz[mode], TUNINGS[2].pitches);
}
const edited = setPresetValue(tuned, "pitch_hz", "rimming", 4, 444.125);
assert.equal(edited.pitch_hz.rimming[4], 444.125);
assert.equal(edited.pitch_hz.strikes[4], TUNINGS[2].pitches[4]);
assert.equal(edited.pitch_hz.split[4], TUNINGS[2].pitches[4]);

assert.throws(
  () => applyTuning(factory, Array(25).fill(10001)),
  /40 to 10,000 Hz/,
);
assert.throws(() => validatePreset({ ...factory, version: 10 }), /version 11/);

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

console.log("Web preset model and SysEx protocol checks passed.");
