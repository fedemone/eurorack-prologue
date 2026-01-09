OSCILLATOR = string
PROJECT = mo2_$(OSCILLATOR)

OSC_DDEFS = -DOSC_STRING

UCXXSRC = macro-oscillator2.cc \
	eurorack/plaits/dsp/engine/string_engine.cc \
	eurorack/plaits/dsp/physical_modelling/string_voice.cc \
	eurorack/plaits/dsp/physical_modelling/string.cc \
	eurorack/plaits/resources.cc \
	eurorack/stmlib/dsp/units.cc \
	eurorack/stmlib/utils/random.cc

# Add drumlogue wrapper for drumlogue platform
ifeq ($(PLATFORM),drumlogue)
    UCXXSRC += drumlogue_osc_adapter.cc drumlogue_unit_wrapper.cc
endif

include makefile.inc
