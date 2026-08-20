INCLUDES = -Ilogue-sdk/platform/drumlogue/common \
		   -Ilogue-sdk/platform/drumlogue/eurorack/plaits \
		   -Ilogue-sdk/platform/drumlogue/eurorack/plaits/dsp \
		   -Ilogue-sdk/platform/drumlogue/eurorack/plaits/drivers \
		   -Ilogue-sdk/platform/drumlogue/eurorack/plaits/dsp/engine \
		   -Ilogue-sdk/platform/drumlogue/eurorack/plaits/fx \
		   -Ilogue-sdk/platform/drumlogue/eurorack/plaits/ui \
		   -Ilogue-sdk/platform/drumlogue/eurorack/plaits/pot_controller \
		   -Ilogue-sdk/platform/drumlogue/eurorack/plaits/pot_controller \
		   -Ilogue-sdk/platform/drumlogue/eurorack/plaits \
		   -Ilogue-sdk/platform/drumlogue/eurorack/plaits/dsp \
		   -Ilogue-sdk/platform/drumlogue/eurorack/plaits/drivers \
		   -Ilogue-sdk/platform/drumlogue/eurorack/plaits/dsp/engine \
		   -Ilogue-sdk/platform/drumlogue/eurorack/plaits/fx \
		   -Ilogue-sdk/platform/drumlogue/eurorack/plaits/ui
TOPTARGETS := all clean

OSCILLATORS := $(wildcard *mk)

VERSION=1.6-1

$(TOPTARGETS): $(OSCILLATORS) package_prologue package_minilogue-xd package_nutekt-digital package_drumlogue
$(OSCILLATORS):
	@rm -fR .dep ./build
	@PLATFORM=prologue VERSION=$(VERSION) $(MAKE) -f $@ $(MAKECMDGOALS)
	@rm -fR .dep ./build
	@PLATFORM=minilogue-xd VERSION=$(VERSION) $(MAKE) -f $@ $(MAKECMDGOALS)
	@rm -fR .dep ./build
	@PLATFORM=nts-1 VERSION=$(VERSION) $(MAKE) -f $@ $(MAKECMDGOALS)
	@rm -fR .dep ./build
	@PLATFORM=drumlogue VERSION=$(VERSION) $(MAKE) -f $@ $(MAKECMDGOALS)
	@rm -fR .dep ./build
	@PLATFORM=drumlogue VERSION=$(VERSION) $(MAKE) -f $@ $(MAKECMDGOALS)

drumlogue: $(OSCILLATORS) package_drumlogue
$(OSCILLATORS):
	@rm -fR .dep ./build
	@PLATFORM=drumlogue VERSION=$(VERSION) $(MAKE) -f $@ all

.PHONY: $(TOPTARGETS) $(OSCILLATORS) drumlogue test test-sound test-all test-elements test-rings test-clouds test-clouds-sample test-clouds-cola test-clouds-fft test-clouds-pvoc-rr test-clouds-engine-opt test-clouds-synth test-clouds-fx test-clouds-fx-reconfig test-mussola bench

SDK_COMMON  := logue-sdk/platform/drumlogue/common
ARM_CC      ?= arm-linux-gnueabihf-gcc
ARM_SYSROOT ?= /usr/arm-linux-gnueabihf
QEMU_ARM    ?= qemu-arm

CXX = g++
COMMON_TEST_FLAGS = -std=c++11 -Wall -Wextra -Idrumlogue -I.
COMMON_TEST_SRC = drumlogue_osc_adapter.cc drumlogue_unit_wrapper.cc header.c
# Host-side unit tests (no ARM toolchain required)
# Usage: make test [BLOCK_SIZE=24]
BLOCK_SIZE ?= 24
test:
	$(CXX) $(COMMON_TEST_FLAGS) -DOSC_NATIVE_BLOCK_SIZE=$(BLOCK_SIZE) \
	    test_drumlogue_callbacks.cc $(COMMON_TEST_SRC) \
	    -o test_drumlogue_callbacks -lm
	./test_drumlogue_callbacks

# Sound production test: links REAL Plaits VirtualAnalogEngine
# Verifies end-to-end audio production through the full wrapper chain
# Usage: make test-sound
test-sound:
	$(CXX) $(COMMON_TEST_FLAGS) -O2 -DTEST -DBLOCKSIZE=$(BLOCK_SIZE) -DOSC_VA \
	    -DOSC_NATIVE_BLOCK_SIZE=$(BLOCK_SIZE) -Ieurorack \
	    test_sound_production.cc $(COMMON_TEST_SRC) \
	    macro-oscillator2.cc \
	    eurorack/plaits/dsp/engine/virtual_analog_engine.cc \
	    eurorack/stmlib/dsp/units.cc \
	    -o test_sound_production -lm
	./test_sound_production


# Elements callback tests: same tests compiled with Elements defines
# Usage: make test-elements
test-elements:
	$(CXX) $(COMMON_TEST_FLAGS) -DOSC_NATIVE_BLOCK_SIZE=32 \
	    -DELEMENTS_RESONATOR_MODES=24 -DELEMENTS_LFO2 \
	    test_drumlogue_callbacks.cc $(COMMON_TEST_SRC) \
	    -o test_drumlogue_callbacks -lm
	./test_drumlogue_callbacks

# Rings callback tests: same tests compiled with Rings defines
# Usage: make test-rings
test-rings:
	$(CXX) $(COMMON_TEST_FLAGS) -DOSC_NATIVE_BLOCK_SIZE=$(BLOCK_SIZE) \
	    -DRINGS_RESONATOR \
	    test_drumlogue_callbacks.cc $(COMMON_TEST_SRC) \
	    -o test_drumlogue_callbacks_rings -lm
	./test_drumlogue_callbacks_rings

# Clouds callback tests: same tests compiled with Clouds defines
# Usage: make test-clouds
test-clouds:
	$(CXX) $(COMMON_TEST_FLAGS) -DOSC_NATIVE_BLOCK_SIZE=32 \
	    -DCLOUDS_GRANULAR \
	    test_drumlogue_callbacks.cc $(COMMON_TEST_SRC) \
	    -o test_drumlogue_callbacks -lm
	./test_drumlogue_callbacks

# Clouds sample playback logic tests (standalone, no SDK dependencies)
# Usage: make test-clouds-sample
test-clouds-sample:
	$(CXX) -std=c++11 -Wall -Wextra \
	    test_clouds_sample_playback.cc \
	    -o test_clouds_sample_playback -lm
	./test_clouds_sample_playback

# Mussola callback tests: same tests compiled with Mussola defines
# Usage: make test-mussola
test-mussola:
	$(CXX) $(COMMON_TEST_FLAGS) -DOSC_NATIVE_BLOCK_SIZE=24 \
	    -DMUSSOLA_VOCAL \
	    test_drumlogue_callbacks.cc $(COMMON_TEST_SRC) \
	    -o test_drumlogue_callbacks_mussola -lm
	./test_drumlogue_callbacks_mussola

# CloudsFX delfx test: links the REAL Clouds engine through the delfx wrapper
# and verifies FX-bus audio input reaches the engine (dry + wet paths).
# Usage: make test-clouds-fx
# granular_processor.cc (reverb/diffuser early-out) and phase_vocoder.cc (one
# channel per Buffer() call, spread across the hop) come from the
# eurorack-opt/ fork; everything else is the submodule.  -Ieurorack-opt must
# precede -Ieurorack so the forked headers shadow the submodule's -- see
# eurorack-opt/README.md.
CLOUDS_OPT_FLAGS = -Ieurorack-opt -Ieurorack -DCLOUDS_OPT_ENGINE
CLOUDS_FX_ENGINE = \
    eurorack-opt/clouds/dsp/granular_processor.cc \
    eurorack-opt/clouds/dsp/correlator.cc \
    eurorack/clouds/dsp/mu_law.cc \
    eurorack-opt/clouds/dsp/pvoc/phase_vocoder.cc \
    eurorack/clouds/dsp/pvoc/frame_transformation.cc \
    eurorack/clouds/dsp/pvoc/stft.cc \
    eurorack/clouds/resources.cc \
    eurorack/stmlib/dsp/units.cc \
    eurorack/stmlib/dsp/atan.cc \
    eurorack/stmlib/utils/random.cc
test-clouds-fx:
	$(CXX) $(COMMON_TEST_FLAGS) -O2 -DTEST -DCLOUDS_FX \
	    -DOSC_NATIVE_BLOCK_SIZE=32 -DBLOCKSIZE=32 $(CLOUDS_OPT_FLAGS) \
	    test_clouds_fx.cc drumlogue_delfx_wrapper.cc clouds-fx.cc header.c \
	    $(CLOUDS_FX_ENGINE) \
	    -o test_clouds_fx -lm
	./test_clouds_fx

# Clouds synth test: links the REAL Clouds engine behind the OSC_* callbacks
# and checks the 48 kHz <-> 32 kHz boundary keeps pitch, plus every
# mode x quality combination.
# Usage: make test-clouds-synth
test-clouds-synth:
	$(CXX) $(COMMON_TEST_FLAGS) -O2 -DTEST -DCLOUDS_GRANULAR \
	    -DOSC_NATIVE_BLOCK_SIZE=32 $(CLOUDS_OPT_FLAGS) -I$(SDK_COMMON) \
	    test_clouds_synth.cc clouds-granular.cc \
	    $(CLOUDS_FX_ENGINE) \
	    -o test_clouds_synth -lm
	./test_clouds_synth

# CloudsFX reconfiguration test: renders on one thread while Mode, Quality and
# unit_reset() are driven from another, which is how the drumlogue drives the
# unit and the only way to exercise the park handshake in clouds-fx.cc.
# Usage: make test-clouds-fx-reconfig
test-clouds-fx-reconfig:
	$(CXX) $(COMMON_TEST_FLAGS) -O2 -DTEST -DCLOUDS_FX -DCLOUDS_FX_TEST \
	    -DOSC_NATIVE_BLOCK_SIZE=32 -DBLOCKSIZE=32 $(CLOUDS_OPT_FLAGS) -pthread \
	    test_clouds_fx_reconfig.cc clouds-fx.cc \
	    $(CLOUDS_FX_ENGINE) \
	    -o test_clouds_fx_reconfig -lm
	./test_clouds_fx_reconfig

# Engine fork differential test: the same rendering compiled against the
# submodule and against eurorack-opt/, compared sample for sample.  Proves the
# reverb/diffuser early-out is inaudible where it claims to be, and pins the
# direction of the one case where the two legitimately differ.
# Usage: make test-clouds-engine-opt
CLOUDS_STOCK_ENGINE = $(patsubst eurorack-opt/%,eurorack/%,$(CLOUDS_FX_ENGINE))
test-clouds-engine-opt:
	$(CXX) $(COMMON_TEST_FLAGS) -O2 -DTEST -Ieurorack \
	    test_clouds_engine_opt.cc $(CLOUDS_STOCK_ENGINE) \
	    -o test_clouds_engine_opt_stock -lm
	$(CXX) $(COMMON_TEST_FLAGS) -O2 -DTEST $(CLOUDS_OPT_FLAGS) \
	    test_clouds_engine_opt.cc $(CLOUDS_FX_ENGINE) \
	    -o test_clouds_engine_opt_fork -lm
	@./test_clouds_engine_opt_stock > .engine_opt_stock.txt
	@./test_clouds_engine_opt_fork  > .engine_opt_fork.txt
	@echo "Clouds Engine Fork Differential Test"
	@echo ""
	@grep -E '^[AB] ' .engine_opt_stock.txt > .engine_opt_stock_ab.txt
	@grep -E '^[AB] ' .engine_opt_fork.txt  > .engine_opt_fork_ab.txt
	@if cmp -s .engine_opt_stock_ab.txt .engine_opt_fork_ab.txt; then \
	    echo "  ok:   A (reverb + diffuser active) bit-identical to upstream"; \
	    echo "  ok:   B (both amounts zero, skipped) bit-identical to upstream"; \
	 else \
	    echo "  FAIL: forked engine changed the output"; \
	    diff .engine_opt_stock_ab.txt .engine_opt_fork_ab.txt || true; \
	    rm -f .engine_opt_*.txt; exit 1; \
	 fi
	@sp=`awk '/^C /{print $$2}' .engine_opt_stock.txt`; \
	 fp=`awk '/^C /{print $$2}' .engine_opt_fork.txt`; \
	 awk -v s=$$sp -v f=$$fp 'BEGIN{ \
	    if (f <= s * 1.02) \
	      printf("  ok:   C (REVERB 0 -> full) fork peak %.0f <= upstream %.0f: no stale tail released\n", f, s); \
	    else { \
	      printf("  FAIL: C fork peak %.0f exceeds upstream %.0f -- flush is not emptying the delay lines\n", f, s); \
	      exit 1 } }'
	@rm -f .engine_opt_*.txt
	@echo ""
	@echo "=== ALL PASS (0 failures) ==="

# Grain window endpoint test: the envelope render compiled against the
# submodule and against eurorack-opt/, compared sample for sample, then the
# fork run again under AddressSanitizer.  The fork exists to stop a read one
# past lut_window, and claims to do it without moving a sample, so both halves
# get checked -- cmp for the window, ASan for the read.
# Usage: make test-clouds-grain-window
test-clouds-grain-window:
	$(CXX) $(COMMON_TEST_FLAGS) -O2 -DTEST -Ieurorack \
	    test_clouds_grain_window.cc eurorack/clouds/resources.cc \
	    -o test_clouds_grain_window_stock -lm
	$(CXX) $(COMMON_TEST_FLAGS) -O2 -DTEST -Ieurorack-opt -Ieurorack \
	    test_clouds_grain_window.cc eurorack/clouds/resources.cc \
	    -o test_clouds_grain_window_fork -lm
	@./test_clouds_grain_window_stock > .grain_window_stock.txt
	@./test_clouds_grain_window_fork  > .grain_window_fork.txt
	@echo "Clouds Grain Window Endpoint Test"
	@echo ""
	@if cmp -s .grain_window_stock.txt .grain_window_fork.txt; then \
	    echo "  ok:   envelope bit-identical to upstream, `wc -l < .grain_window_fork.txt` samples over 8 widths"; \
	 else \
	    echo "  FAIL: the fork changed the grain envelope"; \
	    diff .grain_window_stock.txt .grain_window_fork.txt | head -20; \
	    rm -f .grain_window_*.txt; exit 1; \
	 fi
	@rm -f .grain_window_*.txt
	@if $(CXX) $(COMMON_TEST_FLAGS) -O1 -g -DTEST -fsanitize=address \
	      -Ieurorack-opt -Ieurorack test_clouds_grain_window.cc \
	      eurorack/clouds/resources.cc -o test_clouds_grain_window_asan -lm \
	      >/dev/null 2>&1; then \
	    if ./test_clouds_grain_window_asan >/dev/null 2>.grain_window_asan.txt; then \
	      echo "  ok:   no read past lut_window (ASan clean)"; \
	    else \
	      echo "  FAIL: ASan flagged the forked envelope render"; \
	      head -12 .grain_window_asan.txt; \
	      rm -f .grain_window_asan.txt; exit 1; \
	    fi; \
	    rm -f .grain_window_asan.txt; \
	 else \
	    echo "  skip: no AddressSanitizer in $(CXX); endpoint read not checked"; \
	 fi
	@echo ""
	@echo "=== ALL PASS (0 failures) ==="

# Phase vocoder scheduling test: the round-robin Buffer() in eurorack-opt/
# against upstream's transform-every-channel loop, sample for sample.  Both
# sides are fork builds at the same FFT size, so the only variable is the
# scheduling.  Run at every supported FFT size, because the margin the split
# relies on is one hop and the hop scales with the size.
# Usage: make test-clouds-pvoc-rr
PVOC_RR_SIZES = 256 512 1024 2048 4096
test-clouds-pvoc-rr:
	@echo "Clouds Phase Vocoder Round-Robin Differential Test"
	@echo ""
	@fail=0; \
	 for n in $(PVOC_RR_SIZES); do \
	   for rr in 1 0; do \
	     $(CXX) $(COMMON_TEST_FLAGS) -O2 -DTEST $(CLOUDS_OPT_FLAGS) \
	         -DCLOUDS_FFT_SIZE=$$n -DCLOUDS_PVOC_ROUND_ROBIN=$$rr \
	         test_clouds_pvoc_rr.cc $(CLOUDS_FX_ENGINE) \
	         -o test_clouds_pvoc_rr_$$rr -lm || exit 1; \
	   done; \
	   ./test_clouds_pvoc_rr_1 > .pvoc_rr_on.txt; on=$$?; \
	   ./test_clouds_pvoc_rr_0 > .pvoc_rr_off.txt; off=$$?; \
	   if [ $$on -ne 0 ] || [ $$off -ne 0 ]; then \
	     echo "  FAIL: fft $$n: a scenario rendered silence"; \
	     grep '^FAIL' .pvoc_rr_on.txt .pvoc_rr_off.txt; fail=1; continue; \
	   fi; \
	   grep '^H ' .pvoc_rr_on.txt > .pvoc_rr_on_h.txt; \
	   grep '^H ' .pvoc_rr_off.txt > .pvoc_rr_off_h.txt; \
	   if cmp -s .pvoc_rr_on_h.txt .pvoc_rr_off_h.txt; then \
	     echo "  ok:   fft $$n: `grep -c '^H ' .pvoc_rr_on_h.txt` fixed-parameter scenarios bit-identical"; \
	   else \
	     echo "  FAIL: fft $$n: round-robin changed the output with parameters held still"; \
	     diff .pvoc_rr_on_h.txt .pvoc_rr_off_h.txt || true; fail=1; \
	   fi; \
	   paste .pvoc_rr_on.txt .pvoc_rr_off.txt | awk -v n=$$n ' \
	     /^S / { \
	       d = ($$4 - $$10) / ($$10 == 0 ? 1 : $$10); if (d < 0) d = -d; \
	       if (d <= 0.03) \
	         printf("  ok:   fft %s: %s rms within %.3f%%\n", n, $$2, 100*d); \
	       else { \
	         printf("  FAIL: fft %s: %s rms differs by %.3f%% -- more than a block of skew can explain\n", n, $$2, 100*d); \
	         exit 1 } }' || fail=1; \
	 done; \
	 rm -f .pvoc_rr_on.txt .pvoc_rr_off.txt .pvoc_rr_on_h.txt .pvoc_rr_off_h.txt; \
	 echo ""; \
	 if [ $$fail -ne 0 ]; then echo "=== FAILURES ==="; exit 1; fi; \
	 echo "=== ALL PASS (0 failures) ==="

# Overlap-add reconstruction test: drives the real STFT with the modifier
# disabled and measures the ripple of a steady tone, at hop ratio 4, 2 and 1.
# This is what backs CLOUDS_PVOC_HOP_RATIO -- halving the overlap halves most
# of Spectral's cost, and the reason that is safe is a COLA argument worth
# measuring rather than believing.  Ratio 1 is the control.
# Usage: make test-clouds-cola
test-clouds-cola:
	$(CXX) $(COMMON_TEST_FLAGS) -O2 -DTEST $(CLOUDS_OPT_FLAGS) \
	    test_clouds_cola.cc \
	    eurorack/clouds/dsp/pvoc/stft.cc \
	    eurorack/clouds/dsp/pvoc/frame_transformation.cc \
	    eurorack/clouds/resources.cc \
	    eurorack/stmlib/dsp/units.cc \
	    eurorack/stmlib/dsp/atan.cc \
	    eurorack/stmlib/utils/random.cc \
	    -o test_clouds_cola -lm
	./test_clouds_cola

# FFT tests: the interface contract (split layout, sign convention,
# unnormalised scaling) and the vectorised butterfly against upstream's scalar
# one.  Runs on the host, and under QEMU if the ARM toolchain is present --
# which is the run that matters, since the NEON path only exists there.
# Usage: make test-clouds-fft
test-clouds-fft:
	$(CXX) $(COMMON_TEST_FLAGS) -O2 $(CLOUDS_OPT_FLAGS) \
	    test_clouds_fft.cc -o test_clouds_fft -lm
	./test_clouds_fft
	@command -v $(ARM_CC) >/dev/null 2>&1 && command -v $(QEMU_ARM) >/dev/null 2>&1 || \
	    { echo "SKIP ARM/NEON run: cross toolchain or qemu-arm not found"; exit 0; }; \
	 echo "" && echo "--- same tests, ARM/NEON under QEMU ---" && \
	 arm-linux-gnueabihf-g++ -std=c++11 -Wall -Wextra -O2 -march=armv7-a \
	    -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -ffast-math \
	    -fsigned-char $(CLOUDS_OPT_FLAGS) -Idrumlogue -I. \
	    test_clouds_fft.cc -o test_clouds_fft_arm -lm && \
	 $(QEMU_ARM) -L $(ARM_SYSROOT) ./test_clouds_fft_arm

# Run all tests
test-all: test test-elements test-rings test-clouds test-clouds-sample test-clouds-cola test-clouds-fft test-clouds-pvoc-rr test-clouds-engine-opt test-clouds-grain-window test-clouds-synth test-clouds-fx test-clouds-fx-reconfig test-mussola test-sound test-param-routing

##############################################################################
# ARM unit tests: build the real .drmlgunit binaries and run them under QEMU
#
# The host suites above link the port layer into an x86 binary. This target
# tests the artifact that actually ships: ARM/NEON code, the drumlogue ABI,
# and — the reason it exists — several units loaded into one address space,
# which is what the device does with everything in Units/. See
# drumlogue/unit_exports.map.
#
# Requires: an armhf cross toolchain, qemu-arm, and the logue-sdk submodule
#   apt-get install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf qemu-user
#   git submodule update --init logue-sdk
#
# Usage: make test-arm [ARM_UNITS="clouds clouds_fx rings"]
##############################################################################

ARM_UNITS   ?= clouds clouds_fx mo2_va rings

# The SDK Makefile passes --param max-inline-insns-single=9999999999, which
# newer GCC rejects. Re-supply the same option set with a value it accepts;
# everything else matches the SDK defaults.
ARM_UNIT_OPT := -pipe -ffast-math -fsigned-char -fno-stack-protector \
                -fstrict-aliasing -falign-functions=16 -fno-math-errno \
                -fomit-frame-pointer -finline-limit=200000 \
                --param max-inline-insns-single=200000

.PHONY: test-arm
test-arm:
	@command -v $(ARM_CC) >/dev/null 2>&1 || \
	    { echo "SKIP test-arm: $(ARM_CC) not found"; exit 0; }
	@command -v $(QEMU_ARM) >/dev/null 2>&1 || \
	    { echo "SKIP test-arm: $(QEMU_ARM) not found"; exit 0; }
	@test -d $(SDK_COMMON) || \
	    { echo "SKIP test-arm: run 'git submodule update --init logue-sdk'"; exit 0; }
	@for u in $(ARM_UNITS); do \
	    echo "Building drumlogue/$$u ..."; \
	    $(MAKE) -C drumlogue/$$u CROSS_COMPILE=arm-linux-gnueabihf- \
	        USER_ID=0 GROUP_ID=0 \
	        USE_COPT="$(ARM_UNIT_OPT)" \
	        USE_CXXOPT="$(ARM_UNIT_OPT) -fno-threadsafe-statics" >/dev/null || exit 1; \
	done
	$(ARM_CC) -std=gnu11 -O1 -g -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard \
	    -I$(SDK_COMMON) test_drmlgunit.c -o test_drmlgunit_arm -ldl -lm -lpthread
	$(QEMU_ARM) -L $(ARM_SYSROOT) ./test_drmlgunit_arm \
	    $(foreach u,$(ARM_UNITS),drumlogue/$(u)/build/$(u).drmlgunit)

##############################################################################
# Sanitizer sweep: the same probe battery as test-arm, run against host builds
# of the units under AddressSanitizer and UndefinedBehaviorSanitizer.
#
# test-arm proves the shipped artifact runs. It cannot see a read one element
# past a lookup table, because that read lands in the next table and returns a
# plausible number. Every memory defect found in this repository so far is of
# that shape — Rings at Structure or Damping 100%, Elements at Geometry 100%,
# the Clouds grain envelope at the end of its window — and each was found by
# hand, by someone who already suspected it. This target is that search
# automated: no new test scenarios, just the existing sweeps with a sanitizer
# watching.
#
# Each unit's source list, defines and include paths already exist, in
# drumlogue/<unit>/config.mk. They are used here rather than restated, so a
# unit that gains a source file is covered without anyone remembering to come
# back. Those files all assign the same variable names, so only one can be
# included per make invocation -- hence the recursion through asan-unit.
#
# The version script is applied here too, so the cross-unit isolation check
# means what it means on the device: without it every unit exports its whole
# port layer and the first one loaded hijacks the rest.
#
# Requires a compiler with ASan/UBSan (any recent GCC or Clang). No ARM
# toolchain, no QEMU, no Docker.
#
# Usage: make test-asan [ASAN_UNITS="rings modal_strike"]
##############################################################################

ASAN_UNITS ?= mo2_va rings mussola modal_strike clouds clouds_fx
ASAN_DIR   := .asan

# -DTEST is stmlib's own switch for its portable fallbacks: without it,
# stmlib/dsp/dsp.h reaches for ARM inline assembly (ssat, usat, vsqrt.f32)
# that no host assembler will take. It selects sqrtf() and a compare-based
# clip, which is what the ARM instructions do anyway. Nothing in this
# repository keys on TEST, and the existing host suites already build this way.
# shift-base is switched off. It fires on things like `position << 4`, where
# position is a Q20.12 index and the shift is deliberately reinterpreting the
# bit pattern; the standard calls a signed left shift that overflows undefined,
# but every compiler this builds under wraps it, and Mutable Instruments' code
# uses the idiom throughout. Suppressing the whole check is the honest choice —
# forking headers to add casts would buy nothing and cost maintenance. The
# related shift-exponent check stays on, because a shift by the operand's width
# genuinely differs between targets: that is how the Stretch correlator's
# `>> 32` was found, and ARM and x86 answer it differently.
ASAN_FLAGS := -fsanitize=address,undefined -fsanitize-recover=address,undefined \
              -fno-sanitize=shift-base \
              -fno-omit-frame-pointer -g -O1 -DTEST

# Set by the recursive invocation below; brings in one unit's build variables.
# PROJROOT is overridden *after* the include because config.mk assigns it with
# `=`, so the source lists that reference it are still unexpanded here.
ifdef ASAN_UNIT
include drumlogue/$(ASAN_UNIT)/config.mk
PROJROOT := .
endif

.PHONY: test-asan asan-unit
test-asan:
	@echo "Sanitizer sweep (AddressSanitizer + UndefinedBehaviorSanitizer)"
	@echo ""
	@mkdir -p $(ASAN_DIR)
	@for u in $(ASAN_UNITS); do \
	    $(MAKE) --no-print-directory asan-unit ASAN_UNIT=$$u || exit 1; \
	 done
	@$(CC) -std=gnu11 $(ASAN_FLAGS) -I$(SDK_COMMON) \
	    test_drmlgunit.c -o $(ASAN_DIR)/test_drmlgunit -ldl -lm -lpthread
	@ASAN_OPTIONS=halt_on_error=0:detect_leaks=0 UBSAN_OPTIONS=halt_on_error=0 \
	    ./$(ASAN_DIR)/test_drmlgunit \
	    $(foreach u,$(ASAN_UNITS),$(ASAN_DIR)/$(u).so) \
	    > $(ASAN_DIR)/log.txt 2>&1; echo "  probe exit: $$?" > $(ASAN_DIR)/exit.txt
	@grep -E "^  (ok|FAIL) " $(ASAN_DIR)/log.txt | grep -c "^  ok" \
	    | xargs printf "  %s probe checks passed\n"
	@a=`grep -c "ERROR: AddressSanitizer" $(ASAN_DIR)/log.txt || true`; \
	 u=`grep -c "runtime error:" $(ASAN_DIR)/log.txt || true`; \
	 f=`grep -c "^  FAIL " $(ASAN_DIR)/log.txt || true`; \
	 if [ "$$a" = 0 ] && [ "$$u" = 0 ] && [ "$$f" = 0 ]; then \
	    echo "  no AddressSanitizer reports"; \
	    echo "  no UndefinedBehaviorSanitizer reports"; \
	    echo ""; echo "=== ALL PASS (0 failures) ==="; \
	 else \
	    echo "  AddressSanitizer reports: $$a"; \
	    echo "  UndefinedBehaviorSanitizer reports: $$u"; \
	    echo "  probe failures: $$f"; \
	    echo ""; \
	    grep -E "ERROR: AddressSanitizer|runtime error:|is located|^  FAIL " \
	        $(ASAN_DIR)/log.txt | head -40; \
	    echo ""; echo "  full log: $(ASAN_DIR)/log.txt"; \
	    exit 1; \
	 fi

# One unit, built as a host shared object with the sanitizers on.
# -I$(SDK_COMMON) leads, so the unit and the probe harness agree on the ABI
# structs; the repo keeps its own vendored copy of runtime.h for the
# prologue-family headers and the two are separate transcriptions.
asan-unit:
	@echo "  building $(ASAN_UNIT) ..."
	@mkdir -p $(ASAN_DIR)
	@$(CXX) -std=c++11 $(ASAN_FLAGS) -shared -fPIC \
	    $(UDEFS) -I$(SDK_COMMON) $(patsubst %,-I%,$(UINCDIR)) \
	    $(CSRC) $(CXXSRC) -o $(ASAN_DIR)/$(ASAN_UNIT).so -lm \
	    -Wl,--version-script=drumlogue/unit_exports.map

# Benchmark: per-render cost distribution for the shipped .drmlgunit binaries,
# measured through the drumlogue ABI at the buffer size the firmware asks for.
# bench-clouds-spike benches the Clouds engine; this benches whole units, which
# is the only scale on which two different units can be compared.
# Usage: make bench-units [ARM_UNITS="rings mussola"] [BENCH_FRAMES=64]
BENCH_FRAMES  ?= 64
BENCH_RENDERS ?= 3000

.PHONY: bench-units
bench-units:
	@command -v $(ARM_CC) >/dev/null 2>&1 && command -v $(QEMU_ARM) >/dev/null 2>&1 || \
	    { echo "SKIP bench-units: need $(ARM_CC) and $(QEMU_ARM)"; exit 0; }
	@test -d $(SDK_COMMON) || \
	    { echo "SKIP bench-units: run 'git submodule update --init logue-sdk'"; exit 0; }
	@for u in $(ARM_UNITS); do \
	    $(MAKE) -C drumlogue/$$u CROSS_COMPILE=arm-linux-gnueabihf- \
	        USER_ID=0 GROUP_ID=0 \
	        USE_COPT="$(ARM_UNIT_OPT)" \
	        USE_CXXOPT="$(ARM_UNIT_OPT) -fno-threadsafe-statics" >/dev/null || exit 1; \
	done
	$(ARM_CC) -std=gnu11 -O2 -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard \
	    -I$(SDK_COMMON) bench_units.c -o bench_units_arm -ldl -lm
	$(QEMU_ARM) -L $(ARM_SYSROOT) ./bench_units_arm \
	    -f $(BENCH_FRAMES) -n $(BENCH_RENDERS) \
	    $(foreach u,$(ARM_UNITS),drumlogue/$(u)/build/$(u).drmlgunit)

# Benchmark: measure host-side render throughput for VirtualAnalog engine
# Reports frames/sec, us/frame, and real-time ratio
# Usage: make bench
bench:
	$(CXX) $(COMMON_TEST_FLAGS) -O2 -DTEST -DBLOCKSIZE=$(BLOCK_SIZE) -DOSC_VA \
	    -DOSC_NATIVE_BLOCK_SIZE=$(BLOCK_SIZE) -Ieurorack \
	    bench_render.cc $(COMMON_TEST_SRC) \
	    macro-oscillator2.cc \
	    eurorack/plaits/dsp/engine/virtual_analog_engine.cc \
	    eurorack/stmlib/dsp/units.cc \
	    -o bench_render -lm
	./bench_render

# Benchmark: where Spectral's cost sits within a block, which is what decides
# whether the unit crackles.  Reports mean, worst block and the fraction of
# blocks over the 1 ms deadline, per mode.  ARM under QEMU, because the shape
# of the answer is what matters and QEMU preserves it.
# Usage: make bench-clouds-spike
bench-clouds-spike:
	@command -v $(ARM_CC) >/dev/null 2>&1 && command -v $(QEMU_ARM) >/dev/null 2>&1 || \
	    { echo "SKIP bench-clouds-spike: need $(ARM_CC) and $(QEMU_ARM)"; exit 0; }
	arm-linux-gnueabihf-g++ -std=c++11 -O2 -march=armv7-a -mtune=cortex-a7 \
	    -mfpu=neon-vfpv4 -mfloat-abi=hard -ffast-math -fsigned-char -DTEST \
	    $(CLOUDS_OPT_FLAGS) -Idrumlogue -I. \
	    bench_clouds_spike.cc $(CLOUDS_FX_ENGINE) \
	    -o bench_clouds_spike_arm -lm
	$(QEMU_ARM) -L $(ARM_SYSROOT) ./bench_clouds_spike_arm

PROLOGUE_PACKAGE=eurorack_prologue
MINILOGUE_XD_PACKAGE=eurorack_minilogue-xd
NUTEKT_DIGITAL_PACKAGE=eurorack_nutekt-digital
DRUMLOGUE_PACKAGE=eurorack_drumlogue

package_prologue:
	@echo Packaging to ./${PROLOGUE_PACKAGE}.zip
	@rm -f ${PROLOGUE_PACKAGE}.zip
	@rm -rf ${PROLOGUE_PACKAGE}
	@mkdir ${PROLOGUE_PACKAGE}
	@cp -a *.prlgunit ${PROLOGUE_PACKAGE}/
	@cp -a credits.txt ${PROLOGUE_PACKAGE}/
	@zip -rq9m ${PROLOGUE_PACKAGE}.zip ${PROLOGUE_PACKAGE}/

package_minilogue-xd:
	@echo Packaging to ./${MINILOGUE_XD_PACKAGE}.zip
	@rm -f ${MINILOGUE_XD_PACKAGE}.zip
	@rm -rf ${MINILOGUE_XD_PACKAGE}
	@mkdir ${MINILOGUE_XD_PACKAGE}
	@cp -a *.mnlgxdunit ${MINILOGUE_XD_PACKAGE}/
	@cp -a credits.txt ${MINILOGUE_XD_PACKAGE}/
	@zip -rq9m ${MINILOGUE_XD_PACKAGE}.zip ${MINILOGUE_XD_PACKAGE}/

package_nutekt-digital:
	@echo Packaging to ./${NUTEKT_DIGITAL_PACKAGE}.zip
	@rm -f ${NUTEKT_DIGITAL_PACKAGE}.zip
	@rm -rf ${NUTEKT_DIGITAL_PACKAGE}
	@mkdir ${NUTEKT_DIGITAL_PACKAGE}
	@cp -a *.ntkdigunit ${NUTEKT_DIGITAL_PACKAGE}/
	@cp -a credits.txt ${NUTEKT_DIGITAL_PACKAGE}/
	@zip -rq9m ${NUTEKT_DIGITAL_PACKAGE}.zip ${NUTEKT_DIGITAL_PACKAGE}/

package_drumlogue:
	@echo Packaging to ./${DRUMLOGUE_PACKAGE}.zip
	@rm -f ${DRUMLOGUE_PACKAGE}.zip
	@rm -rf ${DRUMLOGUE_PACKAGE}
	@mkdir ${DRUMLOGUE_PACKAGE}
	@cp -a *.drmlgunit ${DRUMLOGUE_PACKAGE}/
	@cp -a credits.txt ${DRUMLOGUE_PACKAGE}/
	@zip -rq9m ${DRUMLOGUE_PACKAGE}.zip ${DRUMLOGUE_PACKAGE}/

##############################################################################
# Parameter routing: where every panel knob actually goes.
#
# The panel layout is one table per unit (drumlogue_param_table.h). A slip in
# that table does not crash and does not fail a range check -- it sends a knob
# to the wrong engine parameter and waits to be noticed by ear. So the routing
# is captured, not argued: this dumps id -> destination for every parameter of
# every unit and diffs it against docs/param_routing.txt, which was generated
# from the code as it stood before the table existed.
#
# A diff here means the panel layout changed. That is sometimes correct --
# regenerate with `make param-routing-golden` and let the diff be the review.
#
# Usage: make test-param-routing
##############################################################################

ROUTING_UNITS = OSC_VA OSC_STRING ELEMENTS_RESONATOR_MODES=24 RINGS_RESONATOR \
                CLOUDS_GRANULAR MUSSOLA_VOCAL

define ROUTING_BUILD
	@$(CXX) $(COMMON_TEST_FLAGS) -O1 -I$(SDK_COMMON) \
	    -DOSC_NATIVE_BLOCK_SIZE=24 -D$(1) \
	    test_param_routing_dump.cc drumlogue_osc_adapter.cc \
	    drumlogue_unit_wrapper.cc header.c -o .param_routing_bin -lm
	@./.param_routing_bin
endef

.PHONY: test-param-routing param-routing-golden
param-routing-golden:
	@mkdir -p docs
	@: > docs/param_routing.txt
	@for u in $(ROUTING_UNITS); do \
	    $(MAKE) --no-print-directory .param-routing-one ROUTING_UNIT=$$u \
	        >> docs/param_routing.txt || exit 1; \
	 done
	@rm -f .param_routing_bin
	@echo "wrote docs/param_routing.txt (`wc -l < docs/param_routing.txt` lines)"

test-param-routing:
	@echo "Parameter Routing"
	@echo ""
	@: > .param_routing_now.txt
	@for u in $(ROUTING_UNITS); do \
	    $(MAKE) --no-print-directory .param-routing-one ROUTING_UNIT=$$u \
	        >> .param_routing_now.txt || exit 1; \
	 done
	@rm -f .param_routing_bin
	@if diff -u docs/param_routing.txt .param_routing_now.txt > .param_routing_diff.txt; then \
	    echo "  ok:   `grep -c 'osc\[' .param_routing_now.txt` forwarded mappings unchanged across `grep -c '^#' .param_routing_now.txt` units"; \
	    rm -f .param_routing_now.txt .param_routing_diff.txt; \
	    echo ""; echo "=== ALL PASS (0 failures) ==="; \
	 else \
	    echo "  FAIL: the panel layout moved"; \
	    head -40 .param_routing_diff.txt; \
	    rm -f .param_routing_now.txt .param_routing_diff.txt; \
	    exit 1; \
	 fi

.param-routing-one:
	$(call ROUTING_BUILD,$(ROUTING_UNIT))
