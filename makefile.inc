# makefile.inc - Build configuration for eurorack-prologue
# Supports multiple platforms: prologue, minilogue-xd, nts-1, drumlogue

# Platform detection and configuration
ifeq ($(PLATFORM),prologue)
    PLATFORM_NAME = prologue
    PLATFORM_FLAGS = -DPLATFORM_PROLOGUE
else ifeq ($(PLATFORM),minilogue-xd)
    PLATFORM_NAME = minilogue-xd
    PLATFORM_FLAGS = -DPLATFORM_MINILOGUEXD
else ifeq ($(PLATFORM),nts-1)
    PLATFORM_NAME = nts-1
    PLATFORM_FLAGS = -DPLATFORM_NTS1
else ifeq ($(PLATFORM),drumlogue)
    PLATFORM_NAME = drumlogue
    PLATFORM_FLAGS = -DPLATFORM_DRUMLOGUE
else
    $(error PLATFORM not specified. Set PLATFORM=prologue, minilogue-xd, nts-1, or drumlogue)
endif

# Common toolchain settings
ifeq ($(PLATFORM),drumlogue)
    # Drumlogue uses ARM Cortex-A7 architecture
    PREFIX = arm-linux-gnueabihf-
    CC = $(PREFIX)gcc
    CXX = $(PREFIX)g++
    LD = $(PREFIX)ld
    AR = $(PREFIX)ar
    AS = $(PREFIX)as
    OBJCOPY = $(PREFIX)objcopy
    OBJDUMP = $(PREFIX)objdump
    SIZE = $(PREFIX)size
    
    # ARM Cortex-A7 specific flags
    MCU = -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard
    ARCH_FLAGS = -march=armv7ve+simd
    
    # NEON vectorization support
    NEON_FLAGS = -mfpu=neon-vfpv4 -ftree-vectorize -ffast-math
    
    # Shared library compilation flags
    FPIC = -fPIC
    SHARED_FLAGS = -shared -Wl,-soname,$(TARGET).so
    
    # Platform-specific optimization
    OPT_FLAGS = -O3 -funroll-loops -fomit-frame-pointer
    
else
    # Other platforms use ARM Cortex-M4
    PREFIX = arm-none-eabi-
    CC = $(PREFIX)gcc
    CXX = $(PREFIX)g++
    LD = $(PREFIX)ld
    AR = $(PREFIX)ar
    AS = $(PREFIX)as
    OBJCOPY = $(PREFIX)objcopy
    OBJDUMP = $(PREFIX)objdump
    SIZE = $(PREFIX)size
    
    # ARM Cortex-M4 specific flags
    MCU = -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
    ARCH_FLAGS = -march=armv7e-m
    
    FPIC =
    SHARED_FLAGS =
    OPT_FLAGS = -O2 -flto
    NEON_FLAGS =
endif

# Common compiler flags
CFLAGS = $(MCU) $(ARCH_FLAGS) $(OPT_FLAGS) $(PLATFORM_FLAGS) $(FPIC)
CFLAGS += -Wall -Wextra -Werror
CFLAGS += -ffunction-sections -fdata-sections
CFLAGS += -std=c11

CXXFLAGS = $(MCU) $(ARCH_FLAGS) $(OPT_FLAGS) $(PLATFORM_FLAGS) $(FPIC)
CXXFLAGS += -Wall -Wextra -Werror
CXXFLAGS += -ffunction-sections -fdata-sections
CXXFLAGS += -fno-exceptions -fno-rtti
CXXFLAGS += -std=c++11

# Add NEON flags for drumlogue
ifeq ($(PLATFORM),drumlogue)
    CFLAGS += $(NEON_FLAGS)
    CXXFLAGS += $(NEON_FLAGS)
endif

# Linker flags
LDFLAGS = $(MCU) $(ARCH_FLAGS)
LDFLAGS += -Wl,--gc-sections

ifeq ($(PLATFORM),drumlogue)
    LDFLAGS += $(SHARED_FLAGS)
    LDFLAGS += -Wl,--as-needed -Wl,-z,relro -Wl,-z,now
else
    LDFLAGS += -specs=nano.specs -specs=nosys.specs
endif

# Include paths
INCLUDES = -I. -Iinc

# Library paths and libraries
ifeq ($(PLATFORM),drumlogue)
    LIBS = -lm -lpthread
else
    LIBS = -lm
endif

# Build directories
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)/bin

# Target output
ifeq ($(PLATFORM),drumlogue)
    TARGET_EXT = .so
else
    TARGET_EXT = .bin
endif

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
	@echo "$(OK_COLOR)[BUILD]$(NO_COLOR) $(1)"
endef

define print_link
	@echo "$(OK_COLOR)[LINK]$(NO_COLOR) $(1)"
endef

define print_clean
	@echo "$(WARN_COLOR)[CLEAN]$(NO_COLOR) $(1)"
endef

# Common rules
%.o: %.c
	$(call print_build,$<)
	$(Q)$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

%.o: %.cpp
	$(call print_build,$<)
	$(Q)$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

%.o: %.s
	$(call print_build,$<)
	$(Q)$(AS) $(MCU) -c $< -o $@

# Phony targets
.PHONY: all clean size

clean:
	$(call print_clean,$(BUILD_DIR))
	$(Q)rm -rf $(BUILD_DIR)

size: $(TARGET)
	@echo "Size of $(TARGET):"
	$(Q)$(SIZE) $(TARGET)
