##############################################################################
# Project Configuration for drumlogue SDK build
#
# This config.mk follows the logue-sdk v2.0 convention.
# See: logue-sdk/platform/drumlogue/dummy-synth/config.mk
#
# For SDK builds: place this project under logue-sdk/platform/drumlogue/
# and use the SDK's Docker build system.
#
# For standalone builds: use the existing Makefile + makefile.inc.
#
# Per-oscillator OSCILLATOR/OSC_DDEFS are set by the osc_*.mk files.
# This file provides the common configuration.
##############################################################################

# Project name and type
PROJECT := mo2_$(OSCILLATOR)
PROJECT_TYPE := synth

##############################################################################
# Sources
#

EURORACKDIR = eurorack

# C sources (unit header)
CSRC = header.c

# C++ sources (common drumlogue wrapper + adapter)
CXXSRC  = drumlogue_unit_wrapper.cc
CXXSRC += drumlogue_osc_adapter.cc

# Per-oscillator sources are appended by osc_*.mk via UCXXSRC
CXXSRC += $(UCXXSRC)

# Assembly
ASMSRC =
ASMXSRC =

##############################################################################
# Include Paths
#

UINCDIR  = drumlogue
UINCDIR += .
UINCDIR += $(EURORACKDIR)

##############################################################################
# Library Paths
#

ULIBDIR =

##############################################################################
# Libraries
#

ULIBS  = -lm
ULIBS += -lc

##############################################################################
# Macros
#

UDEFS = $(OSC_DDEFS)
