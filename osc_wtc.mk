OSCILLATOR = wtc
PROJECT = mo2_$(OSCILLATOR)

OSC_DDEFS = -DOSC_WTC

UCXXSRC = macro-oscillator2.cc \
	eurorack/plaits/dsp/engine/wavetable_engine.cc \
	eurorack/plaits/resources.cc \
	eurorack/stmlib/dsp/units.cc

# Add drumlogue wrapper for drumlogue platform
ifeq ($(PLATFORM),drumlogue)
    UCXXSRC += drumlogue_osc_adapter.cc drumlogue_unit_wrapper.cc
    OSC_DDEFS += -DOSC_NATIVE_BLOCK_SIZE=24
endif

include makefile.inc
