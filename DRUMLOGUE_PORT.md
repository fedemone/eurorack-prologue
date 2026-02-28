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
  - Per-oscillator param mapping via compile-time #ifdef
  - Base Note parameter for gate trigger tuning
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

## Oscillators

### Plaits-based (macro-oscillator2.cc)

| Unit | Engine | Description |
|---|---|---|
| mo2_va | VirtualAnalogEngine | Classic analog waveforms |
| mo2_wsh | WaveshapingEngine | Waveshaping synthesis |
| mo2_fm | FMEngine | FM synthesis |
| mo2_grn | GrainEngine | Granular synthesis |
| mo2_add | AdditiveEngine | Additive synthesis |
| mo2_string | StringEngine | Physical modelling strings |
| mo2_wta..wtf | WavetableEngine | Wavetable A through F |

### Elements-based (modal-strike.cc)

| Unit | Modes | Limiter | Description |
|---|---|---|---|
| modal_strike | 24 | Yes | Strike exciter + modal resonator |
| modal_strike_16_nolimit | 16 | No | Lighter variant (fewer modes) |
| modal_strike_24_nolimit | 24 | No | Full modes, no limiter |
| elements_full | 64 | Yes | Full Elements DSP (64 modes, extended exciter range) |

## Per-Oscillator Parameters

The drumlogue unit header exposes different parameters depending on the oscillator
type, using compile-time `#ifdef` in `header.c`. This ensures each oscillator's
parameters are correctly labeled and mapped.

### Plaits Parameters (9 params)

| ID | Name | Type | Range | Default | OSC Mapping |
|---|---|---|---|---|---|
| 0 | Shape | percent | 0-100 | 0 | `k_user_osc_param_shape` (0-1023) |
| 1 | ShiftShape | percent | 0-100 | 0 | `k_user_osc_param_shiftshape` (0-1023) |
| 2 | Param 1 | percent | 0-100 | 50 | `k_user_osc_param_id1` (0-200, bipolar) |
| 3 | Param 2 | percent | 0-100 | 50 | `k_user_osc_param_id2` (0-100) |
| 4 | Base Note | midi_note | 0-127 | 60 | Stored in wrapper (used by gate trigger) |
| 5 | LFO Target | enum | 0-7 | 0 | `k_user_osc_param_id3` |
| 6 | LFO2 Rate | percent | 0-100 | 0 | `k_user_osc_param_id4` |
| 7 | LFO2 Depth | percent | 0-100 | 0 | `k_user_osc_param_id5` |
| 8 | LFO2 Target | enum | 0-7 | 0 | `k_user_osc_param_id6` |

**LFO Target values:** 0=Shape, 1=ShiftShape, 2=Param1, 3=Param2, 4=Pitch, 5=Amplitude, 6=LFO2Freq, 7=LFO2Depth

**LFO2** is a second modulation source (cosine oscillator) that can modulate any of the
same targets as the shape LFO. Its rate and depth are user-controllable, and its target
is selectable via the LFO2 Target parameter.

### Elements Parameters (12 params)

| ID | Name | Type | Range | Default | OSC Mapping |
|---|---|---|---|---|---|
| 0 | Position | percent | 0-100 | 30 | `k_user_osc_param_shape` (0-1023) |
| 1 | Geometry | percent | 0-100 | 20 | `k_user_osc_param_shiftshape` (0-1023) |
| 2 | Strength | percent | 0-100 | 80 | `k_user_osc_param_id1` (0-100) |
| 3 | Mallet | percent | 0-100 | 50 | `k_user_osc_param_id2` (0-100) |
| 4 | Timbre | percent | 0-100 | 50 | `k_user_osc_param_id3` (0-100) |
| 5 | Damping | percent | 0-100 | 25 | `k_user_osc_param_id4` (0-100) |
| 6 | Brightness | percent | 0-100 | 50 | `k_user_osc_param_id5` (0-100) |
| 7 | Base Note | midi_note | 0-127 | 60 | Stored in wrapper (used by gate trigger) |
| 8 | LFO Target | enum | 0-8 | 0 | `k_user_osc_param_id6` |
| 9 | LFO2 Rate | percent | 0-100 | 0 | Custom OSC_PARAM index 8 |
| 10 | LFO2 Depth | percent | 0-100 | 0 | Custom OSC_PARAM index 9 |
| 11 | LFO2 Target | enum | 0-6 | 0 | Custom OSC_PARAM index 10 |

**LFO Target values:** 0=Position, 1=Geometry, 2=Strength, 3=Mallet, 4=Timbre, 5=Damping, 6=Brightness, 7=LFO2Freq, 8=LFO2Depth

**LFO2 Target values:** 0=Position, 1=Geometry, 2=Strength, 3=Mallet, 4=Timbre, 5=Damping, 6=Brightness

**LFO2** is a second modulation source (cosine oscillator) for Elements, mirroring the LFO2
system in macro-oscillator2.cc. Its rate and depth are user-controllable, and its target is
selectable via the LFO2 Target parameter. Cross-modulation: the shape LFO can target
LFO2Freq or LFO2Depth (values 7-8), and vice versa.

### Base Note

The **Base Note** parameter sets the MIDI note played when the drumlogue's trigger pad
fires (`unit_gate_on`). This allows tuning the oscillator to a specific pitch without
external MIDI. Default is 60 (middle C). MIDI note-on events (`unit_note_on`) still
play the received note directly, ignoring Base Note.

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
| `drumlogue_unit_wrapper.cc` | Synth Module API implementation, per-oscillator param mapping |
| `drumlogue_osc_adapter.cc` | OSC API bridge with buffered rendering |
| `drumlogue_osc_adapter.h` | Adapter interface header |
| `header.c` | Per-oscillator unit header with `#ifdef` param layouts |

### Build system

| File | Purpose |
|---|---|
| `generate_sdk_projects.sh` | Generates per-oscillator SDK project dirs under `drumlogue/` |
| `build_drumlogue.sh` | Docker-based build script (mounts repo root as `/workspace/`) |
| `Makefile` | Host-side build loop + test targets |

## Building for Drumlogue

### Prerequisites
1. Clone with submodules:
   ```bash
   git clone --recursive https://github.com/fedemone/eurorack-prologue.git
   cd eurorack-prologue
   git checkout drumlogue-port
   ```

2. Docker installed (for SDK cross-compilation)

### Build Commands

```bash
# Generate SDK project directories (first time or after changes)
./generate_sdk_projects.sh

# Build all oscillators via Docker
./build_drumlogue.sh

# Build a specific oscillator
./build_drumlogue.sh mo2_va

# Interactive Docker shell
./build_drumlogue.sh --interactive

# Run host-side tests (no Docker/ARM needed)
make test-all
```

### Output
`.drmlgunit` files collected in the `output/` directory.

### Installation on Drumlogue
1. Power on drumlogue in USB mass storage mode
2. Place `.drmlgunit` files in the `Units/Synths/` directory
3. Restart drumlogue to load the new synth units

## Development Progress

### Completed

- **Stage 1: Wrapper Layer** - `drumlogue_unit_wrapper.cc` + `drumlogue_osc_adapter.cc`
  bridge the Synth Module API to the OSC API. Oscillator sources compile unchanged.

- **Stage 2: Unit Tests** - 58 callback tests + 9 sound production tests verify the
  full chain: `unit_*` -> `osc_adapter_*` -> `OSC_*`. Covers init validation, note
  events, pitch bend, parameter mapping, Q31/float conversion, buffered rendering,
  stereo interleaving, lifecycle, and edge cases.

- **Stage 3: Docker Build Integration** - `generate_sdk_projects.sh` creates per-oscillator
  project directories. `build_drumlogue.sh` runs the official logue-sdk Docker container.
  Repo root is mounted as `/workspace/` so all source files are accessible. Build output
  detection checks for `.drmlgunit` files (not exit codes, since the container always
  returns 0).

- **Stage 4: Windows/WSL Compatibility** - Build scripts work on Windows Git Bash and
  WSL. Makefile `chown` calls are disabled on Windows mounts. SDK Makefile paths are
  rewritten via `sed` to use `$(PROJROOT)/logue-sdk/platform/drumlogue/common`.

- **Stage 5: Per-Oscillator Parameters** - `header.c` uses `#ifdef ELEMENTS_RESONATOR_MODES`
  to provide different parameter layouts for Plaits vs Elements oscillators. Fixes the
  previous shared header where param labels were wrong for Elements and some params
  (Brightness, LFO Target) were inaccessible.

- **Stage 6: Base Note Parameter** - User-editable MIDI note (0-127) stored in the
  wrapper. Used by `unit_gate_on` (trigger pad) to set the pitch. MIDI note-on events
  bypass this and use the received note directly.

- **Stage 7: LFO2 Exposure for Plaits** - LFO2 Depth and LFO2 Target params are now
  exposed in the drumlogue header and mapped through the wrapper. Previously these params
  were unreachable (default 0), making the LFO2 system in macro-oscillator2.cc non-functional.

- **Elements Full Variant** - `elements_full` project with 64 resonator modes, limiter,
  and extended exciter range (granular sample player to particles).

- **Stage 8: LFO2 for Elements** - Added a `CosineOscillator` LFO2 to `modal-strike.cc`
  under `#ifdef ELEMENTS_LFO2`, mirroring the LFO2 system in macro-oscillator2.cc.
  Three new drumlogue params (LFO2 Rate, LFO2 Depth, LFO2 Target) are routed through
  custom OSC_PARAM indices (8, 9, 10) since all standard param IDs are in use. Both
  the shape LFO and LFO2 can target any of the 7 sound parameters, plus cross-modulate
  each other's frequency and depth. All Elements variants (modal_strike, 16_nolimit,
  24_nolimit, elements_full) have LFO2 enabled via `-DELEMENTS_LFO2`.

### Next Steps

#### Study: Chord Mode (Elements String Resonator)

The original Elements firmware includes a 5-string chord resonator model with 11
voicings (Open, Dense, Octave, Detuned, Fifth, etc.). This is not present in the
current `modal-strike.cc` port, which only uses the modal resonator.

**What's needed to add chord mode:**
- Port `elements/dsp/voice.cc` chord logic (`chords[11][5]` table, 5x `String` objects)
- Add `ResonatorModel` selector param (Modal / String / Strings)
- Add chord voicing selector param (0-10)
- Memory: 5 `String` objects require ~5x the memory of a single modal resonator
- CPU: 5 parallel string computations may exceed real-time budget on some settings
- Consider a dedicated `elements_chord` project variant to avoid bloating other units

**Key code reference:** Original `voice.cc` at
[peterall/eurorack@58b9125](https://github.com/peterall/eurorack/blob/58b9125a8c10ff8d496dfe8a12fb8c35a374d96e/elements/dsp/voice.cc)

## Notes

- Drumlogue has significantly more CPU power and memory than Cortex-M4 platforms
- The 32K size constraint does not apply on drumlogue
- Sample rate is 48 kHz (same as all logue platforms)
- The logue-sdk submodule is pinned to v1.x (Sept 2019); drumlogue headers are provided locally
- All 15 oscillator variants (12 Plaits + 3 Elements + 1 Elements Full) are generated
  by `generate_sdk_projects.sh`

## References

- [Korg logue SDK](https://github.com/korginc/logue-sdk)
- [Drumlogue Platform (SDK v2.0)](https://github.com/korginc/logue-sdk/tree/master/platform/drumlogue)
- [Original Eurorack-Prologue](https://github.com/peterall/eurorack-prologue)
- [Original Elements Sources](https://github.com/peterall/eurorack/tree/58b9125a8c10ff8d496dfe8a12fb8c35a374d96e/elements)
