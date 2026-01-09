# makefile.inc - Build configuration for eurorack-prologue
# Supports multiple platforms: prologue, minilogue-xd, nts-1, drumlogue

# Detect OS
ifeq ($(OS),Windows_NT)
ifeq ($(MSYSTEM), MSYS)
    detected_OS := $(shell uname -s)
else
    detected_OS := Windows
endif
else
    detected_OS := $(shell uname -s)
endif

# Platform detection and configuration
ifeq ($(PLATFORM),prologue)
    PLATFORM_NAME = prologue
    PLATFORM_FLAGS = -DPLATFORM_PROLOGUE
    PKGEXT = prlgunit
    SDK_PLATFORM_DIR = logue-sdk/platform/prologue/osc
else ifeq ($(PLATFORM),minilogue-xd)
    PLATFORM_NAME = minilogue-xd
    PLATFORM_FLAGS = -DPLATFORM_MINILOGUEXD
    PKGEXT = mnlgxdunit
    SDK_PLATFORM_DIR = logue-sdk/platform/minilogue-xd/osc
else ifeq ($(PLATFORM),nts-1)
    PLATFORM_NAME = nts-1
    PLATFORM_FLAGS = -DPLATFORM_NTS1
    PKGEXT = ntkdigunit
    SDK_PLATFORM_DIR = logue-sdk/platform/nutekt-digital/osc
else ifeq ($(PLATFORM),drumlogue)
    PLATFORM_NAME = drumlogue
    PLATFORM_FLAGS = -DPLATFORM_DRUMLOGUE
    PKGEXT = drmlgunit
    SDK_PLATFORM_DIR =
else
    $(error PLATFORM not specified. Set PLATFORM=prologue, minilogue-xd, nts-1, or drumlogue)
endif

# Set directories
PLATFORMDIR = .
PROJECTDIR = .
TOOLSDIR = $(PLATFORMDIR)/logue-sdk/tools
EXTDIR = $(PLATFORMDIR)/logue-sdk/platform/ext
CMSISDIR = $(EXTDIR)/CMSIS/CMSIS

# Common toolchain settings
ifeq ($(PLATFORM),drumlogue)
    # Drumlogue uses ARM Cortex-A7 architecture
    PREFIX = arm-linux-gnueabihf-
    CC = $(PREFIX)gcc
    CXX = $(PREFIX)g++
    LD = $(PREFIX)g++  # Use g++ for C++ linking
    AR = $(PREFIX)ar
    AS = $(PREFIX)as
    OBJCOPY = $(PREFIX)objcopy
    OBJDUMP = $(PREFIX)objdump
    SIZE = $(PREFIX)size
    
    # ARM Cortex-A7 specific flags
    MCU = cortex-a7
    MCUFLAGS = -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard
    ARCH_FLAGS = -march=armv7ve+simd
    
    # NEON vectorization support
    NEON_FLAGS = -mfpu=neon-vfpv4 -ftree-vectorize -ffast-math
    
    # Shared library compilation flags
    FPIC = -fPIC
    SHARED_FLAGS = -shared
    
    # Platform-specific optimization
    OPT_FLAGS = -O3 -funroll-loops -fomit-frame-pointer
    
else
    # Other platforms use ARM Cortex-M4
    GCC_TARGET = arm-none-eabi-
    
    # Allow override of GCC path via environment variable
    ifeq ($(GCC_BIN_PATH),)
        GCC_BIN_PATH = $(TOOLSDIR)/gcc/gcc-arm-none-eabi-5_4-2016q3/bin
    endif
    
    CC = $(GCC_BIN_PATH)/$(GCC_TARGET)gcc
    CXX = $(GCC_BIN_PATH)/$(GCC_TARGET)g++
    LD = $(GCC_BIN_PATH)/$(GCC_TARGET)gcc  # Use gcc as per logue-SDK convention
    AR = $(GCC_BIN_PATH)/$(GCC_TARGET)ar
    AS = $(GCC_BIN_PATH)/$(GCC_TARGET)gcc -x assembler-with-cpp
    OBJCOPY = $(GCC_BIN_PATH)/$(GCC_TARGET)objcopy
    OBJDUMP = $(GCC_BIN_PATH)/$(GCC_TARGET)objdump
    SIZE = $(GCC_BIN_PATH)/$(GCC_TARGET)size
    
    # ARM Cortex-M4 specific flags
    MCU = cortex-m4
    MCUFLAGS = -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
    ARCH_FLAGS =
    
    FPIC =
    SHARED_FLAGS =
    OPT_FLAGS = -O2 -flto
    NEON_FLAGS =
endif

# Common includes
UINCDIR = -I. \
          -Ieurorack

# Add CMSIS includes for non-drumlogue platforms
ifneq ($(PLATFORM),drumlogue)
    UINCDIR += -I$(SDK_PLATFORM_DIR)/inc \
               -I$(SDK_PLATFORM_DIR)/inc/api \
               -I$(SDK_PLATFORM_DIR)/inc/utils \
               -I$(CMSISDIR)/Include
endif

# Common compiler flags
CFLAGS = $(MCUFLAGS) $(ARCH_FLAGS) $(OPT_FLAGS) $(PLATFORM_FLAGS) $(FPIC)
CFLAGS += -Wall -Wextra -Werror
CFLAGS += -ffunction-sections -fdata-sections
CFLAGS += -std=c11
CFLAGS += $(OSC_DDEFS)

CXXFLAGS = $(MCUFLAGS) $(ARCH_FLAGS) $(OPT_FLAGS) $(PLATFORM_FLAGS) $(FPIC)
CXXFLAGS += -Wall -Wextra -Werror
CXXFLAGS += -ffunction-sections -fdata-sections
CXXFLAGS += -fno-exceptions -fno-rtti
CXXFLAGS += -std=c++11
CXXFLAGS += $(OSC_DDEFS)

# Add NEON flags for drumlogue
ifeq ($(PLATFORM),drumlogue)
    CFLAGS += $(NEON_FLAGS)
    CXXFLAGS += $(NEON_FLAGS)
endif

# Add include directories
CFLAGS += $(UINCDIR)
CXXFLAGS += $(UINCDIR)

# Linker flags
LDFLAGS = $(MCUFLAGS) $(ARCH_FLAGS)
LDFLAGS += -Wl,--gc-sections

ifeq ($(PLATFORM),drumlogue)
    LDFLAGS += $(SHARED_FLAGS)
    LDFLAGS += -Wl,--as-needed -Wl,-z,relro -Wl,-z,now
    LDLIBS = -lm -lpthread
else
    # Use logue SDK linker script
    LDSCRIPT = $(SDK_PLATFORM_DIR)/ld/userosc.ld
    LDFLAGS += -specs=nano.specs -specs=nosys.specs
    LDFLAGS += -Xlinker --just-symbols=$(SDK_PLATFORM_DIR)/ld/osc_api.syms
    LDFLAGS += -T $(LDSCRIPT)
    LDLIBS = -lm
endif

# Build directories
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj

# Object files
UOBJS = $(addprefix $(OBJ_DIR)/, $(notdir $(UCSRC:.c=.o)))
UOBJS += $(addprefix $(OBJ_DIR)/, $(notdir $(UCXXSRC:.cc=.o)))

# Target output
ifeq ($(PLATFORM),drumlogue)
    TARGET_BIN = $(PROJECT).so
    TARGET_PKG = $(PROJECT).$(PKGEXT)
else
    TARGET_BIN = $(PROJECT).elf
    TARGET_PKG = $(PROJECT).$(PKGEXT)
endif

# Default target
all: $(TARGET_PKG)

# Debug build configuration
DEBUG ?= 0
ifeq ($(DEBUG),1)
    CFLAGS += -g -DDEBUG -O0
    CXXFLAGS += -g -DDEBUG -O0
    OPT_FLAGS = -O0
else
    CFLAGS += -DNDEBUG
    CXXFLAGS += -DNDEBUG
endif

# Verbose output
VERBOSE ?= 0
ifeq ($(VERBOSE),1)
    Q =
else
    Q = @
endif

# Color output
NO_COLOR=\033[0m
OK_COLOR=\033[32;01m
ERROR_COLOR=\033[31;01m
WARN_COLOR=\033[33;01m

# Build messages
define print_build
	@echo "$(OK_COLOR)[BUILD $(PLATFORM)]$(NO_COLOR) $(1)"
endef

define print_link
	@echo "$(OK_COLOR)[LINK $(PLATFORM)]$(NO_COLOR) $(1)"
endef

define print_package
	@echo "$(OK_COLOR)[PACKAGE $(PLATFORM)]$(NO_COLOR) $(1)"
endef

define print_clean
	@echo "$(WARN_COLOR)[CLEAN]$(NO_COLOR) $(1)"
endef

# Compilation rules
$(OBJ_DIR)/%.o: %.c | $(OBJ_DIR)
	$(call print_build,$<)
	$(Q)$(CC) -c $(CFLAGS) $< -o $@

$(OBJ_DIR)/%.o: %.cc | $(OBJ_DIR)
	$(call print_build,$<)
	$(Q)$(CXX) -c $(CXXFLAGS) $< -o $@

$(OBJ_DIR)/%.o: eurorack/%.cc | $(OBJ_DIR)
	$(call print_build,$<)
	$(Q)$(CXX) -c $(CXXFLAGS) $< -o $@

$(OBJ_DIR)/%.o: eurorack/plaits/dsp/%.cc | $(OBJ_DIR)
	$(call print_build,$<)
	$(Q)$(CXX) -c $(CXXFLAGS) $< -o $@

$(OBJ_DIR)/%.o: eurorack/plaits/dsp/engine/%.cc | $(OBJ_DIR)
	$(call print_build,$<)
	$(Q)$(CXX) -c $(CXXFLAGS) $< -o $@

$(OBJ_DIR)/%.o: eurorack/plaits/dsp/physical_modelling/%.cc | $(OBJ_DIR)
	$(call print_build,$<)
	$(Q)$(CXX) -c $(CXXFLAGS) $< -o $@

$(OBJ_DIR)/%.o: eurorack/plaits/dsp/fx/%.cc | $(OBJ_DIR)
	$(call print_build,$<)
	$(Q)$(CXX) -c $(CXXFLAGS) $< -o $@

$(OBJ_DIR)/%.o: eurorack/plaits/%.cc | $(OBJ_DIR)
	$(call print_build,$<)
	$(Q)$(CXX) -c $(CXXFLAGS) $< -o $@

$(OBJ_DIR)/%.o: eurorack/elements/dsp/%.cc | $(OBJ_DIR)
	$(call print_build,$<)
	$(Q)$(CXX) -c $(CXXFLAGS) $< -o $@

$(OBJ_DIR)/%.o: eurorack/stmlib/dsp/%.cc | $(OBJ_DIR)
	$(call print_build,$<)
	$(Q)$(CXX) -c $(CXXFLAGS) $< -o $@

$(OBJ_DIR)/%.o: eurorack/stmlib/utils/%.cc | $(OBJ_DIR)
	$(call print_build,$<)
	$(Q)$(CXX) -c $(CXXFLAGS) $< -o $@

# Create build directory
$(OBJ_DIR):
	$(Q)mkdir -p $(OBJ_DIR)

# Linking
$(BUILD_DIR)/$(TARGET_BIN): $(UOBJS)
	$(call print_link,$@)
	$(Q)$(LD) $(LDFLAGS) -o $@ $(UOBJS) $(LDLIBS)
ifeq ($(PLATFORM),drumlogue)
	$(Q)$(SIZE) $@
else
	$(Q)$(SIZE) $@
	$(Q)$(OBJCOPY) -O binary $@ $(BUILD_DIR)/$(PROJECT).bin
endif

# Packaging
ifeq ($(MANIFEST),)
    MANIFEST = manifest_$(OSCILLATOR).json
endif

$(TARGET_PKG): $(BUILD_DIR)/$(TARGET_BIN)
	$(call print_package,$@)
ifeq ($(PLATFORM),drumlogue)
	# Drumlogue: Copy shared library directly
	$(Q)cp $(BUILD_DIR)/$(TARGET_BIN) $@
else
	# Other platforms: Create ZIP with manifest and binary
	$(Q)cp $(BUILD_DIR)/$(PROJECT).bin $(PROJECT).bin
	$(Q)if [ -f $(MANIFEST) ]; then \
		jq --arg ver "$(VERSION)" '.header.version = $$ver' $(MANIFEST) > $(PROJECT)_tmp.json && \
		zip -j -q $(TARGET_PKG) $(PROJECT)_tmp.json $(PROJECT).bin && \
		rm -f $(PROJECT)_tmp.json $(PROJECT).bin; \
	else \
		echo "Warning: $(MANIFEST) not found, creating package without manifest"; \
		zip -j -q $(TARGET_PKG) $(PROJECT).bin && \
		rm -f $(PROJECT).bin; \
	fi
endif

# Phony targets
.PHONY: all clean size

clean:
	$(call print_clean,$(BUILD_DIR) $(TARGET_PKG))
	$(Q)rm -rf $(BUILD_DIR) $(TARGET_PKG)

size: $(BUILD_DIR)/$(TARGET_BIN)
	@echo "Size of $(TARGET_BIN):"
	$(Q)$(SIZE) $<

