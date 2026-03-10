OSCILLATOR = mussola
PROJECT = $(OSCILLATOR)

OSC_DDEFS = -DMUSSOLA_VOCAL

UCXXSRC = mussola.cc \
	eurorack/plaits/dsp/engine/speech_engine.cc \
	eurorack/plaits/dsp/speech/naive_speech_synth.cc \
	eurorack/plaits/dsp/speech/sam_speech_synth.cc \
	eurorack/plaits/dsp/speech/lpc_speech_synth.cc \
	eurorack/plaits/dsp/speech/lpc_speech_synth_controller.cc \
	eurorack/plaits/dsp/speech/lpc_speech_synth_phonemes.cc \
	eurorack/plaits/dsp/speech/lpc_speech_synth_words.cc \
	eurorack/plaits/resources.cc \
	eurorack/stmlib/dsp/units.cc

# Add drumlogue wrapper for drumlogue platform
ifeq ($(PLATFORM),drumlogue)
    UCSRC = header.c
    UCXXSRC += drumlogue_osc_adapter.cc drumlogue_unit_wrapper.cc
    OSC_DDEFS += -DOSC_NATIVE_BLOCK_SIZE=24
endif

include makefile.inc
