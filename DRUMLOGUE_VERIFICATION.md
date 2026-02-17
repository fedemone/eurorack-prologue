# Drumlogue Port - Verification Report

Generated: 2026-02-11

## Overview

This report documents the verification and corrections made to the drumlogue port.
The initial port had several structural issues that have been identified and fixed.

## Issues Found & Fixed

### 1. `unit_init` Signature (CRITICAL)

**Before**: `void unit_init(uint32_t platform, uint32_t api_version)`
**SDK Requires**: `int8_t unit_init(const unit_runtime_desc_t *desc)`

The drumlogue Synth Module API passes a packed `unit_runtime_desc_t` struct containing
target, API version, sample rate, buffer size, channel count, and sample bank callbacks.
The function must return an `int8_t` error code (`k_unit_err_none` on success).

**Fix**: Complete rewrite of `drumlogue_unit_wrapper.cc` with correct signature, runtime
validation (target, API version, sample rate checks), and proper error returns.

### 2. `unit_header_t` Format (CRITICAL)

**Before**: Custom struct with magic numbers and char pointers — incompatible with SDK.
**SDK Requires**: Packed struct (`#pragma pack(push, 1)`) with specific field layout placed
in `.unit_header` ELF section via `__attribute__((used, section(".unit_header")))`.

**Fix**: Created `drumlogue/runtime.h` with exact SDK-matching packed struct definitions.
The `unit_header` in `drumlogue_unit_wrapper.cc` now uses the correct `unit_header_t`
format with proper `unit_param_t` descriptors for all 6 parameters.

### 3. Root `userosc.h` Shadowing SDK (HIGH)

**Before**: A `userosc.h` file in the project root was found before the SDK's version
due to `-I.` appearing first in include paths. This broke prologue builds because:
- `user_osc_param_t` field order was wrong (`pitch` first; SDK has `shape_lfo` first)
- Extra `value` field not present in SDK struct

**Fix**: Deleted root `userosc.h`. Created `drumlogue/userosc.h` with correct struct
layout matching SDK. Include paths updated so `-Idrumlogue` is only added for drumlogue
builds, while prologue/minilogue-xd/nts-1 builds use SDK's own `userosc.h`.

### 4. Duplicate Function Definitions in Adapter (HIGH)

**Before**: `drumlogue_osc_adapter.cc` had 3 functions defined twice with different
signatures: `osc_adapter_param`, `osc_adapter_get_param_str`, `osc_adapter_tempo_tick`.

**Fix**: Complete rewrite of `drumlogue_osc_adapter.cc` and `.h` with clean single
definitions and a minimal interface.

### 5. Block Size Mismatch (HIGH)

**Before**: The adapter passed drumlogue's frame count directly to `OSC_CYCLE`, but:
- `macro-oscillator2.cc` always renders exactly `plaits::kMaxBlockSize` (24) samples
- `modal-strike.cc` always renders `2 * elements::kMaxBlockSize` (32) samples
Both ignore the `frames` parameter entirely, leading to buffer over/underruns.

**Fix**: Implemented buffered rendering in the adapter. Each `.mk` file now defines
`OSC_NATIVE_BLOCK_SIZE` (24 for Plaits oscillators, 32 for modal-strike oscillators).
The adapter calls `OSC_CYCLE` in native-sized blocks and serves frames from an internal
ring buffer, handling any drumlogue frame count correctly.

### 6. Missing SDK Type Definitions (MEDIUM)

**Before**: No definitions for `unit_runtime_desc_t`, `unit_param_t`, error codes,
param type enums, target constants, or API version constants.

**Fix**: Created `drumlogue/runtime.h` with all SDK types, `drumlogue/unit.h` with
function declarations, and `drumlogue/attributes.h` with compiler attribute macros.

### 7. Platform Name Inconsistency (LOW)

**Before**: `Makefile` passed `PLATFORM=nutekt-digital` but `makefile.inc` checked
for `nts-1`, causing builds for NTS-1 to silently fail.

**Fix**: Changed `Makefile` to use `PLATFORM=nts-1` consistently.

### 8. Manifest Files Now Redundant (LOW)

The 14 `manifest_drumlogue_*.json` files were created for an older approach. The
drumlogue platform uses embedded `unit_header_t` in ELF sections — no external
manifest files are needed. These files remain in the repo but are unused by the
current build system.

## Build System Verification

### Makefile Configuration
- **Platform detection**: Correctly identifies `PLATFORM=drumlogue`
- **Toolchain**: `arm-linux-gnueabihf-gcc/g++` configured
- **Architecture**: ARM Cortex-A7 with NEON VFPv4
- **Build mode**: Shared library (`-fPIC -shared`)

### Compiler Flags
```bash
-mcpu=cortex-a7
-mfpu=neon-vfpv4
-mfloat-abi=hard
-march=armv7ve+simd
-ftree-vectorize
-ffast-math
```
Status: Appropriate for ARM Cortex-A7 + NEON.

### Linker Flags
```bash
-shared
-Wl,-soname,<project>.drmlgunit.so
-Wl,--as-needed
-Wl,-z,relro
-Wl,-z,now
-lm -lpthread
```
Status: Correct for shared library with security hardening.

### Include Paths
- **Drumlogue builds**: `-Idrumlogue -I. -Ieurorack` (drumlogue/ directory first)
- **Other platforms**: `-I. -Ieurorack -I$(SDK_PLATFORM_DIR)/inc ...` (SDK headers used)

This ensures drumlogue builds pick up `drumlogue/userosc.h` while prologue builds
use the SDK's `userosc.h`.

### Oscillator Makefiles (15 total)
All `.mk` files updated with:
- Drumlogue wrapper file auto-inclusion (`drumlogue_osc_adapter.cc`, `drumlogue_unit_wrapper.cc`)
- `OSC_NATIVE_BLOCK_SIZE` compile-time definition

## API Bridging (Verified)

| Drumlogue (Synth Module API) | Adapter | Oscillator (User OSC API) |
|---|---|---|
| `unit_init(desc)` | `osc_adapter_init()` | `OSC_INIT(platform, api)` |
| `unit_render(in, out, frames)` | `osc_adapter_render()` | `OSC_CYCLE(params, yn, frames)` |
| `unit_note_on(note, vel)` | `osc_adapter_note_on()` | `OSC_NOTEON(params)` |
| `unit_note_off(note)` | `osc_adapter_note_off()` | `OSC_NOTEOFF(params)` |
| `unit_set_param_value(id, val)` | `osc_adapter_set_param()` | `OSC_PARAM(idx, val)` |

### Format Conversions
- Q31 (`int32_t`) -> float: via `q31_to_float()` in adapter render path
- Mono -> Stereo interleaved: in `unit_render()` (unit wrapper)
- Parameter scaling: drumlogue `int32_t` (0-100) -> OSC 10-bit (0-1023) for knobs, direct for enums

## Untested (Requires Toolchain / Hardware)

### Compilation
- [ ] ARM cross-compilation with `arm-linux-gnueabihf-gcc`
- [ ] No compiler warnings or errors
- [ ] `.drmlgunit` file generation
- [ ] `unit_header_t` correctly placed in `.unit_header` ELF section

### Runtime (Hardware Required)
- [ ] Audio output on drumlogue hardware
- [ ] Parameter changes via UI
- [ ] Note on/off triggering
- [ ] Pitch bend response
- [ ] CPU usage monitoring
- [ ] Memory usage profiling

### Audio Quality (Hardware Required)
- [ ] No artifacts or glitches at block boundaries
- [ ] Correct pitch tracking across full MIDI range
- [ ] Parameter response matches prologue behavior
- [ ] Stereo output balanced (mono duplicated to both channels)

## SDK Structure Alignment (Stage 4)

Adapted project structure to match the drumlogue SDK convention (dummy-synth template
and boochow/eurorack_drumlogue Braids port):

- **`header.c`**: Unit header extracted to separate C file, placed in `.unit_header` ELF section
- **`config.mk`**: SDK-compatible project configuration
- **`unit_init` target check**: Now uses `desc->target != unit_header.target` (matches Braids port)
- **All 15 `.mk` files**: Updated to include `header.c` for drumlogue builds

## Host-Side Testing (Stages 2 + 4)

### Callback Tests (55 tests — `make test`)

Mock-based tests verify the full callback chain without ARM hardware:
- unit_header structure, unit_init validation, note events, pitch bend
- Parameter mapping/scaling, shape LFO, Q31/float conversion
- Buffered rendering across all frame sizes, stereo interleaving, lifecycle

### Sound Production Tests (9 tests — `make test-sound`)

Links the **real Plaits VirtualAnalogEngine** and verifies end-to-end audio:
- Engine init succeeds through full wrapper chain
- Note-on produces non-zero stereo audio (L=R mono duplication)
- Different MIDI notes produce different waveforms
- Shape parameter changes affect output
- Output amplitude within valid range, no NaN/Inf
- Continuous rendering across multiple calls
- Note-off remains stable (no crashes, valid floats)

```bash
make test-all  # Runs all 64 tests (55 callback + 9 sound production)
```

## SDK Docker Build Integration (Stage 5)

### logue-sdk Submodule Update

The `logue-sdk` submodule was updated from v1.x (Sept 2019, commit `b7a424e`) to the
main branch (commit `10a38b3`). This adds:
- `platform/drumlogue/` — common headers and project templates
- `docker/` — Docker-based build system (`run_interactive.sh`, `run_cmd.sh`, `build_image.sh`)

### SDK Header Compatibility

Comparison of our `drumlogue/` headers vs SDK `common/` headers:
- Core structs (`unit_header_t`, `unit_param_t`, `unit_runtime_desc_t`) — **identical layout**
- SDK version has additional helper macros (`UNIT_TARGET_PLATFORM_IS_COMPAT`, `UNIT_API_IS_COMPAT`, etc.)
- SDK `runtime.h` includes `sample_wrapper.h` separately; ours inlines the definition
- Our local `drumlogue/` headers used for host-side test compilation
- SDK `common/` headers used automatically for ARM cross-compilation via Docker

### Per-Oscillator Project Structure

15 SDK-compatible project directories created under `logue-sdk/platform/drumlogue/`:

| Project | Type | Defines | Block Size |
|---|---|---|---|
| `mo2_va` | Plaits | `-DOSC_VA` | 24 |
| `mo2_wsh` | Plaits | `-DOSC_WSH` | 24 |
| `mo2_fm` | Plaits | `-DOSC_FM` | 24 |
| `mo2_grn` | Plaits | `-DOSC_GRN` | 24 |
| `mo2_add` | Plaits | `-DOSC_ADD` | 24 |
| `mo2_string` | Plaits | `-DOSC_STRING` | 24 |
| `mo2_wta`..`mo2_wtf` | Plaits | `-DOSC_WTA`..`-DOSC_WTF` | 24 |
| `modal_strike` | Elements | `-DELEMENTS_RESONATOR_MODES=24 -DUSE_LIMITER` | 32 |
| `modal_strike_16_nolimit` | Elements | `-DELEMENTS_RESONATOR_MODES=16` | 32 |
| `modal_strike_24_nolimit` | Elements | `-DELEMENTS_RESONATOR_MODES=24` | 32 |

Each project directory contains:
- `Makefile` — copied from SDK `dummy-synth/Makefile` (436 lines, unmodified)
- `config.mk` — project-specific sources, includes, defines

### SDK Build Validation (Dry-Run)

`make -n` from each project directory shows correct:
- **Compiler**: `$(CROSS_COMPILE)gcc` / `g++` (set by Docker environment)
- **Architecture**: `-march=armv7-a -mtune=cortex-a7 -marm -mfpu=neon-vfpv4 -mfloat-abi=hard`
- **Optimization**: `-Os -flto -mvectorize-with-neon-quad -ftree-vectorize`
- **Includes**: `-I. -I<common/> -I<drumlogue/> -I<eurorack/> -I<repo_root/>`
- **Defines**: `-D__arm__ -D__cortex_a7__ -D<OSC_*> -DOSC_NATIVE_BLOCK_SIZE=<N>`
- **Sources**: All oscillator sources, wrapper, adapter, header.c, _unit_base.c
- **Output**: `build/<project>.drmlgunit` (shared library, stripped)

### Build Instructions

```bash
# 1. Build the Docker image (first time only)
cd logue-sdk && docker/build_image.sh && cd ..

# 2. Build all oscillators
./build_drumlogue.sh

# 3. Or build a specific one
./build_drumlogue.sh mo2_va

# 4. Collect all .drmlgunit files
./build_drumlogue.sh --collect
# -> build/drumlogue/*.drmlgunit

# 5. Interactive Docker shell
./build_drumlogue.sh --interactive
```

## Next Steps

1. **Build Docker image** and run first real compilation:
   ```bash
   cd logue-sdk && docker/build_image.sh
   ./build_drumlogue.sh mo2_va
   ```

2. **Verify ELF output**:
   ```bash
   file build/drumlogue/*.drmlgunit  # ELF 32-bit LSB shared object, ARM
   readelf -S build/drumlogue/*.drmlgunit | grep unit_header
   ```

3. **Test on Hardware**:
   - Copy `.drmlgunit` files to `Units/Synths/` on drumlogue USB storage
   - Restart and test audio output + parameters

---
**Last Updated**: 2026-02-17
