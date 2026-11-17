export const MODES = ["strikes", "rimming", "split"];
export const KEY_COUNT = 25;

const GLOBAL_RANGES = {
  mode: [0, 2],
  volume: [0, 32],
  strike: [0, 32],
  shimmer: [0, 32],
  reverb_mix: [0, 32],
  delay_mix: [0, 32],
  delay_time: [0, 32],
};

const BANK_RANGES = {
  pan: [0, 32],
  bvol: [0, 32],
  profile: [0, 4],
  bright: [0, 32],
  decay: [0, 32],
};

const FACTORY_PITCHES = [
  99, 113.066, 129.879, 149.192, 171.376, 198, 226.132, 259.758,
  298.383, 342.752, 396, 452.265, 519.516, 596.767, 685.505, 792,
  904.529, 1039.031, 1193.533, 1371.01, 1584, 1809.058, 2078.062,
  2387.067, 2742.02,
];

export function createFactoryPreset() {
  const banks = Object.fromEntries(
    Object.keys(BANK_RANGES).map((field) => [
      field,
      Object.fromEntries(MODES.map((mode) => [mode, Array(KEY_COUNT).fill(
        field === "pan" ? 16 : field === "bvol" ? 16 : field === "bright" ? 16 : 0,
      )])),
    ]),
  );
  return {
    version: 11,
    mode: 2,
    volume: 8,
    strike: 32,
    shimmer: 0,
    reverb_mix: 0,
    delay_mix: 0,
    delay_time: 15,
    ...banks,
    pitch_hz: Object.fromEntries(MODES.map((mode) => [mode, [...FACTORY_PITCHES]])),
  };
}

export function clonePreset(preset) {
  return JSON.parse(JSON.stringify(preset));
}

export function validatePreset(preset) {
  if (!preset || typeof preset !== "object" || preset.version !== 11) {
    throw new Error("Preset must use schema version 11.");
  }
  for (const [field, [min, max]] of Object.entries(GLOBAL_RANGES)) {
    const value = preset[field];
    if (!Number.isInteger(value) || value < min || value > max) {
      throw new Error(`Invalid ${field} value.`);
    }
  }
  for (const field of Object.keys(BANK_RANGES)) {
    validateModeArrays(preset[field], field);
  }
  validateModeArrays(preset.pitch_hz, "pitch_hz", true);
  return preset;
}

function validateModeArrays(value, field, pitch = false) {
  if (!value || typeof value !== "object") {
    throw new Error(`Missing ${field} mode arrays.`);
  }
  for (const mode of MODES) {
    const values = value[mode];
    if (!Array.isArray(values) || values.length !== KEY_COUNT) {
      throw new Error(`${field}.${mode} must contain 25 values.`);
    }
    for (const item of values) {
      if (typeof item !== "number" || !Number.isFinite(item)) {
        throw new Error(`${field}.${mode} contains a non-numeric value.`);
      }
      if (pitch) {
        if (item < 40 || item > 10000) {
          throw new Error(`${field}.${mode} pitches must be 40-10,000 Hz.`);
        }
      } else {
        const [min, max] = BANK_RANGES[field];
        if (!Number.isInteger(item) || item < min || item > max) {
          throw new Error(`Invalid ${field}.${mode} value.`);
        }
      }
    }
  }
}

export function applyTuning(preset, pitches) {
  validatePreset(preset);
  if (!Array.isArray(pitches) || pitches.length !== KEY_COUNT
      || pitches.some((pitch) => !Number.isFinite(pitch) || pitch < 40 || pitch > 10000)) {
    throw new Error("A tuning must contain 25 pitches from 40 to 10,000 Hz.");
  }
  const updated = clonePreset(preset);
  const splitPitches = [
    ...pitches.slice(0, 12),
    ...pitches.slice(0, 12),
    pitches[12],
  ];
  updated.pitch_hz = {
    strikes: [...pitches],
    rimming: [...pitches],
    split: splitPitches,
  };
  return updated;
}

export function setPresetValue(preset, field, mode, key, value) {
  const updated = clonePreset(preset);
  if (mode === null) {
    updated[field] = value;
  } else {
    updated[field][mode][key] = value;
  }
  return validatePreset(updated);
}

export function setBankValueForAllModes(preset, field, value) {
  if (!Object.prototype.hasOwnProperty.call(BANK_RANGES, field)) {
    throw new Error(`Unknown per-key field: ${field}.`);
  }
  const updated = clonePreset(preset);
  for (const mode of MODES) {
    updated[field][mode] = Array(KEY_COUNT).fill(value);
  }
  return validatePreset(updated);
}
