OSCILLATOR = mussola
PROJECT = $(OSCILLATOR)

OSC_DDEFS = -DMUSSOLA_VOCAL

MUSSOLA_ENGINE_SOURCES := $(shell cat osc_mussola.sources)
UCXXSRC = mussola.cc $(MUSSOLA_ENGINE_SOURCES)

# Add drumlogue wrapper for drumlogue platform
ifeq ($(PLATFORM),drumlogue)
    UCSRC = header.c
    UCXXSRC += drumlogue_osc_adapter.cc drumlogue_unit_wrapper.cc
    OSC_DDEFS += -DOSC_NATIVE_BLOCK_SIZE=24
endif

include makefile.inc
