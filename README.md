Eurorack Oscillators for Korg prologue, minilogue xd, Nu:tekt NTS-1, and drumlogue
=================================

Ports of some of Mutable Instruments (tm) oscillators to the Korg "logue" multi-engine.

**Platforms supported:**
- Korg prologue
- Korg minilogue xd
- Korg Nu:tekt NTS-1
- **Korg drumlogue** (synth module support)

See [releases](https://github.com/peterall/eurorack-prologue/releases) for latest binaries.

Oscillators
====

Macro Oscillator 2 (based on Plaits)
----

| Name | Engine | Description | `Shape` | `Shift-shape` |
|--|--|--|--|--|
| `mo2_va` | VirtualAnalog | Classic analog waveforms | Shape | Pulse width |
| `mo2_wsh` | Waveshaping | Waveshaping synthesis | Amount | Waveform |
| `mo2_fm` | FM | Two operator FM synthesis | Modulation index | Frequency ratio |
| `mo2_grn` | Grain | Granular formant synthesis | Frequency ratio | Formant frequency |
| `mo2_add` | Additive | Additive synthesis | Index of prominent harmonic | Bump shape |
| `mo2_wta`\* | Wavetable A | Additive (2x sine, quadratic, comb) | Row index | Column index |
| `mo2_wtb`\* | Wavetable B | Additive (pair, triangle stack, 2x drawbar) | Row index | Column index |
| `mo2_wtc`\* | Wavetable C | Formantish (trisaw, sawtri, burst, bandpass formant) | Row index | Column index |
| `mo2_wtd`\* | Wavetable D | Formantish (formant, digi_formant, pulse, sine power) | Row index | Column index |
| `mo2_wte`\* | Wavetable E | Braids (male, choir, digi, drone) | Row index | Column index |
| `mo2_wtf`\* | Wavetable F | Braids (metal, fant, 2x unknown) | Row index | Column index |
| `mo2_string` | String | Inharmonic string physical modelling | Decay | Brightness |

\* Due to the 32k size constraint on prologue/minilogue xd/NTS-1, the Wavetable oscillator is split into 6 oscillators of 8 rows (scannable by Shape) by 4 columns (scannable by Shift-shape). This constraint does not apply on drumlogue.

### Plaits Parameters

**On prologue / minilogue xd / NTS-1:**

In the Multi-engine menu you can find additional parameters for the oscillators.

`Parameter 1` is oscillator specific and controls whichever parameter is not mapped to `Shape` or `Shift-shape`.

`Parameter 2` sets the mix between the oscillator `out` and `aux`.

`LFO Target` sets the Shape LFO target, see LFO2 table below.

`LFO2 Rate` is rate of LFO2.

`LFO2 Int` is intensity (depth) of LFO2.

`LFO2 Target` sets the target for LFO2 according to the table below.

**On drumlogue** (12 params):

| # | Name | Description |
|---|------|-------------|
| 0 | Base Note | MIDI note for trigger pad (0-127, default C4) |
| 1 | Shape | Primary timbre control (0-100%) |
| 2 | ShiftShape | Secondary timbre / color control (0-100%) |
| 3 | Param 1 | Engine-specific parameter, bipolar (0-100%, center=50) |
| 4 | Param 2 | Engine-specific parameter (0-100%) |
| 5 | LFO Target | Which parameter the shape LFO modulates |
| 6 | LFO1 Shape | Waveform of the shape LFO |
| 7 | LFO2 Rate | Second LFO speed (0-100%) |
| 8 | LFO2 Depth | Second LFO amount (0-100%) |
| 9 | LFO2 Target | Which parameter LFO2 modulates |
| 10 | LFO2 Shape | Waveform of LFO2 |
| 11 | Gate Mode | Envelope/gate behavior (Trigger/Sustain/Continuous) |

### LFO2

The oscillator has a built-in additional cosine key-synced LFO which can modulate an internal parameter:

| LFO Target | Parameter     | Notes |
|------------|---------------|-------|
| 1          | `Shape`       |       |
| 2          | `Shift-shape` |       |
| 3          | `Parameter 1` | Not implemented for Wavetable oscillator |
| 4          | `Parameter 2` |       |
| 5          | `Pitch` |       |
| 6          | _reserved_ (Amplitude?) |       |
| 7          | `LFO2 Rate` |       |
| 8          | `LFO2 Int` |       |

For more information please read the excellent [Mutable Instruments Plaits documentation](https://mutable-instruments.net/modules/plaits/manual/).

### Plaits Tips

Many parameters 'neutral' settings are in center position, such as `va` Detune or `fm` Feedback, however the prologue defaults all parameters to the lowest value, hence get used to going into the menus and set the first parameter to 50% when instantiating the oscillator.

Modal Resonator (based on Elements)
----
*Physical modeling synthesis*

| Name | Modes | Limiter | Description |
|--|--|--|--|
| `modal_strike` | 24 | Yes | Strike exciter + modal resonator |
| `modal_strike_16_nolimit` | 16 | No | Lighter variant (fewer modes) |
| `modal_strike_24_nolimit` | 24 | No | Full modes, no limiter |
| `elements_full` | 64 | Yes | Full Elements DSP (64 modes, extended exciter range) — drumlogue only |

### Elements Parameters

**On prologue / minilogue xd / NTS-1:**

| Parameter               | Parameter             | LFO Target | Notes |
|-------------------------|-----------------------|------------|-------|
| `Shape` knob            | Resonator position    | 1 | Position where the mallet strikes, has a comb-filtering effect.  |
| `Shift` + `Shape` knob  | Resonator geometry    | 2 | Geometry and stiffness of resonator. Set to 25-30% for a nice tuned sound. |
| `Strength` menu         | Strike strength       | 3 | Mallet strength, high values causes the strike to bleed into the resonator output. |
| `Mallet` menu               | Strike mallet         | 4 | Type of mallet, over 70% is bouncing particles. |
| `Timbre` menu                | Strike timbre         | 5 | Brightness/speed of the excitation. |
| `Damping` menu              | Resonator damping     | 6 | The rate of energy dissipation in the resonator. High values cause long release effect. |
| `Brightness` menu           | Resonator brightness  | 7 | Muting of high frequencies |
| `LFO Target` menu           | multi-engine `Shape` LFO target |  | Sets which parameter is modulated by the `Shape` LFO (see LFO Target column)      |

**On drumlogue** (14 params):

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
| 10 | LFO2 Rate | Second LFO speed (0-100%) |
| 11 | LFO2 Depth | Second LFO amount (0-100%) |
| 12 | LFO2 Target | Which parameter LFO2 modulates |
| 13 | LFO2 Shape | Waveform of LFO2 |

**Elements LFO Target values:** 0=Position, 1=Geometry, 2=Strength, 3=Mallet, 4=Timbre, 5=Damping, 6=Brightness, 7=LFO2Freq, 8=LFO2Depth

**Elements LFO2 Target values:** 0=Position, 1=Geometry, 2=Strength, 3=Mallet, 4=Timbre, 5=Damping, 6=Brightness

For more information please read the excellent [Mutable Instruments Elements documentation](https://mutable-instruments.net/modules/elements/manual/).

### Elements Tips

*When you first select the oscillator it will make no sound, all parameters are at 0%!* Increase the `Strength` and `Damping` parameters until you start hearing something.

Try a nice pluck:

| Parameter           |  Value |
|---------------------|--------|
| `Shape`             | 50%    |
| `Shift` + `Shape`   | 30%    |
| `Strength`          | 90%    |
| `Mallet`            | 45%    |
| `Timbre`            | 45%    |
| `Damping`           | 70%    |
| `Brightness`        | 45%    |

### Limitations (prologue / minilogue xd / NTS-1)

Due to compute and memory (32K!) limitations in the prologue multi-engine quite a few short-cuts had to be taken:

* Only the Strike exciter is used
* Sample-player and Granular sample-player mallet-modes did not fit in memory
* Resonator filter bank is reduced to 24+2 filters from 52+8
* Resonator filters are recomputed one per block instead all-ish every block
* Samplerate is 24KHz vs 32KHz in Elements

*Sounds pretty great IMO but go buy Elements for the real experience!*

The drumlogue platform has significantly more CPU and memory, so `elements_full` runs with 64 resonator modes and the full exciter range.

Rings Resonator (based on Rings)
----
*Resonant string and modal synthesis*

Based on Mutable Instruments **Rings**, a resonator module with six distinct models. Produces tuned resonant sounds from plucked strings to metallic reverberant tones.

| Name | Models | Description |
|--|--|--|
| `rings` | 6 | Resonant strings, bells, reverberant metallic sounds |

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

For more information please read the excellent [Mutable Instruments Rings documentation](https://mutable-instruments.net/modules/rings/manual/).

Clouds (based on Clouds)
----
*Granular audio processor*

Based on Mutable Instruments **Clouds**, a granular audio processor with four playback modes. Transforms incoming audio or internal oscillator into textural clouds, time-stretched drones, delays, and spectral freezes.

| Name | Modes | Description |
|--|--|--|
| `clouds` | 4 | Granular textures, stretches, spectral freezes |

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

For more information please read the excellent [Mutable Instruments Clouds documentation](https://mutable-instruments.net/modules/clouds/manual/).

Mussola (Vocal Synthesis)
----
*Abstract vocal synthesizer*

A vocal synthesis engine combining three speech synthesis models (Naive formant, SAM phoneme, LPC speech) with unison voicing and stereo spread. Produces vowel sounds, robotic speech, choir-like textures, and vocal percussion.

| Name | Models | Description |
|--|--|--|
| `mussola` | 4 | Vocal tones, formant sweeps, robotic speech, choir |

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

Base Note
----

*(Drumlogue only)*

The **Base Note** parameter sets the MIDI note played when the drumlogue's trigger pad fires (`unit_gate_on`). This allows tuning the oscillator to a specific pitch without external MIDI. Default is 60 (middle C). MIDI note-on events (`unit_note_on`) still play the received note directly, ignoring Base Note.

Drumlogue Architecture
====

**Same source code, different HW APIs.** The oscillator source files (`macro-oscillator2.cc`, `modal-strike.cc`, `rings-resonator.cc`, `clouds-granular.cc`, `mussola.cc`) compile unchanged for both prologue-class platforms and drumlogue. A thin wrapper layer translates between the drumlogue Synth Module API (logue-sdk v2.0) and the User Oscillator API (logue-sdk v1.x) that the oscillators were written against.

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

### Key Differences: Prologue vs Drumlogue

| Aspect | Prologue / Minilogue XD / NTS-1 | Drumlogue |
|---|---|---|
| **SDK version** | logue-sdk v1.x (User OSC API) | logue-sdk v2.0 (Synth Module API) |
| **Architecture** | ARM Cortex-M4 (Thumb, bare metal) | ARM Cortex-A7 (ARM, Linux-based) |
| **FPU** | FPv4-SP-D16 (single precision) | NEON VFPv4 (SIMD + double) |
| **Audio format** | `int32_t *` Q31 fixed-point, mono | `float *` 32-bit IEEE 754, stereo interleaved |
| **Sample rate** | 48 kHz | 48 kHz |
| **Build output** | `.prlgunit` / `.mnlgxdunit` / `.ntkdigunit` | `.drmlgunit` (ELF shared library) |
| **Toolchain** | `arm-none-eabi-gcc` | `arm-linux-gnueabihf-gcc` |
| **Max params** | 6 | 24 |

Known Issues
====

* The prologue Sound Librarian tends to timeout when transferring the user oscillator, however the transfer is still complete. Try adding the user oscillator one at a time and _Send All_ / _Receive All_ for each oscillator.

* There's been [many reports](https://github.com/peterall/eurorack-prologue/issues/2) that the Modal Resonator oscillator doesn't produce any sound. I've included a few versions which lower CPU usage which may yield better results. On my prologue there's been cases where I've had issues after a factory-reset where the oscillator wouldn't produce sound. Installing _sequentially in the same oscillator slot_ the lightest CPU version `osc_modal_strike_16_nolimit` (16 filters and removed limiter), followed by `osc_modal_strike_24_nolimit` followed by `osc_modal_strike` resolved the issue for me. _Your milage may vary_.

* When first selecting the oscillator in the multi-engine, all values default to their minimum values, however the display seems to default to 0. For bipolar values it means the display might still show 0% while internally in the oscillator the value is -100%.

Building
====

**Prerequisites:**
* Checkout the repo (including subrepos): `git clone --recursive https://github.com/fedemone/eurorack-prologue.git`
* Follow the toolchain installation instructions in the `logue-sdk`
* Make sure you have the `jq` tool installed (`brew install jq` on macOS)
* Docker installed (for drumlogue cross-compilation)

**Building for all platforms:**
```bash
make
```

This will build all oscillators for prologue, minilogue-xd, nutekt-digital, and drumlogue platforms.

**Building for specific platform:**
```bash
# Build all oscillators for drumlogue only
PLATFORM=drumlogue make

# Build specific oscillator for drumlogue
PLATFORM=drumlogue make -f osc_va.mk

# Build specific oscillator for prologue
PLATFORM=prologue make -f osc_fm.mk
```

**Drumlogue-specific build (via Docker):**
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

**Build outputs:**
- `.prlgunit` files for prologue
- `.mnlgxdunit` files for minilogue-xd
- `.ntkdigunit` files for nutekt-digital
- `.drmlgunit` files for drumlogue

**Installation:**

*Prologue / minilogue xd / NTS-1:* Use the Korg Sound Librarian to transfer `.prlgunit` / `.mnlgxdunit` / `.ntkdigunit` files.

*Drumlogue:*
1. Power on drumlogue in USB mass storage mode
2. Place `.drmlgunit` files in the `Units/Synths/` directory
3. Restart drumlogue to load the new synth units
4. Units will be loaded in alphabetical order

**Note:** Only tested on macOS, but should work on Linux with appropriate toolchains installed.

Acknowledgements
====
*All credit to Emilie Gillet for her amazing modules!*
