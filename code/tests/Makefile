# Host-side DSP regression tests for the platform-independent BowlVoice
# header (plans/rimming_texture_direction_plan.md). NOT part of the
# firmware build. Run from the MSYS2 UCRT64 shell:
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

# Strike work (plans/improve_striking_character_plan.md): analyze the
# two reference recordings vs current-engine renders at matched pitches.
analyze: strike_analyze.exe
	./strike_analyze.exe compare

strike_analyze.exe: strike_analyze.cpp ../src/bowl_voice.h ../src/bowl_profile.h
	$(CXX) $(CXXFLAGS) -o $@ strike_analyze.cpp

# Strike regression suite (determinism, re-hits, steals, sweeps).
strike-test: strike_test.exe
	./strike_test.exe

strike_test.exe: strike_test.cpp ../src/bowl_voice.h ../src/bowl_profile.h ../src/bowl_scale.h
	$(CXX) $(CXXFLAGS) -o $@ strike_test.cpp

# Regenerate .baseline/baseline.raw from the PRE-CHANGE header snapshot
# in .baseline/. The snapshot was copied before the rim-texture change;
# do not refresh it from a post-change tree (that defeats T0).
baseline: baseline_gen.exe
	./baseline_gen.exe

# Play-modes feature tests (plans/improve-play-modes.md): the
# hardware-independent mode/key/latch state machine, the clean-pad
# envelope DSP, and the v6 preset schema (needs coreJSON, compiled as
# C99 like the firmware does).
modes-test: play_modes_test.exe pad_test.exe preset_test.exe
	./play_modes_test.exe
	./pad_test.exe
	./preset_test.exe

# Audio-routing feature tests: the send-FX loop key mapping and the
# per-sample output distribution (pure header, like play_modes.h).
routing-test: audio_routing_test.exe
	./audio_routing_test.exe

# SD record/playback pure helpers: stereo ring, PCM/WAV format, and take order.
sd-test: sd_audio_test.exe
	./sd_audio_test.exe

sd_audio_test.exe: sd_audio_test.cpp ../src/sd_audio.h
	$(CXX) $(CXXFLAGS) -o $@ sd_audio_test.cpp

# Stereo BBD delay DSP port: native timing, stereo, bypass, and bounds checks.
delay-test: stereo_bbd_delay_test.exe
	./stereo_bbd_delay_test.exe

stereo_bbd_delay_test.exe: stereo_bbd_delay_test.cpp ../src/StereoBbdDelay.h
	$(CXX) $(CXXFLAGS) -o $@ stereo_bbd_delay_test.cpp

audio_routing_test.exe: audio_routing_test.cpp ../src/audio_routing.h ../src/bowl_scale.h
	$(CXX) $(CXXFLAGS) -o $@ audio_routing_test.cpp

play_modes_test.exe: play_modes_test.cpp ../src/play_modes.h ../src/bowl_scale.h
	$(CXX) $(CXXFLAGS) -o $@ play_modes_test.cpp

pad_test.exe: pad_test.cpp ../src/bowl_voice.h ../src/bowl_profile.h ../src/bowl_scale.h
	$(CXX) $(CXXFLAGS) -o $@ pad_test.cpp

COREJSON_DIR = ../libs/coreJSON/source

preset_test.exe: preset_test.cpp ../src/BowlsPreset.h core_json_host.o
	$(CXX) $(CXXFLAGS) -I$(COREJSON_DIR)/include -o $@ preset_test.cpp core_json_host.o

core_json_host.o: $(COREJSON_DIR)/core_json.c
	gcc -O2 -std=c99 -I$(COREJSON_DIR)/include -c -o $@ $<

render_test.exe: render_test.cpp ../src/bowl_voice.h ../src/bowl_profile.h
	$(CXX) $(CXXFLAGS) -o $@ render_test.cpp

baseline_gen.exe: baseline_gen.cpp .baseline/bowl_voice.h .baseline/bowl_profile.h
	$(CXX) $(CXXFLAGS) -o $@ baseline_gen.cpp

clean:
	rm -f render_test.exe baseline_gen.exe strike_analyze.exe strike_test.exe play_modes_test.exe pad_test.exe preset_test.exe audio_routing_test.exe sd_audio_test.exe stereo_bbd_delay_test.exe core_json_host.o

.PHONY: test cal analyze baseline modes-test strike-test routing-test sd-test delay-test clean
