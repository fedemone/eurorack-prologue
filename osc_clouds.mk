OSCILLATOR = clouds
PROJECT = $(OSCILLATOR)

OSC_DDEFS = -DCLOUDS_GRANULAR

UCXXSRC = clouds-granular.cc \
	eurorack/clouds/dsp/granular_processor.cc \
	eurorack/clouds/dsp/correlator.cc \
	eurorack/clouds/dsp/mu_law.cc \
	eurorack/clouds/dsp/pvoc/phase_vocoder.cc \
	eurorack/clouds/dsp/pvoc/frame_transformation.cc \
	eurorack/clouds/dsp/pvoc/stft.cc \
	eurorack/clouds/resources.cc \
	eurorack/stmlib/dsp/units.cc \
	eurorack/stmlib/dsp/atan.cc \
	eurorack/stmlib/utils/random.cc

# Add drumlogue wrapper for drumlogue platform
ifeq ($(PLATFORM),drumlogue)
    UCSRC = header.c
    UCXXSRC += drumlogue_osc_adapter.cc drumlogue_unit_wrapper.cc
    OSC_DDEFS += -DOSC_NATIVE_BLOCK_SIZE=32 -DBLOCKSIZE=32
endif

include makefile.inc
