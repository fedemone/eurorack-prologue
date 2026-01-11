# Legacy Makefile Drumlogue Support (Reference Only)

## ⚠️ Important Note

The changes made to `Makefile` and `makefile.inc` to add Drumlogue support are **NOT USED** by the official Drumlogue SDK build system.

They were added to the legacy Prologue build system for completeness but are:
- ❌ Not compatible with Drumlogue's actual architecture (Drumlogue uses ARMv7/Linux, not STM32F4)
- ❌ Not maintained or supported officially
- ❌ Cannot produce valid `.drmlgunit` files

**Use the official SDK instead**: `logue-sdk/docker/run_cmd.sh build drumlogue/<project>`

---

## What Was Changed (For Reference)

### Makefile Changes

**Line 7:** Added `package_drumlogue` to main targets
```makefile
$(TOPTARGETS): $(OSCILLATORS) package_prologue package_minilogue-xd package_nutekt-digital package_drumlogue
```

**Line 15-16:** Added Drumlogue build step
```makefile
@rm -fR .dep ./build
@PLATFORM=drumlogue VERSION=$(VERSION) $(MAKE) -f $@ $(MAKECMDGOALS)
```

**Line 23:** Added Drumlogue package variable
```makefile
DRUMLOGUE_PACKAGE=eurorack_drumlogue
```

**Line 57-63:** Added Drumlogue packaging rule
```makefile
package_drumlogue:
	@echo Packaging to ./${DRUMLOGUE_PACKAGE}.zip
	@rm -f ${DRUMLOGUE_PACKAGE}.zip
	@rm -rf ${DRUMLOGUE_PACKAGE}
	@mkdir ${DRUMLOGUE_PACKAGE}
	@cp -a *.drmlgunit ${DRUMLOGUE_PACKAGE}/
	@cp -a credits.txt ${DRUMLOGUE_PACKAGE}/
	@zip -rq9m ${DRUMLOGUE_PACKAGE}.zip ${DRUMLOGUE_PACKAGE}/
```

**Line 19:** Added drumlogue to .PHONY targets
```makefile
.PHONY: $(TOPTARGETS) $(OSCILLATORS) drumlogue
```

**Line 17-20:** Added drumlogue target
```makefile
drumlogue: $(OSCILLATORS) package_drumlogue
$(OSCILLATORS):
	@rm -fR .dep ./build
	@PLATFORM=drumlogue VERSION=$(VERSION) $(MAKE) -f $@ all
```

### makefile.inc Changes

**Line 60-63:** Added Drumlogue platform configuration
```makefile
ifeq ($(PLATFORM), drumlogue)
    PKGSUFFIX = drmlgunit
	BLOCKSIZE = 64
endif
```

---

## Why These Changes Don't Work

### Architecture Mismatch
- **Prologue/Minilogue XD/NTS-1**: ARM Cortex-M4 microcontroller (STM32F4)
- **Drumlogue**: ARM Cortex-A7 Linux processor
- The toolchain, libraries, and compilation flags are completely different

### Build Process Difference
| Aspect | Prologue | Drumlogue |
|--------|----------|-----------|
| **Toolchain** | `arm-none-eabi-gcc` (bare-metal) | `arm-unknown-linux-gnueabihf-gcc` (Linux) |
| **Libc** | None (bare-metal) | glibc (GNU C Library) |
| **Build System** | Make + custom Makefile | Make + Docker wrapper |
| **Headers** | Prologue SDK specific | Drumlogue SDK specific |
| **Linking** | Bare-metal linking script | Dynamic linking |

### Why Official SDK Uses Docker
The official SDK uses Docker because:
1. **Cross-compilation complexity** - Building for Linux ARM on Windows/macOS is difficult
2. **Dependency management** - All compiler versions, libraries, and tools in one container
3. **Consistency** - Same build environment regardless of host OS
4. **Portability** - No local toolchain installation required

---

## Correct Drumlogue Build Process

Use the official SDK:
```bash
# Option 1: Single command
logue-sdk/docker/run_cmd.sh build drumlogue/dummy-synth

# Option 2: Interactive development
logue-sdk/docker/run_interactive.sh
user@logue-sdk $ build drumlogue/dummy-synth
```

This:
- ✅ Uses correct ARM Linux cross-compiler
- ✅ Links with proper glibc libraries
- ✅ Compiles with Drumlogue-specific headers
- ✅ Produces valid `.drmlgunit` files

---

## Legacy Makefile - Still Works For

The modified `Makefile` and `makefile.inc` still work perfectly for:
- ✅ Prologue oscillators (`.prlgunit`)
- ✅ Minilogue XD oscillators (`.mnlgxdunit`)
- ✅ Nu:tekt NTS-1 oscillators (`.ntkdigunit`)

**To use legacy build:**
```bash
make                          # Build all platforms
make prologue                 # Build Prologue only
make minilogue-xd            # Build Minilogue XD only
make nutekt-digital          # Build NTS-1 only
```

---

## File Locations

### Files with Drumlogue Support Added (Legacy)
- `Makefile` - Root makefile
- `makefile.inc` - Shared configuration

### Files to Use for Official Drumlogue Builds
- `logue-sdk/docker/build_image.sh` - Build Docker image
- `logue-sdk/docker/run_cmd.sh` - Build command wrapper
- `logue-sdk/docker/run_interactive.sh` - Interactive shell
- `logue-sdk/platform/drumlogue/Makefile` - Official Drumlogue Makefile (inside Docker)

---

## Summary

| Task | Tool | Location |
|------|------|----------|
| Build Prologue/Minilogue XD/NTS-1 | `make` (legacy) | `./Makefile` |
| Build Drumlogue | Docker + official SDK | `logue-sdk/docker/run_cmd.sh` |
| Get Drumlogue output | N/A | `logue-sdk/platform/drumlogue/<project>/build/` |
| Reference only | Legacy Makefile Drumlogue support | `./*Makefile` (not functional for Drumlogue) |

**Bottom line:** Use the official SDK for Drumlogue. The Makefile changes are for reference/documentation only.
