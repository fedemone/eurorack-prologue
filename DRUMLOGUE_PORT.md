# Drumlogue Port of Eurorack Oscillators

This branch adds support for the **Korg drumlogue** platform to the eurorack-prologue project.

## Design Principle

**Same source code, different HW APIs.** The oscillator source files (`macro-oscillator2.cc`,
`modal-strike.cc`) compile unchanged for both prologue-class platforms and drumlogue. A thin
wrapper layer translates between the drumlogue Synth Module API (logue-sdk v2.0) and the
User Oscillator API (logue-sdk v1.x) that the oscillators were written against.

## Key Differences: Prologue vs Drumlogue

| Aspect | Prologue / Minilogue XD / NTS-1 | Drumlogue |
|---|---|---|
| **SDK version** | logue-sdk v1.x (User OSC API) | logue-sdk v2.0 (Synth Module API) |
| **Architecture** | ARM Cortex-M4 (Thumb, bare metal) | ARM Cortex-A7 (ARM, Linux-based) |
| **FPU** | FPv4-SP-D16 (single precision) | NEON VFPv4 (SIMD + double) |
| **Audio format** | `int32_t *` Q31 fixed-point, mono | `float *` 32-bit IEEE 754, stereo interleaved |
| **Sample rate** | 48 kHz | 48 kHz |
| **Init callback** | `void OSC_INIT(uint32_t, uint32_t)` | `int8_t unit_init(const unit_runtime_desc_t *)` |
| **Render callback** | `void OSC_CYCLE(params, int32_t *yn, frames)` | `void unit_render(float *in, float *out, frames)` |
| **Note on** | `void OSC_NOTEON(params)` (pitch in struct) | `void unit_note_on(uint8_t note, uint8_t vel)` |
| **Param set** | `void OSC_PARAM(uint16_t idx, uint16_t val)` | `void unit_set_param_value(uint8_t id, int32_t val)` |
| **Max params** | 6 | 24 |
| **Build output** | `.prlgunit` (ZIP: binary + manifest) | `.drmlgunit` (ELF shared library) |
| **Toolchain** | `arm-none-eabi-gcc` | `arm-linux-gnueabihf-gcc` |
| **Unit header** | Hook table at fixed binary offset | `unit_header_t` in `.unit_header` ELF section |

## Architecture

```
Drumlogue Runtime (Linux, ARM Cortex-A7)
     |
     |  Synth Module API (unit_init, unit_render, unit_note_on, ...)
     v
drumlogue_unit_wrapper.cc
  - Implements all unit_* callbacks
  - Exports unit_header_t in .unit_header section
  - Maps drumlogue params (int32) to OSC params (10-bit / enum)
  - Converts mono float -> stereo interleaved float
     |
     |  Adapter API (osc_adapter_init, osc_adapter_render, ...)
     v
drumlogue_osc_adapter.cc
  - Manages user_osc_param_t struct (pitch, shape_lfo)
  - Buffered rendering: calls OSC_CYCLE in native block sizes
  - Converts Q31 <-> float
  - Handles pitch bend translation
     |
     |  User OSC API (OSC_INIT, OSC_CYCLE, OSC_NOTEON, ...)
     v
macro-oscillator2.cc / modal-strike.cc  (UNCHANGED)
```

## Files Added/Modified

### New: `drumlogue/` directory (SDK compatibility headers)

| File | Purpose |
|---|---|
| `drumlogue/runtime.h` | `unit_runtime_desc_t`, `unit_header_t`, `unit_param_t`, constants |
| `drumlogue/unit.h` | `unit_*` function declarations |
| `drumlogue/attributes.h` | `__unit_callback`, `__unit_header` macros |
| `drumlogue/userosc.h` | OSC API types for drumlogue (replaces SDK's `userosc.h`) |

### New: Wrapper/adapter files

| File | Purpose |
|---|---|
| `drumlogue_unit_wrapper.cc` | Synth Module API implementation |
| `drumlogue_osc_adapter.cc` | OSC API bridge with buffered rendering |
| `drumlogue_osc_adapter.h` | Adapter interface header |

### Modified: Build system

| File | Change |
|---|---|
| `makefile.inc` | Drumlogue toolchain, flags, include paths, shared library build |
| `Makefile` | Drumlogue in build loop + packaging target |
| `osc_*.mk` (all 15) | Auto-include wrapper files + `OSC_NATIVE_BLOCK_SIZE` define |

## Building for Drumlogue

### Prerequisites
1. Clone with submodules:
   ```bash
   git clone --recursive https://github.com/fedemone/eurorack-prologue.git
   ```

2. Install the ARM Linux cross-compiler:
   ```bash
   # Ubuntu/Debian
   sudo apt-get install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf
   ```

### Build Commands
```bash
# Build all oscillators for drumlogue only
PLATFORM=drumlogue make

# Build a specific oscillator
PLATFORM=drumlogue make -f osc_va.mk

# Build for all platforms
make
```

### Output
`.drmlgunit` files (ELF shared libraries with embedded `unit_header_t`).

### Installation on Drumlogue
1. Power on drumlogue in USB mass storage mode
2. Place `.drmlgunit` files in the `Units/Synths/` directory
3. Restart drumlogue to load the new synth units

## Staging Plan

### Stage 1: Same Source, Different APIs (current)
- Wrapper layer bridges Synth Module API to OSC API
- Oscillator source code compiles unchanged
- Build system supports all 4 platforms

### Stage 2: Unit Tests for Callbacks
- Verify `unit_init` -> `OSC_INIT` flow
- Verify `unit_note_on`/`unit_note_off` -> `OSC_NOTEON`/`OSC_NOTEOFF`
- Verify `unit_set_param_value` -> `OSC_PARAM` with correct scaling
- Verify `unit_render` -> `OSC_CYCLE` with Q31/float conversion
- Verify buffered rendering across block size boundaries
- Verify pitch bend translation

### Stage 3: NEON Optimizations
- Profile hot paths in DSP code
- Replace scalar loops with NEON intrinsics (`float32x4_t`)
- Key targets: Q31/float conversion, buffer copy, engine render loops

### Stage 4: Verification of Optimized Output
- Bit-exact comparison of NEON vs scalar output (where applicable)
- Performance benchmarks on Cortex-A7
- Audio quality verification

### Future: Template Abstraction
- Extract wrapper pattern into a reusable template
- Enable porting additional Mutable Instruments modules
- Enable porting from other open-source DSP sources

## Notes

- Drumlogue has significantly more CPU power and memory than Cortex-M4 platforms
- The 32K size constraint does not apply on drumlogue
- Sample rate is 48 kHz (same as all logue platforms)
- The logue-sdk submodule is pinned to v1.x (Sept 2019); drumlogue headers are provided locally

## References

- [Korg logue SDK](https://github.com/korginc/logue-sdk)
- [Drumlogue Platform (SDK v2.0)](https://github.com/korginc/logue-sdk/tree/master/platform/drumlogue)
- [Original Eurorack-Prologue](https://github.com/peterall/eurorack-prologue)
