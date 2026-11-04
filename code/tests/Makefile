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

render_test.exe: render_test.cpp ../src/bowl_voice.h ../src/bowl_profile.h
	$(CXX) $(CXXFLAGS) -o $@ render_test.cpp

baseline_gen.exe: baseline_gen.cpp .baseline/bowl_voice.h .baseline/bowl_profile.h
	$(CXX) $(CXXFLAGS) -o $@ baseline_gen.cpp

clean:
	rm -f render_test.exe baseline_gen.exe strike_analyze.exe

.PHONY: test cal analyze baseline clean
