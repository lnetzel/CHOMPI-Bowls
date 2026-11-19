export const MODES = ["strikes", "rimming", "split"];
export const KEY_COUNT = 25;
export const PRESET_VERSION = 1;
export const METADATA_BYTE_LIMITS = Object.freeze({
  name: 48,
  comment: 128,
});

const GLOBAL_RANGES = {
  mode: [0, 2],
  volume: [0, 32],
  strike: [0, 32],
  shimmer: [0, 32],
  reverb_mix: [0, 32],
  reverb_decay: [0, 32],
};

const BANK_RANGES = {
  pan: [0, 32],
  bvol: [0, 32],
  profile: [0, 4],
  bright: [0, 32],
  decay: [0, 32],
  beat: [0, 32],
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
        field === "pan" || field === "bvol" || field === "bright" ? 16
          : field === "beat" ? 8 : 0,
      )])),
    ]),
  );
  return {
    version: PRESET_VERSION,
    name: "",
    comment: "",
    mode: 2,
    volume: 8,
    strike: 32,
    shimmer: 0,
    reverb_mix: 0,
    reverb_decay: 4,
    ...banks,
    pitch_hz: Object.fromEntries(MODES.map((mode) => [mode, [...FACTORY_PITCHES]])),
  };
}

export function clonePreset(preset) {
  return JSON.parse(JSON.stringify(preset));
}

export function validatePreset(preset) {
  if (!preset || typeof preset !== "object") {
    throw new Error(`Preset must use schema version ${PRESET_VERSION}.`);
  }
  const normalized = Object.prototype.hasOwnProperty.call(preset, "reverb_decay")
    ? preset
    : { ...preset, reverb_decay: 4 };
  if (normalized.version !== PRESET_VERSION) {
    throw new Error(`Preset must use schema version ${PRESET_VERSION}.`);
  }
  validateMetadata(normalized.name, "name", METADATA_BYTE_LIMITS.name);
  validateMetadata(normalized.comment, "comment", METADATA_BYTE_LIMITS.comment);
  for (const [field, [min, max]] of Object.entries(GLOBAL_RANGES)) {
    const value = normalized[field];
    if (!Number.isInteger(value) || value < min || value > max) {
      throw new Error(`Invalid ${field} value.`);
    }
  }
  for (const field of Object.keys(BANK_RANGES)) {
    validateModeArrays(normalized[field], field);
  }
  validateModeArrays(normalized.pitch_hz, "pitch_hz", true);
  return normalized;
}

function validateMetadata(value, field, maxBytes) {
  if (typeof value !== "string" || value.includes("\0")) {
    throw new Error(`Invalid preset ${field}.`);
  }
  for (let i = 0; i < value.length; i += 1) {
    const unit = value.charCodeAt(i);
    if (unit >= 0xd800 && unit <= 0xdbff) {
      const next = value.charCodeAt(i + 1);
      if (next < 0xdc00 || next > 0xdfff) {
        throw new Error(`Preset ${field} contains invalid Unicode.`);
      }
      i += 1;
    } else if (unit >= 0xdc00 && unit <= 0xdfff) {
      throw new Error(`Preset ${field} contains invalid Unicode.`);
    }
  }
  if (new TextEncoder().encode(value).length > maxBytes) {
    throw new Error(`Preset ${field} must be ${maxBytes} UTF-8 bytes or less.`);
  }
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
