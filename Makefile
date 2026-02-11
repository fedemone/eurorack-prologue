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

.PHONY: $(TOPTARGETS) $(OSCILLATORS) test

# Host-side unit tests (no ARM toolchain required)
test:
	g++ -std=c++11 -DOSC_NATIVE_BLOCK_SIZE=24 -Idrumlogue -I. -Wall -Wextra \
	    test_drumlogue_callbacks.cc drumlogue_osc_adapter.cc drumlogue_unit_wrapper.cc \
	    -o test_drumlogue_callbacks -lm
	./test_drumlogue_callbacks

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