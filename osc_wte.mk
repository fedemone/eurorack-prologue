OSCILLATOR = wte
PROJECT = mo2_$(OSCILLATOR)

OSC_DDEFS = -DOSC_WTE

UCXXSRC = macro-oscillator2.cc \
	eurorack/plaits/dsp/engine/wavetable_engine.cc \
	eurorack/plaits/resources.cc \
	eurorack/stmlib/dsp/units.cc

# Add drumlogue wrapper for drumlogue platform
ifeq ($(PLATFORM),drumlogue)
    UCXXSRC += drumlogue_osc_adapter.cc drumlogue_unit_wrapper.cc
endif

include makefile.inc
