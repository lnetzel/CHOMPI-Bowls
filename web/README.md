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

Create a local draft from the factory settings or import a schema-v11 or
schema-v12 JSON preset. Older v11 imports are upgraded with empty metadata.
The name and comment fields are saved with the other preset values; names
are limited to 48 UTF-8 bytes and comments to 128 UTF-8 bytes. The editor
tries to connect automatically and requests MIDI/SysEx permission when
needed. If the browser requires a user gesture, select
**Connect** to grant permission. Use **Disconnect** to stop device
communication; select **Connect** to resume.
The 15-key slot bed shows saved, empty, pending, and incompatible slots.
It loads on connection and refreshes automatically while idle and after a
successful write. Selecting a saved slot loads its preset;
selecting an empty slot sets the destination for **Save to slot**. Select a tuning and
choose **Apply to all bowls** to replace the 25 pitch values in Strikes,
Rimming, and Split. This action never changes the firmware's embedded
factory preset. The global bowl controls apply pan, bowl volume, profile,
brightness, or decay to all 25 keys in all three modes; individual bowl
values and pitches remain editable afterward. **Strike** remains a
preset-wide setting. **Save to slot** is
the only action that writes to the SD card; overwriting a populated slot
requires confirmation. Export JSON to keep a local backup.

The connected instrument must run firmware with preset-transfer protocol
v1 and preset schema v12. The browser requires user approval before
granting MIDI/SysEx access;
after permission is granted, the editor finds CHOMPI-BOWLS and reconnects
when it appears. If the device has no SD card or is running older firmware,
the editor reports that before enabling preset transfer.

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
is limited to 4095 bytes. Firmware validates schema-v12 presets, preserves
metadata when saving hardware parameter changes, and appends `-copy` on each
firmware slot copy. Existing v11 files load with empty metadata. Writes use
the existing staged SD-card path before reporting success.
