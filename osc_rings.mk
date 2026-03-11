OSCILLATOR = rings
PROJECT = $(OSCILLATOR)

OSC_DDEFS = -DRINGS_RESONATOR

UCXXSRC = rings-resonator.cc \
	eurorack/rings/dsp/part.cc \
	eurorack/rings/dsp/resonator.cc \
	eurorack/rings/dsp/string.cc \
	eurorack/rings/dsp/fm_voice.cc \
	eurorack/rings/dsp/string_synth_part.cc \
	eurorack/rings/resources.cc \
	eurorack/stmlib/dsp/units.cc \
	eurorack/stmlib/utils/random.cc

# Add drumlogue wrapper for drumlogue platform
ifeq ($(PLATFORM),drumlogue)
    UCSRC = header.c
    UCXXSRC += drumlogue_osc_adapter.cc drumlogue_unit_wrapper.cc
    OSC_DDEFS += -DOSC_NATIVE_BLOCK_SIZE=24
endif

include makefile.inc
