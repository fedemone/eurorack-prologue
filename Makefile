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

.PHONY: $(TOPTARGETS) $(OSCILLATORS) test test-sound test-all

# Host-side unit tests (no ARM toolchain required)
# Usage: make test [BLOCK_SIZE=24]
BLOCK_SIZE ?= 24
test:
	g++ -std=c++11 -DOSC_NATIVE_BLOCK_SIZE=$(BLOCK_SIZE) -Idrumlogue -I. -Wall -Wextra \
	    test_drumlogue_callbacks.cc drumlogue_osc_adapter.cc drumlogue_unit_wrapper.cc \
	    header.c -o test_drumlogue_callbacks -lm
	./test_drumlogue_callbacks

# Sound production test: links REAL Plaits VirtualAnalogEngine
# Verifies end-to-end audio production through the full wrapper chain
# Usage: make test-sound
test-sound:
	g++ -std=c++11 -O2 -DTEST -DBLOCKSIZE=$(BLOCK_SIZE) -DOSC_VA \
	    -DOSC_NATIVE_BLOCK_SIZE=$(BLOCK_SIZE) -Idrumlogue -I. -Ieurorack -Wall -Wextra \
	    test_sound_production.cc drumlogue_osc_adapter.cc drumlogue_unit_wrapper.cc \
	    header.c macro-oscillator2.cc \
	    eurorack/plaits/dsp/engine/virtual_analog_engine.cc \
	    eurorack/stmlib/dsp/units.cc \
	    -o test_sound_production -lm
	./test_sound_production

# Run all tests
test-all: test test-sound

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
