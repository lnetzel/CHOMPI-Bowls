# Post-Tap Audio Click Diagnostics

## Problem and goal

The same CHOMPI performance was captured through the SD recording tap and
through line out. After level normalization and approximate alignment, the
line-out capture has several one-sample transients in both channels that are
not present in the tap capture. The tap occurs before playback injection,
output routing, soft clipping, float-to-24-bit conversion, and the SAI/DMA
output buffers, so it does not rule out a fault later in that path.

Determine whether output-buffer deadlines or SAI/DMA handoff cause the clicks,
then correct the measured cause without changing the bowl sound or adding
unbounded work to the real-time audio path.

## Recommended approach

- Treat a late or missed output-buffer update as the leading hypothesis, not
  an established root cause. The current 24-frame block at 48 kHz gives a
  0.5 ms interval between half-buffer callbacks.
- Measure the complete low-level audio callback, including the firmware
  callback, float-to-24-bit conversion, and writes to both transmit buffers.
  The existing `CPU_METER` interval ends inside `AudioCallback`, before the
  libDaisy conversion and buffer writes.
- Keep diagnostics bounded and non-blocking in audio/DMA callbacks. Accumulate
  counters and maximum cycle counts there; report them from the main loop.
- Change audio behavior only after measurements distinguish callback
  overrun, SAI/DMA synchronization, or a discontinuity in the post-tap signal
  itself.

## Affected files and components

- Tap, playback injection, output routing, and soft clip:
  [AudioCallback](../code/src/chompi_main.cpp#L1969)
- Audio block size and dual-SAI setup:
  [ChompiHardware](../code/src/chompi_hardware.h#L240)
- Headphone/line-out signal distribution:
  [RouteOutputs](../code/src/audio_routing.h#L92)
- Per-voice DSP workload under polyphony:
  [BowlVoice::ProcessBlock](../code/src/bowl_voice.h#L479)
- Post-callback sample conversion and transmit-buffer writes:
  [libDaisy AudioHandle](../code/libs/libDaisy/src/hid/audio.cpp)
- SAI DMA half/full completion and buffer offsets:
  [libDaisy SaiHandle](../code/libs/libDaisy/src/per/sai.cpp)
- Existing host coverage:
  [audio_routing_test.cpp](../code/tests/audio_routing_test.cpp),
  [render_test.cpp](../code/tests/render_test.cpp),
  [strike_test.cpp](../code/tests/strike_test.cpp), and
  [test Makefile](../code/tests/Makefile)

## Step-by-step tasks

1. **Keep a reproducible baseline.**
   - Use the same firmware, CHOMPI, routing mode, performance, and timing for
     the tap and output captures.
   - Normalize for capture gain and align using shared musical content, then
     compare sample-level transient metrics in both channels. Preserve the
     current captures as the baseline.
   - Record the active routing and whether SD playback is running; playback
     is injected after the tap and would otherwise confound the comparison.

2. **Measure the full output-block deadline.**
   - Add a low-overhead cycle measurement around the complete libDaisy audio
     internal callback, ending only after conversion and both SAI transmit
     buffers have been written.
   - Compare the measured duration with the 24-frame/48-kHz half-buffer
     interval. Count deadline misses and retain a peak duration without
     printing from the callback.
   - Exercise the worst case: many simultaneous notes, including low notes,
     while monitoring both headphone and line outputs.
   - Compare this full-path result with the existing `CPU_METER` reading,
     which measures only the firmware `AudioCallback`.

3. **Localize any missed or corrupted block.**
   - If the full path exceeds its deadline, correlate the miss counter with
     the audible/transient events. Inspect callback cost and output conversion
     before considering a block-size or DSP change.
   - If the full path stays within budget, inspect the SAI1/SAI2 DMA callback
     ordering, buffer offsets, and transmit-buffer ownership for both output
     pairs.
   - If timing and DMA state are clean, compare the float samples immediately
     after the tap, after playback injection/routing, and before conversion to
     identify the first stage containing a discontinuity.

4. **Fix only the localized cause.**
   - For a confirmed deadline miss, reduce measured callback work or select a
     larger block only after accounting for latency and all fixed-size audio
     buffers.
   - For an SAI/DMA handoff defect, correct the synchronization or buffer
     offset logic without changing the synthesized signal.
   - Keep all audio-path diagnostics bounded; move formatted logging and
     reporting to the main loop.

5. **Validate the fix on host and hardware.**
   - Run the focused routing, render, and strike tests.
   - Build both the diagnostic and ordinary firmware. Do not rely on a
     diagnostic build alone: the current CPU-meter build changes USB behavior
     and prints from inside the audio callback.
   - Repeat the same-device dual-capture test under the worst-case note load.
     Require no matching output-only transients, no deadline misses, and no
     regression in the tap recording or perceived sound.

## Risks, open questions, and memory/CPU/timing considerations

- The existing CPU meter starts and ends inside `AudioCallback`; it excludes
  libDaisy's later float-to-24-bit conversion and transmit-buffer writes, so
  a low reading cannot by itself rule out an output deadline miss.
- In the `CPU_METER` build, the callback also prints periodically. That
  logging can perturb timing, so it must not be used as the sole proof of
  real-time headroom.
- Both output pairs share the firmware callback but use separate SAI/DMA
  buffers. A common callback overrun and a secondary-SAI handoff fault need
  different fixes; instrumentation must distinguish them.
- Host tests can verify routing and DSP behavior but cannot reproduce live SAI
  DMA deadlines. Hardware stress testing is required for acceptance.
- Increasing the block size changes latency and requires keeping the callback
  arrays and all related buffer sizes consistent. Do not use it as a blind
  workaround.
- The monitor-input recording level prevents inferring the firmware's
  pre-clip amplitude from WAV peak level alone. Any clipping diagnosis should
  use internal pre/post-`SoftClip` measurements.

## Testing and validation

- Run the existing host checks from the MSYS2 UCRT64 shell:
  `make -C code/tests routing-test test strike-test`.
- Add a host regression only if the fix changes a pure output/routing helper;
  host tests cannot establish the SAI/DMA deadline.
- Build and flash the diagnostic firmware, then stress-test simultaneous
  low-note voices while recording the tap and line output from the same unit.
- Repeat with the ordinary firmware and verify the click detector no longer
  finds the output-only one-sample transients in either channel.
