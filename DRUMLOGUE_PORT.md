# Drumlogue Port of Eurorack Oscillators

This project ports Mutable Instruments oscillator engines to the **Korg drumlogue**
synthesizer platform: 19 synth units, covering subtractive, FM, granular,
physical modelling, spectral and vocal synthesis, plus **CloudsFX**, a delay
effect unit that runs Clouds on the drumlogue's effect bus.

**Project Status**: Concluded. The test suites run on every push; see
*Building* in the [README](README.md#building) for the full list.

This document is the port's overview. Every unit's parameters, defaults and
tips are documented in one place, the [README](README.md), and the tables
below link to the right section.

---

## Quick Start

### Installation

1. Build the `.drmlgunit` files (see [Building](#building-for-drumlogue) below)
2. Power on your drumlogue and connect via USB (mass storage mode)
3. Copy the synth units to `Units/Synths/`, and `clouds_fx.drmlgunit` (the
   CloudsFX delay effect, not to be confused with the `clouds` synth) to
   `Units/DelayFXs/`
4. Restart the drumlogue — new synth units appear in the synth selection
   menu, and CloudsFX among the delay effects

The full installation notes are in the [README](README.md#installation).

### Usage

Each oscillator appears as a selectable synth unit on the drumlogue. Navigate
parameters using the drumlogue's 4-knob interface (parameters are organized in
pages of 4, in the order of each unit's parameter table). Use the trigger pad
or external MIDI to play notes.

**Base Note** (param 0 on all synth units) sets the pitch played by the
drumlogue's trigger pad. MIDI note-on events bypass this and play the received
note directly. CloudsFX, being an effect, has no Base Note.

---

## Units

| Unit File | Shown on the drumlogue as | Type | Parameters | Documentation |
|-----------|---------------------------|------|-----------:|---------------|
| `mo2_va.drmlgunit` | VirtAnalog | Synth | 15 | [Plaits](README.md#macro-oscillator-2-based-on-plaits) |
| `mo2_wsh.drmlgunit` | Waveshaper | Synth | 15 | [Plaits](README.md#macro-oscillator-2-based-on-plaits) |
| `mo2_fm.drmlgunit` | FM | Synth | 15 | [Plaits](README.md#macro-oscillator-2-based-on-plaits) |
| `mo2_grn.drmlgunit` | Granular | Synth | 15 | [Plaits](README.md#macro-oscillator-2-based-on-plaits) |
| `mo2_add.drmlgunit` | Additive | Synth | 15 | [Plaits](README.md#macro-oscillator-2-based-on-plaits) |
| `mo2_string.drmlgunit` | String | Synth | 15 | [Plaits](README.md#macro-oscillator-2-based-on-plaits) |
| `mo2_wta.drmlgunit` … `mo2_wtf.drmlgunit` | Wavetable A … Wavetable F | Synth | 15 | [Plaits](README.md#macro-oscillator-2-based-on-plaits) |
| `modal_strike.drmlgunit` | ModalStrike | Synth | 15 | [Elements](README.md#modal-resonator-based-on-elements) |
| `modal_strike_16_nolimit.drmlgunit` | Strike16 | Synth | 15 | [Elements](README.md#modal-resonator-based-on-elements) |
| `modal_strike_24_nolimit.drmlgunit` | Strike24 | Synth | 15 | [Elements](README.md#modal-resonator-based-on-elements) |
| `elements_full.drmlgunit` | ElementsFull | Synth | 15 | [Elements](README.md#modal-resonator-based-on-elements) |
| `rings.drmlgunit` | Rings | Synth | 21 | [Rings](README.md#rings-resonator-based-on-rings) |
| `clouds.drmlgunit` | Clouds | Synth | 16 | [Clouds](README.md#clouds-based-on-clouds) |
| `mussola.drmlgunit` | Mussola | Synth | 24 | [Mussola](README.md#mussola-vocal-synthesis) |
| `clouds_fx.drmlgunit` | CloudsFX | **Delay effect** | 11 | [CloudsFX](README.md#cloudsfx-clouds-as-an-insert-effect) |

All synth units go in `Units/Synths/`; `clouds_fx.drmlgunit` goes in
`Units/DelayFXs/`.

---

## Design Principle

**Same source code, different HW APIs.** The oscillator source files compile
unchanged for both prologue-class platforms and drumlogue. A thin wrapper layer
translates between the APIs:

```
Drumlogue Runtime (Linux, ARM Cortex-A7)
     |
     |  Synth Module API (unit_init, unit_render, unit_note_on, ...)
     v
drumlogue_unit_wrapper.cc
  - Implements all unit_* callbacks
  - Per-oscillator param mapping via compile-time #ifdef
  - Base Note parameter for gate trigger tuning
  - Converts mono float -> stereo interleaved float (NEON-optimized)
     |
     |  Adapter API (osc_adapter_init, osc_adapter_render, ...)
     v
drumlogue_osc_adapter.cc
  - Manages user_osc_param_t struct (pitch, shape_lfo)
  - Buffered rendering: calls OSC_CYCLE in native block sizes
  - Converts Q31 <-> float (NEON-optimized)
  - Handles pitch bend translation
     |
     |  User OSC API (OSC_INIT, OSC_CYCLE, OSC_NOTEON, ...)
     v
Oscillator source files (UNCHANGED)
```

## Key Differences: Prologue vs Drumlogue

| Aspect | Prologue / Minilogue XD / NTS-1 | Drumlogue |
|---|---|---|
| **SDK version** | logue-sdk v1.x (User OSC API) | logue-sdk v2.0 (Synth Module API) |
| **Architecture** | ARM Cortex-M4 (Thumb, bare metal) | ARM Cortex-A7 (ARM, Linux-based) |
| **FPU** | FPv4-SP-D16 (single precision) | NEON VFPv4 (SIMD + double) |
| **Audio format** | `int32_t *` Q31 fixed-point, mono | `float *` 32-bit IEEE 754, stereo interleaved |
| **Sample rate** | 48 kHz | 48 kHz |
| **Max params** | 6 | 24 |
| **Build output** | `.prlgunit` (ZIP: binary + manifest) | `.drmlgunit` (ELF shared library) |
| **Toolchain** | `arm-none-eabi-gcc` | `arm-linux-gnueabihf-gcc` |

---

## Building for Drumlogue

### Prerequisites

1. Clone with submodules:
   ```bash
   git clone --recursive https://github.com/fedemone/eurorack-prologue.git
   cd eurorack-prologue
   ```

2. Docker installed (for SDK cross-compilation)

### Build Commands

```bash
# Generate SDK project directories (first time or after changes)
./generate_sdk_projects.sh

# Build Docker image (first time only)
cd logue-sdk && docker/build_image.sh && cd ..

# Build all units via Docker
./build_drumlogue.sh

# Build a specific unit (the delay effect is built the same way)
./build_drumlogue.sh mo2_va
./build_drumlogue.sh clouds_fx

# Collect every .drmlgunit, clouds_fx included, into build/drumlogue/.
# One flat folder: clouds_fx.drmlgunit still has to go to Units/DelayFXs/
./build_drumlogue.sh --collect

# Interactive Docker shell
./build_drumlogue.sh --interactive
```

### Testing (No Docker Required)

```bash
make test          # Plaits callback tests
make test-elements # Elements callback tests
make test-rings    # Rings callback tests
make test-clouds   # Clouds callback tests
make test-clouds-sample  # Clouds sample playback tests
make test-clouds-fx      # CloudsFX: FX bus in -> engine -> out
make test-mussola  # Mussola callback tests
make test-mussola-words  # Mussola word banks, against the real SpeechEngine
make test-sound    # sound production tests (real Plaits engine)
make test-all      # all of the host suites
make bench         # Render throughput benchmark
```

The README's *Building* section lists every suite, including the
sanitizer and ARM/QEMU runs that CI performs on each push.

---

## Files

### Oscillator Sources (unchanged between platforms)

| File | Block Size | Engines |
|------|-----------|---------|
| `macro-oscillator2.cc` | 24 | 12 Plaits engines |
| `modal-strike.cc` | 32 | 4 Elements variants |
| `rings-resonator.cc` | 24 | 6 Rings resonator models |
| `clouds-granular.cc` | 32 | 4 Clouds playback modes |
| `clouds-fx.cc` | 32 | Clouds as a delay effect (CloudsFX); drumlogue only |
| `mussola.cc` | 24 | 4 vocal synthesis models |
| `mussola_words.cc` | — | Mussola's LPC word banks. Generated by `tools/generate_lpc_words.py`; do not edit |

### Wrapper Layer

| File | Purpose |
|------|---------|
| `drumlogue_unit_wrapper.cc` | Synth Module API callbacks, param mapping, mono->stereo |
| `drumlogue_delfx_wrapper.cc` | Delay-effect (delfx) API callbacks for CloudsFX: stereo in/out |
| `drumlogue_osc_adapter.cc` | OSC API bridge, buffered rendering, Q31/float conversion |
| `drumlogue_osc_adapter.h` | Adapter interface |
| `drumlogue_param_route.h` | Where each panel parameter goes, one table per unit |
| `drumlogue_guards.h` | What every unit owes the instrument: finite output, in-range parameters, its own target check |
| `clouds_src.h` | 48 <-> 32 kHz sample-rate conversion around the Clouds engine |
| `header.c` | Per-unit unit_header with parameter layouts |

### SDK Compatibility Headers (`drumlogue/` directory)

| File | Purpose |
|------|---------|
| `drumlogue/runtime.h` | `unit_runtime_desc_t`, `unit_header_t`, `unit_param_t`, constants |
| `drumlogue/unit.h` | `unit_*` function declarations |
| `drumlogue/attributes.h` | `__unit_callback`, `__unit_header` macros |
| `drumlogue/userosc.h` | OSC API types for drumlogue |
| `drumlogue/unit_exports.map` | Linker version script: exports only the callbacks the firmware looks up |

### Build System

| File | Purpose |
|------|---------|
| `Makefile` | Top-level build, test targets, packaging |
| `makefile.inc` | Toolchain, flags, include paths |
| `osc_*.mk` (19 files) | Per-oscillator build configuration (none for CloudsFX, which is drumlogue-only) |
| `config.mk` | SDK-compatible project configuration |
| `drumlogue/<project>/config.mk` | Per-unit drumlogue SDK project, `clouds_fx` included |
| `generate_sdk_projects.sh` | Creates SDK project directories |
| `build_drumlogue.sh` | Docker build wrapper |
| `eurorack-opt/` | Forked engine sources that shadow the `eurorack` submodule; see [eurorack-opt/README.md](eurorack-opt/README.md) |

---

## References

- [Korg logue SDK](https://github.com/korginc/logue-sdk)
- [Drumlogue Platform (SDK v2.0)](https://github.com/korginc/logue-sdk/tree/master/platform/drumlogue)
- [Original Eurorack-Prologue](https://github.com/peterall/eurorack-prologue)
- [Mutable Instruments Plaits](https://mutable-instruments.net/modules/plaits/)
- [Mutable Instruments Elements](https://mutable-instruments.net/modules/elements/)
- [Mutable Instruments Rings](https://mutable-instruments.net/modules/rings/)
- [Mutable Instruments Clouds](https://mutable-instruments.net/modules/clouds/)

---
**Last Updated**: 2026-10-13
**Status**: Project concluded. 19 synth units and 1 delay effect unit.
