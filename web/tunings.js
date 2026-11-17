const tibetanBowls = [
  99, 112.7, 128.3, 146, 167.3, 191.7, 219.6, 261, 310.2, 347.8,
  390, 425.9, 465, 513, 564.1, 620.4, 693.6, 780, 850.3, 927,
  1026, 1128.3, 1240.8, 1395, 1560,
];

const slendro = [
  99, 113.066, 129.879, 149.192, 171.376, 198, 226.132, 259.758,
  298.383, 342.752, 396, 452.265, 519.516, 596.767, 685.505, 792,
  904.529, 1039.031, 1193.533, 1371.01, 1584, 1809.058, 2078.062,
  2387.067, 2742.02,
];

const pelogSelisir = [
  99, 108.586, 124.014, 147.478, 166.497, 198, 217.171, 248.028,
  294.956, 332.995, 396, 434.343, 496.055, 589.912, 665.99, 792,
  868.685, 992.11, 1179.825, 1331.98, 1584, 1737.371, 1984.22,
  2359.649, 2663.96,
];

const shrutiRatios = [
  1, 256 / 243, 16 / 15, 10 / 9, 9 / 8, 32 / 27, 6 / 5, 5 / 4,
  81 / 64, 4 / 3, 27 / 20, 45 / 32, 729 / 512, 3 / 2, 128 / 81,
  8 / 5, 5 / 3, 27 / 16, 16 / 9, 9 / 5, 15 / 8, 243 / 128,
];

const fromGenerator = (generator) => Array.from({ length: 25 }, (_, key) => generator(key));
const anchorHz = 99;

export const TUNINGS = [
  {
    id: "tibetan",
    name: "Tibetan bowls",
    description: "Pitch set from the bundled default.json example.",
    pitches: tibetanBowls,
  },
  {
    id: "slendro",
    name: "Sléndro",
    description: "Pitch set from the bundled Sléndro-inspired example.",
    pitches: slendro,
  },
  {
    id: "pelog",
    name: "Pélog Selisir",
    description: "Pitch set from the bundled Pélog Selisir-inspired example.",
    pitches: pelogSelisir,
  },
  {
    id: "harmonic",
    name: "Harmonic series",
    description: "Harmonics 1–25, anchored at 99 Hz.",
    pitches: fromGenerator((key) => anchorHz * (key + 1)),
  },
  {
    id: "12tet",
    name: "12-TET",
    description: "Equal semitones, anchored at 99 Hz.",
    pitches: fromGenerator((key) => anchorHz * (2 ** (key / 12))),
  },
  {
    id: "arabic-24tet",
    name: "Arabic 24-TET",
    description: "24 equal divisions of the octave, anchored at 99 Hz.",
    pitches: fromGenerator((key) => anchorHz * (2 ** (key / 24))),
  },
  {
    id: "thai-7tet",
    name: "Thai 7-TET",
    description: "Seven equal divisions of the octave, anchored at 99 Hz.",
    pitches: fromGenerator((key) => anchorHz * (2 ** (key / 7))),
  },
  {
    id: "indian-22-shruti",
    name: "Indian 22-Shruti",
    description: "A 22-shruti just-ratio reference set, anchored at 99 Hz.",
    pitches: fromGenerator((key) => (
      anchorHz * (2 ** Math.floor(key / 22)) * shrutiRatios[key % 22]
    )),
  },
];
