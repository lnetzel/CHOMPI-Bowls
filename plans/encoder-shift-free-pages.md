# SHIFT-Free Encoder Pages

## Problem and goal

Some encoder turns currently depend on holding SHIFT. The master/volume
encoder uses SHIFT to select reverb decay instead of reverb mix, and
Encoder 1 uses SHIFT to make tuning adjustments finer. Other encoder turns
are disabled while SHIFT is held. This modifier makes the controls harder
to discover and use.

Make encoder turns behave the same whether SHIFT is held or not. Move every
alternate adjustment onto its own page on that encoder, preserving all
existing adjustments. Keep SHIFT's keyboard shortcuts and other non-encoder
behavior unchanged.

## Recommended approach

- Give the master/volume encoder explicit Volume, Reverb Mix, and Reverb
  Decay pages, selected by its existing press action.
- Give Encoder 1 separate Coarse Tune and Fine Tune pages, alongside its
  existing Bowl Volume and Pan pages. The dedicated Fine Tune page uses the
  current fine pitch step and feedback.
- Remove SHIFT checks that suppress turns on the master-volume, Encoder 1
  Bowl Volume, and Encoder 3 Character controls. Encoder turns should be
  dispatched by the active page alone.
- Keep keyboard SHIFT behavior, including mode/routing shortcuts and
  preset gestures. Decouple any fine-tune page feedback or short-lived
  keyboard handling from the old SHIFT-plus-turn gesture without changing
  those keyboard interactions.
- Update page labels/overlays, encoder LED indication, and the controls
  overview so each newly distinct page is discoverable.

## Affected files/components

- Encoder page state, turn dispatch, pitch feedback, and LED/overlay UI:
  [chompi_main.cpp](../code/src/chompi_main.cpp)
- Host test targets and focused encoder/page regression tests:
  [Makefile](../code/tests/Makefile) and a new or existing test under
  `code/tests/`
- User-facing encoder page legend:
  [chompi-controls-overview.svg](../web/images/chompi-controls-overview.svg)

## Step-by-step tasks

1. **Define the page model.**
   - Master/volume encoder: Volume -> Reverb Mix -> Reverb Decay.
   - Encoder 1: Bowl Volume -> Pan -> Coarse Tune -> Fine Tune.
   - Keep the existing page order for other encoders and make their turns
     independent of SHIFT.
2. **Implement page-only turn dispatch.**
   - Route the master/volume encoder's reverb decay adjustment from its new
     page instead of SHIFT.
   - Route Encoder 1's fine pitch increment from its new page instead of
     SHIFT; retain the coarse and fine pitch ranges and clamping.
   - Remove SHIFT-based turn suppression for master volume, Encoder 1 Bowl
     Volume, and Encoder 3 Character.
   - Preserve keyboard SHIFT shortcuts, preset actions, and routing
     behavior. Recheck the fine-tune feedback window so it remains scoped
     to the Fine Tune page rather than requiring SHIFT on the encoder.
3. **Update page feedback and documentation.**
   - Show the correct overlay and LED state when selecting or adjusting each
     page, including explicit fine-tune feedback.
   - Update the controls overview with the new page sequence and remove
     SHIFT-plus-turn instructions.
4. **Add focused regression tests.**
   - Test page cycling and that each alternate adjustment is reachable
     without SHIFT.
   - Test that holding SHIFT does not suppress any encoder turn or change
     which parameter the active page edits.
   - Verify coarse/fine pitch adjustments retain their respective step
     sizes and bounds, and that keyboard SHIFT gestures remain unaffected.
5. **Validate on target hardware.**
   - Build firmware and run the focused host tests.
   - Manually test page cycling, parameter changes, overlays/LEDs, SHIFT
     held and released for every encoder, and keyboard SHIFT shortcuts and
     preset gestures.

## Risks, open questions, memory/CPU/timing considerations

- More pages require additional press cycles, especially on Encoder 1;
  clear page feedback and a consistent order are important.
- Fine tuning currently shares state and feedback with the SHIFT path.
  Keep the reference pitch, fine-step behavior, overlay, and keyboard
  gesture timing consistent when the page becomes the mode selector.
- Tests should avoid adding hardware-dependent work to the audio callback.
  The page/turn logic is small and should add negligible CPU or memory use;
  measure firmware memory as part of the normal build.
- SHIFT remains meaningful for keyboard and preset/routing interactions.
  Ensure this change only removes its influence on encoder turns.

## Testing and validation

- Add or extend a host-side test for encoder page selection and
  SHIFT-independent dispatch; run its test target from
  `code/tests/Makefile`.
- Run the existing relevant host tests, especially
  `make -C code/tests modes-test` and `make -C code/tests midi-test`, to
  guard keyboard SHIFT and routing behavior.
- Build firmware and check SRAM, SRAM_EXEC, and SDRAM usage.
- On hardware, verify every page's adjustment and feedback with SHIFT
  both held and released, including fine-tune step size, end stops, and
  keyboard SHIFT shortcuts/preset actions.
