# Drumlogue Port - TODO List

## Stage 1: Same Source Code, Different HW APIs

### Completed

1. **SDK compatibility headers** (`drumlogue/` directory)
   - [x] `drumlogue/runtime.h` — `unit_runtime_desc_t`, `unit_header_t`, `unit_param_t`, constants
   - [x] `drumlogue/unit.h` — `unit_*` function declarations
   - [x] `drumlogue/attributes.h` — `__unit_callback`, `__unit_header` macros
   - [x] `drumlogue/userosc.h` — OSC API types for drumlogue with correct `user_osc_param_t` layout

2. **Unit wrapper** (`drumlogue_unit_wrapper.cc`)
   - [x] Correct `unit_init` signature: `int8_t unit_init(const unit_runtime_desc_t *desc)`
   - [x] Correct `unit_header_t` with packed format in `.unit_header` ELF section
   - [x] All Synth Module API callbacks implemented
   - [x] Parameter mapping: drumlogue int32 (0-100) -> OSC 10-bit/enum
   - [x] Mono float -> stereo interleaved float in `unit_render`

3. **OSC adapter** (`drumlogue_osc_adapter.cc` / `.h`)
   - [x] Clean interface with no duplicate definitions
   - [x] Buffered rendering with `OSC_NATIVE_BLOCK_SIZE` support
   - [x] Q31 <-> float conversion
   - [x] Pitch management (note on/off, pitch bend)
   - [x] `user_osc_param_t` state management

4. **Build system**
   - [x] `makefile.inc` — drumlogue toolchain, flags, include paths, shared library
   - [x] `Makefile` — drumlogue in build loop, fixed nts-1 platform name
   - [x] All 15 `.mk` files — wrapper auto-inclusion + `OSC_NATIVE_BLOCK_SIZE` defines

5. **Fixes applied to pre-existing code**
   - [x] Deleted root `userosc.h` that was shadowing SDK headers for prologue builds
   - [x] Fixed `unit_init` signature (was `void(uint32_t, uint32_t)`)
   - [x] Fixed `unit_header_t` format (was custom struct, now SDK-matching packed struct)
   - [x] Removed duplicate function definitions in adapter
   - [x] Added buffered rendering to handle block size mismatch
   - [x] Fixed `nutekt-digital` -> `nts-1` platform name in Makefile

### Remaining

- [ ] Test ARM cross-compilation (`PLATFORM=drumlogue make`)
- [ ] Verify `.drmlgunit` ELF output (file type, `.unit_header` section)
- [ ] Decide whether to remove or keep the 14 redundant `manifest_drumlogue_*.json` files
- [ ] Test on drumlogue hardware (if available)

## Stage 2: Unit Tests for Callbacks (DONE)

### Completed

- [x] 55 callback tests in `test_drumlogue_callbacks.cc` with mock OSC_* functions
- [x] `unit_init` — null desc, bad target, bad API, bad samplerate, success
- [x] `unit_render` — stereo interleave, silence when suspended, resume after suspend
- [x] `unit_note_on` / `unit_note_off` — pitch encoding, delegation, all-note-off
- [x] `unit_set_param_value` — scaling for all 6 params, out-of-range guard, get/set roundtrip
- [x] `unit_teardown` — prevents further calls, adapter teardown
- [x] Buffered rendering — exact block, partial, multi-block, accumulation, 96-frame requests
- [x] Pitch bend — neutral, up, down
- [x] Shape LFO — Q31 conversion, channel pressure mapping
- [x] Q31/Float — zero, positive, negative conversion accuracy
- [x] Edge cases — null output, uninitialized render

### Bugs Found & Fixed

- Forward reference: `s_render_rd`/`s_render_avail` used before declaration
- Buffer not flushed on init: leftover samples across init/teardown cycles
- Missing `osc_adapter_teardown()`: adapter stayed initialized after teardown
- `float_to_q31(1.0)` overflow: `1.0 * 2^31` exceeds `INT32_MAX`, now clamped

### Build

```bash
make test  # Runs 55 callback tests (host compiler, no ARM required)
```

## Stage 3: NEON Optimizations (DONE)

### Completed

- [x] Q31 -> float conversion loop: `q31_buf_to_float()` in adapter
  - NEON: `vld1q_s32` -> `vcvtq_f32_s32` -> `vmulq_f32` -> `vst1q_f32` (4 samples/iter)
  - Both block sizes (24, 32) are multiples of 4 — no tail needed
- [x] Mono -> stereo interleaving: `mono_to_stereo()` in wrapper
  - NEON: `vld1q_f32` -> `vst2q_f32` interleaved store (4 samples -> 8 floats/iter)
  - Tail loop handles non-multiple-of-4 frame counts
- [x] All NEON code gated by `#ifdef __ARM_NEON` with scalar fallback
- [x] Existing 55 tests pass (scalar fallback on x86 host)

### Not optimized (analysis)

- Buffer copy (`memcpy` in `osc_adapter_render`): already optimal via libc
- DSP engine inner loops: inside unmodified oscillator source code, out of scope
- `float_to_q31`: called once per shape_lfo update, not a hot path

## Stage 4: SDK Structure Alignment & Sound Production Test (DONE)

### Completed

- [x] `header.c` — unit_header extracted to separate C file per SDK convention
- [x] `config.mk` — SDK-compatible project configuration file
- [x] `unit_init` target validation now uses `unit_header.target` (matches Braids port pattern)
- [x] All 15 `.mk` files updated to include `header.c` for drumlogue builds
- [x] Plaits source code (eurorack/plaits/) confirmed present with all 16 engine files
- [x] stmlib submodule initialized (was empty)
- [x] Sound production test (`test_sound_production.cc`) — 9 tests with real VirtualAnalogEngine
  - Engine init, render before note-on, note-on produces audio, stereo output
  - Different notes produce different pitch, param changes affect output
  - Amplitude within valid range, continuous rendering, note-off stability

### Build

```bash
make test-sound  # Runs 9 sound production tests (links real Plaits engine)
make test-all    # Runs all 64 tests (55 callback + 9 sound production)
```

## Stage 5: Verification of Optimized Output

- [ ] Bit-exact comparison: NEON vs scalar output for Q31/float conversion
- [ ] Near-exact comparison: full render output (within floating-point tolerance)
- [ ] Performance benchmarks on actual Cortex-A7 (cycles per frame)
- [ ] Audio quality listening tests on drumlogue hardware

## Future: Template Abstraction

Once the drumlogue port is stable and tested:

- [ ] Extract the wrapper pattern into a reusable template/framework
- [ ] Document how to add a new platform (what to implement, what to configure)
- [ ] Enable porting additional Mutable Instruments modules (Clouds, Rings, etc.)
- [ ] Enable porting from other open-source DSP projects

## File Inventory

### Core Files

| File | Status | Purpose |
|---|---|---|
| `drumlogue/runtime.h` | New | SDK type definitions |
| `drumlogue/unit.h` | New | SDK function declarations |
| `drumlogue/attributes.h` | New | Compiler attribute macros |
| `drumlogue/userosc.h` | New | OSC API compatibility layer |
| `header.c` | New (Stage 4) | SDK-style unit_header in `.unit_header` ELF section |
| `config.mk` | New (Stage 4) | SDK-compatible project configuration |
| `drumlogue_unit_wrapper.cc` | Rewritten | Synth Module API implementation (NEON mono->stereo) |
| `drumlogue_osc_adapter.cc` | Rewritten | OSC API bridge + buffered rendering (NEON Q31->float) |
| `drumlogue_osc_adapter.h` | Rewritten | Adapter interface |
| `makefile.inc` | Modified | Build system (include paths, toolchain) |
| `Makefile` | Modified | Top-level build, test targets |
| `osc_*.mk` (15 files) | Modified | Per-oscillator build config + header.c |

### Test Files

| File | Tests | Description |
|---|---|---|
| `test_drumlogue_callbacks.cc` | 55 | Mock-based callback chain tests |
| `test_sound_production.cc` | 9 | Real Plaits engine sound production |

### Oscillator Source (Unchanged)

| File | Block Size | Description |
|---|---|---|
| `macro-oscillator2.cc` | 24 | Plaits engine (12 oscillators) |
| `modal-strike.cc` | 32 | Elements resonator (3 variants) |

### Documentation

| File | Status |
|---|---|
| `DRUMLOGUE_PORT.md` | Updated |
| `DRUMLOGUE_VERIFICATION.md` | Updated |
| `TODO_DRUMLOGUE_PORT.md` | Updated (this file) |

### Legacy (Possibly Redundant)

| File | Notes |
|---|---|
| `manifest_drumlogue_*.json` (14) | Created for older approach; drumlogue uses embedded `unit_header_t` in ELF |
| `userosc.h` (root) | Deleted — was shadowing SDK |

---
**Last Updated**: 2026-02-16
**Branch**: claude/prologue-to-drumlogue-port-OZPcA
**Status**: Stages 1-4 complete. 64 tests passing. Awaiting ARM cross-compilation and hardware testing.
