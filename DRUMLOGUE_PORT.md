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
   git checkout main
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

### CPU: build at -O3

The drumlogue runs the synth, both send effects, the master effect and its own
drum engine on one audio thread. The SDK Makefile takes the optimisation level
from `OPTIM` and falls back to `-Os` when it is unset, so every unit here used
to ship size-optimised — and at `-Os`, Rings at polyphony 4 beside a reverb and
a master compressor was enough to make the drumlogue crackle and then go
silent. Every `config.mk` (and `generate_sdk_projects.sh`, which writes them)
now sets `OPTIM = -O3`.

Measured on the ARM builds under `qemu-arm`: instructions per 64-frame render,
header defaults, a note every 125 ms; and how far the `-O3` output is from the
`-Os` output over 3 s (the residue is float rounding taken in a different
order under `-ffast-math`; "identical" is bit for bit).

| Unit | `-Os` | `-O3` | saving | `-O3` vs `-Os` |
|------|------:|------:|-------:|----------------|
| rings | 43,400 | 12,400 | 71% | −77 dB |
| rings, polyphony 4 | 48,100 | 21,400 | 55% | −52 dB worst model (sympathetic strings) |
| clouds | 35,300 | 18,400 | 48% | −116 dB |
| clouds_fx | 51,100 | 18,900 | 63% | −109 dB |
| elements_full | 30,000 | 19,500 | 35% | −126 dB |
| modal_strike | 15,900 | 9,200 | 42% | −123 dB |
| modal_strike_16_nolimit | 12,900 | 6,700 | 48% | −124 dB |
| modal_strike_24_nolimit | 15,700 | 8,800 | 44% | −123 dB |
| mussola | 12,200 | 9,400 | 23% | −71 dB |
| mo2_add | 31,300 | 4,600 | 85% | −140 dB |
| mo2_fm | 15,800 | 3,600 | 77% | −155 dB |
| mo2_grn | 19,200 | 9,900 | 48% | −120 dB |
| mo2_string | 7,900 | 4,400 | 44% | −106 dB |
| mo2_va | 13,700 | 12,000 | 12% | −161 dB |
| mo2_wsh | 7,900 | 5,600 | 29% | identical |
| mo2_wta … mo2_wtf | 12,000 | 3,600 | 70% | identical |

The binaries grow by 4–45 KB (Rings 57 → 77 KB, Clouds 92 → 137 KB). The
instruction count is a proxy — it does not see cache or memory stalls — but
every unit runs fewer instructions for the same sound.

At `-Os` Rings also had a spike on top of that: setting **Polyphony**
re-initialises every string from inside the next audio callback, and the
size-optimised build cleared their delay lines one float at a time — one
render of 168,000 instructions, 3.5× its already heavy steady state, at the
exact moment the drumlogue crashed. At `-O3` that render is 23,800, no more
than an ordinary one.

#### Checking a build

`OPTIM` lives in `config.mk`, and upstream the SDK Makefile recompiles an
object only when its source, a header or the Makefile changes. So a tree that
was built before `OPTIM = -O3` arrived relinks its old `-Os` objects on the
next build and nothing says so. Two things now guard against that:

- the generated Makefiles make every object depend on `config.mk` too, so
  changing it rebuilds the unit;
- `header.c` stamps the level and the compiler into the binary:

  ```bash
  strings drumlogue/rings/rings.drmlgunit | grep -o "build:.*"
  # build: -O2/-O3 (speed), gcc ...                       <- good
  # build: -Os (size: rebuild clean for -O3), gcc ...      <- stale
  ```

  (A unit built before the stamp existed prints nothing.)

After pulling a change to the flags, a clean build is still the safe habit:
`./build_drumlogue.sh --clean` and then build.

### CPU: one block per render

What decides whether the drumlogue drops audio is the most expensive render,
not the average one: each 64-frame render has to finish inside its 1.33 ms
along with everything else on the audio thread. The Plaits units, Rings and
Mussola used to render 24-sample blocks, and 64 is 2.67 of those, so the
adapter rendered three blocks in two renders out of three and two in the
third. The expensive renders did 72 samples of work against a 64-sample
deadline and paid the engine's per-block overhead three times.

The Plaits units and Rings now render one 64-sample block per render
(`BLOCKSIZE` / `OSC_NATIVE_BLOCK_SIZE` 64 in `config.mk`). Every render then
costs the same, and the most expensive one costs less:

| Unit (header defaults unless noted) | worst render before | after | |
|------|------:|------:|---:|
| mo2_add | 26,636 | 19,453 | −27% |
| mo2_string | 14,863 | 12,244 | −18% |
| mo2_va | 25,523 | 21,282 | −17% |
| mo2_fm, mo2_grn, mo2_wsh | 24,346 – 27,123 | 20,886 – 23,280 | −14% |
| mo2_wta (… wtf) | 35,001 | 30,441 | −13% |
| rings | 102,911 | 86,111 | −16% |
| rings, Model 1, Polyphony 4 | 113,896 | 93,073 | −18% |
| rings, Model 5, Polyphony 4 | 118,721 | 99,268 | −16% |
| rings, Model 3 | 23,747 | 18,727 | −21% |
| rings, the render that applies a Polyphony change (Model 4) | 154,183 | 132,544 | −14% |

Mean cost falls by 2–18% as well, since the per-block overhead is paid 1 time
per render instead of 2.67.

Why this is a configuration change and not a rewrite:

- **Plaits** takes any block size — `plaits::kMaxBlockSize` is `BLOCKSIZE`,
  and this repository already ships the same engines at 16 on prologue and 64
  on NTS-1. Output level matches at every engine. The String engine's plucks
  differ one by one, because its excitation noise and its dispersion jitter
  draw from one shared random generator block by block; over 30 plucks the
  level distribution is the same (mean 0.020 / 0.028 at 24 / 64, standard
  error 0.003–0.004, single plucks ranging 0.002–0.097 either way).
- **Rings** fixes `kMaxBlockSize` at 24 upstream, so `eurorack-opt/rings/dsp/dsp.h`
  forks it to follow `OSC_NATIVE_BLOCK_SIZE`. Upstream already initialises its
  note filter and string LFOs from `kSampleRate / kMaxBlockSize`; the one
  per-block smoother it does not scale, the sympathetic strings' frequency
  glide, is corrected in `eurorack-opt/rings/dsp/part.cc`. Across all six
  models at Polyphony 1 and 4, per-note level and attack spectral centroid
  over 30 notes agree within 1.3 standard errors; Models 0 and 3, which have
  no random excitation, match to within 0.3%.
- **LFO2** advances once per block in both ports, so its step is scaled by
  the block size: measured on the ARM binaries, a pitch LFO at Rate 30% runs at
  the same 0.93 Hz before and after; uncorrected it would have dropped to
  0.375 Hz. LFO1 is
  advanced per render by the wrapper and never depended on the block size.
- Both ports `static_assert` that their block size equals
  `OSC_NATIVE_BLOCK_SIZE` — `OSC_CYCLE` writes one into a buffer of the other.

What does change: parameter changes, note-ons and Rings' arpeggiator steps land
on 64-sample (1.3 ms) boundaries instead of 24-sample (0.5 ms) ones. The
arpeggiator still counts in samples, so its tempo is exact; only the jitter of
a single step grows. Per-block parameter smoothers inside some Plaits engines
(e.g. the wavetable engine's knob smoothing) run 2.67 times slower, as they
already do on NTS-1 — tens of milliseconds at the slowest.

Mussola stays at 24: it times its LFO, its round-robin harmonics update and its
word-bank decode pacing in blocks, so moving it is a re-tuning job. Elements
and Clouds already divide 64 exactly (32-sample blocks); moving Elements to 64
bought 1% and changed its level, so it was left alone.

### CPU: Elements' filter bank, unrolled

The Elements units spend about 60% of every render in the modal resonator's
filter bank — one state-variable filter per mode, per sample.
`-funroll-loops` (their `config.mk` only) takes 14% off `elements_full` and
11–13% off the `modal_strike` variants, with **bit-identical output**, for
4 KB of code. On the other units it buys 1–2% and grows Clouds by 38 KB, so it
is not set there.

### CPU: Clouds' Stretch correlator on NEON

At `-O3` the WSOLA correlator's scoring loop became the largest cost in
Stretch and most of its tail. On ARM it now runs four words at a time with
`VCNT`, bit-exact (`make test-clouds-correlator`): Stretch's worst render
drops about 20% in both `clouds` and `clouds_fx` (497,060 → 394,551
instructions at default Size), its p99 11–25% and its mean 8–13%. Details in
[eurorack-opt/README.md](eurorack-opt/README.md).

### How these were measured

Every figure in the three sections above is **instructions per 64-frame
render**, counted exactly by a QEMU TCG plugin around `unit_render()` — not
timed — on units built with **KORG's own toolchain (GCC 6.5, glibc 2.24)**,
the one `build_drumlogue.sh` uses, and run against its sysroot. Header
defaults, a note every 125 ms. Counting rather than timing makes the tail
deterministic: the same build gives the same worst render every run.

It is still a proxy. QEMU prices every instruction alike, so it cannot see
cache misses, the Cortex-A7's in-order pipeline stalls, or that a NEON
q-register op occupies its 64-bit datapath for two beats. Directions are
reliable; percentages are estimates until the hardware agrees.

Note on the `-O3` table above: with this counter the `-Os` → `-O3` saving on
the shipped compiler is smaller than listed — Rings 19% rather than 71%,
mo2_add 52% rather than 85%, mo2_va 11% rather than 12% — while the number of
*basic blocks* executed per render falls by 60%, 88% and 14%, much closer to
the listed figures. Fewer, longer blocks with fewer taken branches do help an
in-order core, so the real gain is probably between the two, but the -Os →
-O3 change on its own is likely to have bought Rings less headroom than the
table suggests.

### What is still on the table

Ranked by what they would buy, measured where they could be:

1. **SIMD across modes in the modal filter banks.** Elements' resonator is
   ~60% of `elements_full`, and Rings' Modal model is the same structure:
   independent SVFs fed the same input, one per mode, which is the textbook
   case for four modes per NEON register. Upstream stores the filters as an
   array of objects, so this is a rewrite of the resonator's inner loop, with
   a differential test against the scalar one like the FFT's. Likely the
   largest single win left, and the only one here that QEMU cannot size —
   NEON gains need the hardware.
2. **SIMD across Rings' strings.** The sympathetic and string models run up to
   eight `rings::String`s per sample (delay-line Hermite reads, damping filter,
   dispersion all-pass): about 85% of a Rings render. Strings are independent
   until they are summed, but each reads its delay line at its own fractional
   position, so the loads stay scalar and only the arithmetic vectorises.
   Smaller gain than (1) for more work.
3. **Clouds Stretch's remaining burst.** Stretch's worst renders are still
   6–10× its median: a window's search scores all its candidates across four
   `Prepare()` calls, and packing the correlator's sign bits
   (`ReadSignBits`) lands in the same blocks. The window hop leaves room to
   spread the search over many more blocks; that needs the same
   timing-and-guard analysis the existing two-block split got.
4. **Clouds Granular's 1-2-1 pattern.** The 32 kHz engine needs 42.67 samples
   per render, so renders alternate one and two 32-sample `Process()` calls and
   p99 sits at about 2× the median. Two calls of 21–22 samples per render
   would flatten it, if `GranularProcessor` is validated at those sizes (the
   low-fidelity path halves them, so they must stay even).
5. **Mussola at 64.** Same 3-3-2 pattern as Plaits had; needs its block-timed
   features re-tuned first.
6. **Not rendering silence.** Every engine runs at full cost whether or not it
   is sounding. Detecting a decayed voice and skipping the engine until the
   next note would cut the *average* load substantially, but not the worst
   render, which is the one that drops audio — so it ranks last here.

Compiler flags were tried too, with KORG's GCC 6.5: `-mcpu=cortex-a7` (which
allows the hardware divider) changed no unit's instruction count by more than
0.3%; `-O2` was worse almost everywhere (Plaits Additive +85%, Clouds Stretch
p99 +140%). GCC 13 needs glibc 2.27 symbols from `libm` that KORG's 2.24
sysroot does not have, so a unit built with Ubuntu's cross compiler (as
`make test-arm` does) is not one to install.

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
