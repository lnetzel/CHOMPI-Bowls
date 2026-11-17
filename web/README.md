# CHOMPI-BOWLS Preset Editor

The static editor reads and writes the 15 SD-card preset slots over USB
MIDI SysEx. The embedded factory preset is not exposed or changed by the
editor. Tuning choices are optional and affect only the current browser
draft until you explicitly save it to a slot.

## Local use

From the repository root, start a static server:

```powershell
python -m http.server 8000 --directory web
```

Open `http://localhost:8000/` in desktop Chrome or Edge. Localhost is a
secure context for Web MIDI. Direct MIDI access requires a browser that
supports Web MIDI and the SysEx permission prompt.

Create a local draft from the factory settings, import a schema-v11 JSON
preset, or connect to a device and read a saved slot. Select a tuning and
choose **Apply to all modes** to replace the 25 pitch values in Strikes,
Rimming, and Split. This action never changes the firmware's embedded
factory preset. Individual pitches remain editable. **Save to slot** is
the only action that writes to the SD card; overwriting a populated slot
requires confirmation. Export JSON to keep a local backup.

The connected instrument must run firmware with preset-transfer protocol
v1. Connect the CHOMPI-BOWLS USB MIDI input and output, then choose
**Use selected device**. If the device has no SD card or is running older
firmware, the editor reports that before enabling preset transfer.

## GitHub Pages deployment

The workflow in `.github/workflows/deploy-pages.yml` publishes the
contents of `web/` at the repository site root:

`https://lnetzel.github.io/CHOMPI-Bowls/`

In the repository settings, open **Pages** and set the build and
deployment source to **GitHub Actions**. Push a change to `main` or run
**Deploy Pages** from the Actions tab. The workflow uses GitHub's
`GITHUB_TOKEN`; no API keys, personal tokens, or other secrets are
required.

## Checks

Run the browser-side model and protocol checks with:

```powershell
node web\tests\smoke.js
```

Run the firmware protocol framing test from an MSYS2 UCRT64 shell:

```sh
make -C code/tests midi-transfer-test
```

The SysEx protocol uses manufacturer ID `0x7D`, protocol version 1,
7-bit-safe payloads, CRC-16 checksums, and 96-byte data chunks. Slot JSON
is limited to 4095 bytes. Firmware validates a complete v11 preset and
uses the existing staged SD-card write path before reporting success.
