# Drumlogue Port of Eurorack Oscillators

This project ports Mutable Instruments oscillator engines to the **Korg drumlogue**
synthesizer platform. 19 oscillator variants are available, covering subtractive,
FM, granular, physical modelling, spectral, and vocal synthesis.

**Project Status**: Concluded. 347 tests passing across 7 test suites.

---

## Quick Start

### Installation

1. Build the `.drmlgunit` files (see [Building](#building-for-drumlogue) below)
2. Power on your drumlogue and connect via USB (mass storage mode)
3. Copy `.drmlgunit` files to the `Units/Synths/` directory
4. Restart the drumlogue — new synth units appear in the synth selection menu

### Usage

Each oscillator appears as a selectable synth unit on the drumlogue. Navigate
parameters using the drumlogue's 4-knob interface (parameters are organized in
pages of 4). Use the trigger pad or external MIDI to play notes.

**Base Note** (param 0 on all units) sets the pitch played by the drumlogue's
trigger pad. MIDI note-on events bypass this and play the received note directly.

---

## Oscillator Guide

### Plaits Engines (12 variants)

These oscillators are based on Mutable Instruments **Plaits**, a macro-oscillator
offering 16 synthesis engines in the original Eurorack module. Each drumlogue unit
exposes one engine with full parameter control.

| Unit File | Display Name | What It Sounds Like |
|-----------|-------------|---------------------|
| `mo2_va.drmlgunit` | VirtAnalog | Classic analog — saw, square, pulse width. Warm, fat tones good for basses and leads. |
| `mo2_wsh.drmlgunit` | Waveshaper | Sine through various waveshaping functions. Harmonically rich, from subtle warmth to aggressive distortion. |
| `mo2_fm.drmlgunit` | FM | Two-operator FM synthesis. Metallic bells, electric pianos, bass. Shape controls modulation index. |
| `mo2_grn.drmlgunit` | Granular | Granular cloud of short sound particles. Textural, evolving pads and atmospheric drones. |
| `mo2_add.drmlgunit` | Additive | Harmonic series with individual partial control. Organ-like tones, vowel-ish resonances. |
| `mo2_string.drmlgunit` | String | Karplus-Strong physical model. Plucked strings, harps, metallic resonances. |
| `mo2_wta.drmlgunit` | Wavetable A | Wavetable scanning through bank A. Evolving timbres, PWM-like sweeps. |
| `mo2_wtb.drmlgunit` | Wavetable B | Wavetable bank B. Different wave shapes and timbral territory. |
| `mo2_wtc.drmlgunit` | Wavetable C | Wavetable bank C. |
| `mo2_wtd.drmlgunit` | Wavetable D | Wavetable bank D. |
| `mo2_wte.drmlgunit` | Wavetable E | Wavetable bank E. |
| `mo2_wtf.drmlgunit` | Wavetable F | Wavetable bank F. |

**Parameters (13 per unit):**

| # | Name | Description |
|---|------|-------------|
| 0 | Base Note | MIDI note for trigger pad (0-127, default C4) |
| 1 | Shape | Primary timbre control (0-100%) |
| 2 | ShiftShape | Secondary timbre / color control (0-100%) |
| 3 | Param 1 | Engine-specific parameter, bipolar (0-100%, center=50) |
| 4 | Param 2 | Engine-specific parameter (0-100%) |
| 5 | LFO Target | Which parameter the shape LFO modulates |
| 6 | LFO1 Shape | Waveform of the shape LFO |
| 7 | LFO1 Rate | Shape LFO speed (0-100%) |
| 8 | LFO2 Rate | Second LFO speed (0-100%) |
| 9 | LFO2 Depth | Second LFO amount (0-100%) |
| 10 | LFO2 Target | Which parameter LFO2 modulates |
| 11 | LFO2 Shape | Waveform of LFO2 |
| 12 | Gate Mode | Envelope/gate behavior (Trigger/Sustain/Continuous) |

**LFO Target values**: 0=Shape, 1=ShiftShape, 2=Param1, 3=Param2, 4=Pitch, 5=Amplitude, 6=LFO2Freq, 7=LFO2Depth

**Sound design tips:**
- Start with Shape and ShiftShape for broad timbre changes
- Use Param 1 at center (50) as the neutral point — moving below or above sweeps opposite directions
- Set LFO Target to Shape (0) and increase LFO2 Rate/Depth for evolving textures
- Gate Mode: Trigger = percussive hits, Sustain = held notes, Continuous = drone

### Elements Engines (4 variants)

Based on Mutable Instruments **Elements**, a modal synthesis voice combining an
exciter (strike/bow/blow) with a bank of tuned bandpass resonators. Produces
realistic percussive and resonant sounds — bells, chimes, bars, plates, and bowed
metallic tones.

| Unit File | Display Name | Modes | Limiter | Character |
|-----------|-------------|-------|---------|-----------|
| `modal_strike.drmlgunit` | ModalStrike | 24 | Yes | Standard modal — rich resonance, safe output levels |
| `modal_strike_16_nolimit.drmlgunit` | Strike16 | 16 | No | Lighter CPU, fewer partials, more aggressive dynamics |
| `modal_strike_24_nolimit.drmlgunit` | Strike24 | 24 | No | Full resonance, raw dynamics (can clip) |
| `elements_full.drmlgunit` | ElementsFull | 64 | Yes | Maximum resonance detail, extended exciter range |

**Parameters (15 per unit):**

| # | Name | Description |
|---|------|-------------|
| 0 | Base Note | MIDI note for trigger pad (0-127, default C4) |
| 1 | Position | Where the exciter hits the resonator (0-100%) |
| 2 | Geometry | Modal density / resonator shape (0-100%) |
| 3 | Strength | Strike exciter level (0-100%) |
| 4 | Mallet | Exciter type: mallet (0) through particles (100) |
| 5 | Timbre | Exciter brightness / spectral content (0-100%) |
| 6 | Damping | How quickly resonances decay (0-100%) |
| 7 | Brightness | Resonator spectral tilt — dark to bright (0-100%) |
| 8 | LFO Target | Which parameter the shape LFO modulates |
| 9 | LFO1 Shape | Waveform of the shape LFO |
| 10 | LFO1 Rate | Shape LFO speed (0-100%) |
| 11 | LFO2 Rate | Second LFO speed (0-100%) |
| 12 | LFO2 Depth | Second LFO amount (0-100%) |
| 13 | LFO2 Target | Which parameter LFO2 modulates |
| 14 | LFO2 Shape | Waveform of LFO2 |

**LFO Target values**: 0=Position, 1=Geometry, 2=Strength, 3=Mallet, 4=Timbre, 5=Damping, 6=Brightness, 7=LFO2Freq, 8=LFO2Depth

**Sound design tips:**
- Position + Geometry define the fundamental character — sweep slowly for evolving tones
- Mallet at 0 = rubber mallet, at 50 = wooden stick, at 100 = granular particles
- Low Damping + high Brightness = ringing bells; high Damping = dull thuds
- ElementsFull with 64 modes gives the richest resonance but uses more CPU

### Rings (1 variant)

Based on Mutable Instruments **Rings**, a resonator module with six distinct models.
Produces tuned resonant sounds from plucked strings to metallic reverberant tones.
Responds to both internal excitation and external triggers.

| Unit File | Display Name | Models | Character |
|-----------|-------------|--------|-----------|
| `rings.drmlgunit` | Rings | 6 | Resonant strings, bells, reverberant metallic sounds |

**The 6 resonator models:**

| # | Model | Description |
|---|-------|-------------|
| 0 | Modal | Bank of bandpass filters — bells, tubes, plates |
| 1 | Sympathetic String | Physical string with sympathetic resonance — sitar-like |
| 2 | Karplus-Strong | String with stiffness — plucked and hammered strings |
| 3 | FM Voice | FM synthesis with envelope follower — electric piano, DX tones |
| 4 | Sympathetic Quantized | Strings quantized to chords — strummed harmonics |
| 5 | String + Reverb | String with integrated reverb — ambient, ethereal |

**Parameters (8):**

| # | Name | Description |
|---|------|-------------|
| 0 | Base Note | MIDI note for trigger pad (0-127, default C4) |
| 1 | Position | Excitation point along the resonator (0-100%) |
| 2 | Structure | Frequency ratio / inharmonicity (0-100%) |
| 3 | Brightness | Spectral tilt — dark to bright (0-100%) |
| 4 | Damping | Resonance decay time (0-100%) |
| 5 | Chord | Chord voicing for sympathetic models (0-10) |
| 6 | Model | Resonator type (0-5, see table above) |
| 7 | Polyphony | Number of voices (1-4) |

**Sound design tips:**
- Start with Model 0 (Modal) and sweep Structure for metallic to harmonic
- Model 2 (Karplus-Strong) with low Damping makes excellent plucked bass/guitar
- Increase Polyphony for chordal playing (uses more CPU per voice)
- Chord parameter only affects sympathetic models (1 and 4)

### Clouds (1 variant)

Based on Mutable Instruments **Clouds**, a granular audio processor with four
playback modes. Transforms incoming audio or internal oscillator into textural
clouds, time-stretched drones, delays, and spectral freezes.

| Unit File | Display Name | Modes | Character |
|-----------|-------------|-------|-----------|
| `clouds.drmlgunit` | Clouds | 4 | Granular textures, stretches, spectral freezes |

**The 4 playback modes:**

| # | Mode | Description |
|---|------|-------------|
| 0 | Granular | Up to 40 overlapping grains — clouds of particles |
| 1 | Stretch (WSOLA) | Time-stretching with pitch tracking — frozen textures |
| 2 | Looping Delay | Pitch-shifted delay with sync — rhythmic effects |
| 3 | Spectral | Phase vocoder with FFT — spectral freezes, smearing |

**Parameters (16):**

| # | Name | Description |
|---|------|-------------|
| 0 | Base Note | MIDI note for trigger pad (0-127, default C4) |
| 1 | Position | Where in the buffer to read grains (0-100%) |
| 2 | Size | Grain length / buffer region (0-100%) |
| 3 | Density | Grain rate / overlap amount (0-100%) |
| 4 | Texture | Grain window shape / filtering (0-100%) |
| 5 | Pitch | Pitch transposition in semitones (-24 to +24, center=24) |
| 6 | Feedback | Amount of output fed back into input (0-100%) |
| 7 | Dry/Wet | Mix between dry input and processed output (0-100%) |
| 8 | Reverb | Built-in reverb amount (0-100%) |
| 9 | Freeze | Freeze the audio buffer (on/off) |
| 10 | Mode | Playback mode (0-3, see table above) |
| 11 | Quality | Audio quality / stereo mode (0-3) |
| 12 | SampleBank | Drumlogue sample bank to use as source (0-15) |
| 13 | SampleNum | Sample number within bank (0=internal, 1+=sample) |
| 14 | SmplStart | Sample start point in per-mille (0-1000 = 0-100%) |
| 15 | SmplEnd | Sample end point in per-mille (0-1000 = 0-100%) |

**Sound design tips:**
- Mode 0 (Granular) + small Size + high Density = shimmering cloud texture
- Mode 1 (Stretch) + Freeze on = infinite sustain of any sound
- Mode 3 (Spectral) is CPU-heavy but produces unique frozen-spectrum effects
- Use SampleBank/SampleNum to process drumlogue's built-in samples as grain source
- Feedback > 70% creates self-oscillating loops — use with care

### Mussola (1 variant)

A vocal synthesis engine combining three speech synthesis models (Naive formant,
SAM phoneme, LPC speech) with unison voicing and stereo spread. Produces vowel
sounds, robotic speech, choir-like textures, and vocal percussion.

| Unit File | Display Name | Models | Character |
|-----------|-------------|--------|-----------|
| `mussola.drmlgunit` | Mussola | 4 | Vocal tones, formant sweeps, robotic speech, choir |

**The 4 synthesis models:**

| # | Model | Description |
|---|-------|-------------|
| 0 | Naive | Simple formant synthesis — vowel-like tones, smooth |
| 1 | SAM | Software Automatic Mouth — classic 8-bit speech, robotic |
| 2 | LPC | Linear Predictive Coding — natural-sounding speech fragments |
| 3 | Blend | Crossfade between all three models |

**Parameters (16):**

| # | Name | Description |
|---|------|-------------|
| 0 | Base Note | MIDI note for trigger pad (0-127, default C4) |
| 1 | Phoneme | Vowel / phoneme selection (0-100%) |
| 2 | Timbre | Vocal register / formant shift (0-100%) |
| 3 | Harmonics | Harmonic richness / model blend (0-100%) |
| 4 | Morph | Morph within current model (0-100%) |
| 5 | Speed | LPC playback speed, 50=normal (0-100%) |
| 6 | Prosody | Prosody replay amount (0-100%) |
| 7 | Decay | Envelope decay time (0-100%) |
| 8 | Mix | Main/auxiliary output crossfade (0-100%) |
| 9 | Model | Synthesis model (0-3, see table above) |
| 10 | Gate Mode | Trigger/Sustain/Continuous |
| 11 | Voices | Unison voice count (1-4) |
| 12 | Detune | Unison detune amount (0-100%) |
| 13 | Spread | Stereo spread of unison voices (0-100%) |
| 14 | Gender | Formant shift — bass (0) to soprano (100), neutral at 50 |
| 15 | Attack | Envelope attack time (0-100%) |

**Sound design tips:**
- Sweep Phoneme slowly for vowel animation ("aah" to "eee" to "ooh")
- Model 1 (SAM) at low Phoneme values produces classic robot voice
- Voices=4 with Detune=30-50 and Spread=80 creates a wide stereo choir
- Gender shifts the formant spectrum — low values = bass voice, high = soprano

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
   git checkout claude/prologue-to-drumlogue-port-OZPcA
   ```

2. Docker installed (for SDK cross-compilation)

### Build Commands

```bash
# Generate SDK project directories (first time or after changes)
./generate_sdk_projects.sh

# Build Docker image (first time only)
cd logue-sdk && docker/build_image.sh && cd ..

# Build all oscillators via Docker
./build_drumlogue.sh

# Build a specific oscillator
./build_drumlogue.sh mo2_va

# Collect .drmlgunit files into output/
./build_drumlogue.sh --collect

# Interactive Docker shell
./build_drumlogue.sh --interactive
```

### Testing (No Docker Required)

```bash
make test          # 61 Plaits callback tests
make test-elements # 64 Elements callback tests
make test-rings    # 58 Rings callback tests
make test-clouds   # 66 Clouds callback tests
make test-clouds-sample  # 28 Clouds sample tests
make test-mussola  # 61 Mussola callback tests
make test-sound    # 9 sound production tests (real Plaits engine)
make test-all      # All 347 tests
make bench         # Render throughput benchmark
```

---

## Files

### Oscillator Sources (unchanged between platforms)

| File | Block Size | Engines |
|------|-----------|---------|
| `macro-oscillator2.cc` | 24 | 12 Plaits engines |
| `modal-strike.cc` | 32 | 4 Elements variants |
| `rings-resonator.cc` | 24 | 6 Rings resonator models |
| `clouds-granular.cc` | 32 | 4 Clouds playback modes |
| `mussola.cc` | 24 | 4 vocal synthesis models |

### Wrapper Layer

| File | Purpose |
|------|---------|
| `drumlogue_unit_wrapper.cc` | Synth Module API callbacks, param mapping, mono->stereo |
| `drumlogue_osc_adapter.cc` | OSC API bridge, buffered rendering, Q31/float conversion |
| `drumlogue_osc_adapter.h` | Adapter interface |
| `header.c` | Per-oscillator unit_header with parameter layouts |

### SDK Compatibility Headers (`drumlogue/` directory)

| File | Purpose |
|------|---------|
| `drumlogue/runtime.h` | `unit_runtime_desc_t`, `unit_header_t`, `unit_param_t`, constants |
| `drumlogue/unit.h` | `unit_*` function declarations |
| `drumlogue/attributes.h` | `__unit_callback`, `__unit_header` macros |
| `drumlogue/userosc.h` | OSC API types for drumlogue |

### Build System

| File | Purpose |
|------|---------|
| `Makefile` | Top-level build, test targets, packaging |
| `makefile.inc` | Toolchain, flags, include paths |
| `osc_*.mk` (19 files) | Per-oscillator build configuration |
| `config.mk` | SDK-compatible project configuration |
| `generate_sdk_projects.sh` | Creates SDK project directories |
| `build_drumlogue.sh` | Docker build wrapper |

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
**Last Updated**: 2026-03-11
**Status**: Project concluded. 19 oscillator variants, 347 tests passing.
