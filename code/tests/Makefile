# Host-side DSP tests for BowlVoice. Run from the MSYS2 UCRT64 shell:
#   make -C code/tests test
# The UCRT64 bin dir must be on PATH (export PATH=/c/msys64/ucrt64/bin:$PATH).

CXX      ?= g++
CXXFLAGS ?= -O2 -std=c++14 -Wall -Wextra -I../src

test: render_test.exe
	./render_test.exe

# Print calibration numbers for kCleanDrive (bowl_voice.h) without
# running the assertions.
cal: render_test.exe
	./render_test.exe cal

# Compare current-engine renders with the two reference recordings.
analyze: strike_analyze.exe
	./strike_analyze.exe compare

strike_analyze.exe: strike_analyze.cpp ../src/bowl_voice.h ../src/bowl_profile.h
	$(CXX) $(CXXFLAGS) -o $@ strike_analyze.cpp

# Strike regression suite (determinism, re-hits, steals, sweeps).
strike-test: strike_test.exe
	./strike_test.exe

strike_test.exe: strike_test.cpp ../src/bowl_voice.h ../src/bowl_profile.h ../src/bowl_scale.h ../src/control_mappings.h
	$(CXX) $(CXXFLAGS) -o $@ strike_test.cpp

# Regenerate the stored reference render.
baseline: baseline_gen.exe
	./baseline_gen.exe

# Play-mode, clean-pad envelope and preset schema tests.
modes-test: play_modes_test.exe pad_test.exe preset_test.exe
	./play_modes_test.exe
	./pad_test.exe
	./preset_test.exe

# Audio-routing feature tests: the send-FX loop key mapping and the
# per-sample output distribution (pure header, like play_modes.h).
routing-test: audio_routing_test.exe
	./audio_routing_test.exe

# MIDI input feature tests (plans/midi-input-and-cc-control.md): the
# note/CC map and conversions, the fixed event queues + fair bounded
# dispatch, and the source-aware claim/ownership state machine. Pure
# headers (BOWLS_MIDI_HOST_TEST stops MidiHandler.h before the Daisy
# transport), like play_modes.h.
midi-test: midi_test.exe
	./midi_test.exe

midi-transfer-test: preset_transfer_test.exe
	./preset_transfer_test.exe

preset_transfer_test.exe: preset_transfer_test.cpp ../src/PresetTransferProtocol.h
	$(CXX) $(CXXFLAGS) -o $@ preset_transfer_test.cpp

midi_test.exe: midi_test.cpp ../src/MidiCcMap.h ../src/MidiHandler.h ../src/play_modes.h ../src/bowl_scale.h ../src/control_mappings.h ../src/delay_parameters.h ../src/midi_clock.h
	$(CXX) $(CXXFLAGS) -o $@ midi_test.cpp

# SD record/playback pure helpers: stereo ring, PCM/WAV format, and take order.
sd-test: sd_audio_test.exe
	./sd_audio_test.exe

sd_audio_test.exe: sd_audio_test.cpp ../src/sd_audio.h
	$(CXX) $(CXXFLAGS) -o $@ sd_audio_test.cpp

# Stereo BBD delay DSP port: native timing, stereo, bypass, and bounds checks.
delay-test: stereo_bbd_delay_test.exe
	./stereo_bbd_delay_test.exe

stereo_bbd_delay_test.exe: stereo_bbd_delay_test.cpp ../src/StereoBbdDelay.h ../src/delay_parameters.h
	$(CXX) $(CXXFLAGS) -o $@ stereo_bbd_delay_test.cpp

audio_routing_test.exe: audio_routing_test.cpp ../src/audio_routing.h ../src/bowl_scale.h
	$(CXX) $(CXXFLAGS) -o $@ audio_routing_test.cpp

play_modes_test.exe: play_modes_test.cpp ../src/play_modes.h ../src/bowl_scale.h ../src/voice_allocator.h ../src/control_mappings.h
	$(CXX) $(CXXFLAGS) -o $@ play_modes_test.cpp

pad_test.exe: pad_test.cpp ../src/bowl_voice.h ../src/bowl_profile.h ../src/bowl_scale.h
	$(CXX) $(CXXFLAGS) -o $@ pad_test.cpp

COREJSON_DIR = ../libs/coreJSON/source

preset_test.exe: preset_test.cpp ../src/BowlsPreset.h ../src/bowl_scale.h ../state\ examples/default_slendro_inspired.json core_json_host.o
	$(CXX) $(CXXFLAGS) -I$(COREJSON_DIR)/include -o $@ preset_test.cpp core_json_host.o

core_json_host.o: $(COREJSON_DIR)/core_json.c
	gcc -O2 -std=c99 -I$(COREJSON_DIR)/include -c -o $@ $<

render_test.exe: render_test.cpp ../src/bowl_voice.h ../src/bowl_profile.h
	$(CXX) $(CXXFLAGS) -o $@ render_test.cpp

baseline_gen.exe: baseline_gen.cpp .baseline/bowl_voice.h .baseline/bowl_profile.h
	$(CXX) $(CXXFLAGS) -o $@ baseline_gen.cpp

clean:
	rm -f render_test.exe baseline_gen.exe strike_analyze.exe strike_test.exe play_modes_test.exe pad_test.exe preset_test.exe audio_routing_test.exe sd_audio_test.exe stereo_bbd_delay_test.exe midi_test.exe preset_transfer_test.exe core_json_host.o

.PHONY: test cal analyze baseline modes-test strike-test routing-test sd-test delay-test midi-test midi-transfer-test clean
