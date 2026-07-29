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

.PHONY: $(TOPTARGETS) $(OSCILLATORS) drumlogue test test-sound test-all test-elements test-rings test-clouds test-clouds-sample test-clouds-engine-opt test-clouds-synth test-clouds-fx test-clouds-fx-reconfig test-mussola bench

SDK_COMMON  := logue-sdk/platform/drumlogue/common

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
# granular_processor.cc comes from the eurorack-opt/ fork (reverb/diffuser
# early-out); everything else is the submodule.  -Ieurorack-opt must precede
# -Ieurorack so the forked headers shadow the submodule's -- see
# eurorack-opt/README.md.
CLOUDS_OPT_FLAGS = -Ieurorack-opt -Ieurorack -DCLOUDS_OPT_ENGINE
CLOUDS_FX_ENGINE = \
    eurorack-opt/clouds/dsp/granular_processor.cc \
    eurorack/clouds/dsp/correlator.cc \
    eurorack/clouds/dsp/mu_law.cc \
    eurorack/clouds/dsp/pvoc/phase_vocoder.cc \
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

# Run all tests
test-all: test test-elements test-rings test-clouds test-clouds-sample test-clouds-engine-opt test-clouds-synth test-clouds-fx test-clouds-fx-reconfig test-mussola test-sound

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

ARM_CC      ?= arm-linux-gnueabihf-gcc
ARM_SYSROOT ?= /usr/arm-linux-gnueabihf
QEMU_ARM    ?= qemu-arm
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
