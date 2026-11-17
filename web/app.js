import {
  applyTuning,
  createFactoryPreset,
  setPresetValue,
  validatePreset,
} from "./preset-model.js";
import { TUNINGS } from "./tunings.js";
import { PresetTransferClient } from "./preset-transfer.js";

const byId = (id) => document.getElementById(id);
const elements = {
  connect: byId("connect-midi"),
  disconnect: byId("disconnect-midi"),
  inputPort: byId("midi-input"),
  outputPort: byId("midi-output"),
  useMidi: byId("use-midi"),
  refreshSlots: byId("refresh-slots"),
  deviceMessage: byId("device-message"),
  connectionState: byId("connection-state"),
  slot: byId("preset-slot"),
  readSlot: byId("read-slot"),
  saveSlot: byId("save-slot"),
  progress: byId("transfer-progress"),
  newPreset: byId("new-preset"),
  importPreset: byId("import-preset"),
  exportPreset: byId("export-preset"),
  editor: byId("editor-controls"),
  draftState: byId("draft-state"),
  tuningSelect: byId("tuning-select"),
  tuningDescription: byId("tuning-description"),
  tuningPreview: byId("tuning-preview"),
  applyTuning: byId("apply-tuning"),
  startupMode: byId("startup-mode"),
  editMode: byId("edit-mode"),
  editKey: byId("edit-key"),
  keyControls: byId("key-controls"),
};

const state = {
  client: null,
  slots: { valid: 0, incompatible: 0, dirty: 0, busy: false },
  connected: false,
  preset: null,
  selectedSlot: 0,
  mode: "strikes",
  key: 0,
  dirty: false,
};

const MODE_NAMES = { strikes: "Strikes", rimming: "Rimming", split: "Split" };
const FIELD_CONFIG = [
  { field: "pan", label: "Pan", kind: "range", min: 0, max: 32, step: 1 },
  { field: "bvol", label: "Bowl volume", kind: "range", min: 0, max: 32, step: 1 },
  { field: "profile", label: "Bowl profile", kind: "select", values: [0, 1, 2, 3, 4] },
  { field: "bright", label: "Brightness", kind: "range", min: 0, max: 32, step: 1 },
  { field: "decay", label: "Decay", kind: "range", min: 0, max: 32, step: 1 },
  { field: "pitch_hz", label: "Pitch (Hz)", kind: "number", min: 40, max: 10000, step: 0.001 },
];

function setMessage(message, type = "info") {
  elements.deviceMessage.textContent = message;
  elements.deviceMessage.dataset.state = type;
}

function setConnectionLabel(message, connected = false) {
  elements.connectionState.textContent = message;
  elements.connectionState.dataset.state = connected ? "connected" : "";
}

function setDraftDirty(dirty) {
  state.dirty = dirty;
  elements.draftState.textContent = !state.preset
    ? "No preset loaded"
    : dirty ? "Unsaved changes" : "Draft ready";
  elements.draftState.dataset.state = dirty ? "dirty" : "";
  elements.exportPreset.disabled = !state.preset;
  elements.saveSlot.disabled = !state.connected || !state.preset;
}

function updateEditorAvailability() {
  elements.editor.disabled = !state.preset;
  elements.readSlot.disabled = !state.connected || state.slots.busy
    || (state.slots.valid & (1 << state.selectedSlot)) === 0;
  elements.saveSlot.disabled = !state.connected || !state.preset || state.slots.busy;
  elements.exportPreset.disabled = !state.preset;
}

function renderPorts(access) {
  const inputs = [...access.inputs.values()];
  const outputs = [...access.outputs.values()];
  fillPortSelect(elements.inputPort, inputs, "No MIDI inputs found");
  fillPortSelect(elements.outputPort, outputs, "No MIDI outputs found");
  elements.inputPort.disabled = inputs.length === 0;
  elements.outputPort.disabled = outputs.length === 0;
  elements.useMidi.disabled = inputs.length === 0 || outputs.length === 0;
}

function fillPortSelect(select, ports, emptyText) {
  const previous = select.value;
  select.replaceChildren();
  if (ports.length === 0) {
    select.add(new Option(emptyText, ""));
    return;
  }
  for (const port of ports) {
    select.add(new Option(port.name || port.manufacturer || port.id, port.id));
  }
  if (ports.some((port) => port.id === previous)) select.value = previous;
}

async function connectToMidi() {
  try {
    const access = await state.client.openAccess();
    renderPorts(access);
    setMessage("MIDI access granted. Select the instrument input and output.");
  } catch (error) {
    setMessage(error.message, "error");
  }
}

async function useSelectedDevice() {
  try {
    state.client.selectPorts(elements.inputPort.value, elements.outputPort.value);
    setMessage("Checking firmware compatibility…");
    await state.client.handshake();
    state.connected = true;
    setConnectionLabel("Connected · protocol v1", true);
    elements.disconnect.disabled = false;
    elements.refreshSlots.disabled = false;
    elements.slot.disabled = false;
    await refreshSlots();
    setMessage("Connected. Presets are read or written only when you choose a slot action.");
  } catch (error) {
    state.connected = false;
    setConnectionLabel("Not connected");
    updateEditorAvailability();
    setMessage(error.message, "error");
  }
}

function disconnectFromMidi() {
  state.client?.disconnect();
  state.client = new PresetTransferClient((message) => setMessage(message));
  state.connected = false;
  state.slots = { valid: 0, incompatible: 0, dirty: 0, busy: false };
  elements.disconnect.disabled = true;
  elements.refreshSlots.disabled = true;
  elements.slot.disabled = true;
  elements.inputPort.disabled = true;
  elements.outputPort.disabled = true;
  elements.useMidi.disabled = true;
  setConnectionLabel("Not connected");
  renderSlotOptions();
  updateEditorAvailability();
  setMessage("Disconnected.");
}

async function refreshSlots() {
  if (!state.connected) return;
  try {
    state.slots = await state.client.listSlots();
    renderSlotOptions();
    elements.readSlot.disabled = state.slots.busy
      || (state.slots.valid & (1 << state.selectedSlot)) === 0;
    setMessage(state.slots.busy ? "Instrument storage is busy; try again shortly." : "Slot list updated.");
  } catch (error) {
    setMessage(error.message, "error");
  }
}

function renderSlotOptions() {
  const current = state.selectedSlot;
  elements.slot.replaceChildren();
  for (let slot = 0; slot < 15; slot += 1) {
    const bit = 1 << slot;
    const label = (state.slots.incompatible & bit) !== 0
      ? "Incompatible"
      : (state.slots.valid & bit) !== 0
        ? (state.slots.dirty & bit) !== 0 ? "Pending save" : "Saved"
        : "Empty";
    elements.slot.add(new Option(`Slot ${String(slot + 1).padStart(2, "0")} — ${label}`, slot));
  }
  elements.slot.value = String(current);
  updateEditorAvailability();
}

function prepareEditor() {
  for (const tuning of TUNINGS) {
    elements.tuningSelect.add(new Option(tuning.name, tuning.id));
  }
  for (let key = 0; key < 25; key += 1) {
    const note = 48 + key;
    const noteName = ["C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B"][note % 12];
    const octave = Math.floor(note / 12) - 1;
    elements.editKey.add(new Option(`Key ${key + 1} · ${noteName}${octave}`, key));
  }
  elements.keyControls.innerHTML = FIELD_CONFIG.map((config) => {
    const control = config.kind === "select"
      ? `<select data-key-field="${config.field}">${config.values.map((value) => `<option value="${value}">${value}</option>`).join("")}</select>`
      : `<input data-key-field="${config.field}" type="${config.kind}" min="${config.min}" max="${config.max}" step="${config.step}">`;
    return `<label>${config.label}${control}<output></output></label>`;
  }).join("");
}

function renderPreset() {
  if (!state.preset) {
    updateEditorAvailability();
    setDraftDirty(false);
    return;
  }
  elements.startupMode.value = String(state.preset.mode);
  for (const input of document.querySelectorAll("[data-global]")) {
    input.value = String(state.preset[input.dataset.global]);
    const output = input.parentElement.querySelector("output");
    if (output) output.value = input.value;
  }
  elements.editMode.value = state.mode;
  elements.editKey.value = String(state.key);
  renderKeyValues();
  updateEditorAvailability();
}

function renderKeyValues() {
  if (!state.preset) return;
  for (const input of elements.keyControls.querySelectorAll("[data-key-field]")) {
    const field = input.dataset.keyField;
    const value = state.preset[field][state.mode][state.key];
    input.value = String(value);
    const output = input.parentElement.querySelector("output");
    if (output) output.value = field === "pitch_hz" ? `${Number(value).toFixed(3)} Hz` : String(value);
  }
}

function markChanged() {
  setDraftDirty(true);
}

async function readSelectedSlot() {
  if (!state.connected) return;
  if (state.dirty && !window.confirm("Discard your unsaved draft and read this slot?")) return;
  elements.progress.hidden = false;
  elements.progress.value = 0;
  setMessage("Reading preset…");
  try {
    const preset = validatePreset(await state.client.readPreset(state.selectedSlot));
    state.preset = preset;
    state.mode = ["strikes", "rimming", "split"][preset.mode];
    setDraftDirty(false);
    renderPreset();
    setMessage(`Read slot ${state.selectedSlot + 1}.`);
  } catch (error) {
    setMessage(error.message, "error");
  } finally {
    elements.progress.hidden = true;
  }
}

async function saveToSelectedSlot() {
  if (!state.connected || !state.preset) return;
  const slot = state.selectedSlot;
  const occupied = ((state.slots.valid | state.slots.incompatible) & (1 << slot)) !== 0;
  if (occupied && !window.confirm(`Overwrite existing data in slot ${slot + 1}?`)) return;
  try {
    validatePreset(state.preset);
    elements.progress.hidden = false;
    elements.progress.value = 0;
    setMessage("Sending preset to the instrument…");
    await state.client.writePreset(slot, state.preset, (progress) => {
      elements.progress.value = progress;
      setMessage(progress < 1 ? `Writing preset… ${Math.round(progress * 100)}%` : "Waiting for SD-card save…");
    });
    await refreshSlots();
    setDraftDirty(false);
    elements.draftState.textContent = "Saved to instrument";
    elements.draftState.dataset.state = "saved";
    setMessage(`Saved preset to slot ${slot + 1}.`);
  } catch (error) {
    setMessage(error.message, "error");
  } finally {
    elements.progress.hidden = true;
  }
}

function createDraft() {
  if (state.dirty && !window.confirm("Discard your unsaved draft?")) return;
  state.preset = createFactoryPreset();
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
  setMessage(`${tuning.name} applied to Strikes, Rimming, and Split. Individual pitches remain editable.`);
}

elements.connect.addEventListener("click", connectToMidi);
elements.disconnect.addEventListener("click", disconnectFromMidi);
elements.useMidi.addEventListener("click", useSelectedDevice);
elements.refreshSlots.addEventListener("click", refreshSlots);
elements.readSlot.addEventListener("click", readSelectedSlot);
elements.saveSlot.addEventListener("click", saveToSelectedSlot);
elements.newPreset.addEventListener("click", createDraft);
elements.exportPreset.addEventListener("click", exportPreset);
elements.importPreset.addEventListener("change", (event) => {
  const [file] = event.target.files;
  if (file) importPreset(file);
});
elements.slot.addEventListener("change", (event) => {
  if (state.dirty && !window.confirm("Keep this draft while changing the destination slot?")) {
    event.target.value = String(state.selectedSlot);
    return;
  }
  state.selectedSlot = Number(event.target.value);
  updateEditorAvailability();
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
elements.editKey.addEventListener("change", (event) => {
  state.key = Number(event.target.value);
  renderKeyValues();
});

document.addEventListener("input", (event) => {
  const input = event.target;
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
      markChanged();
    } catch (error) {
      setMessage(error.message, "error");
    }
  }
});

state.client = new PresetTransferClient((message) => {
  if (!state.connected) setMessage(message);
});
prepareEditor();
renderSlotOptions();
setDraftDirty(false);
