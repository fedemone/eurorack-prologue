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

**On drumlogue** (13 params):

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

**On drumlogue** (15 params):

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

**Parameters (12):**

| # | Name | Description |
|---|------|-------------|
| 0 | Base Note | MIDI note for trigger pad (0-127, default C4) |
| 1 | Position | Excitation point along the resonator (0-100%) |
| 2 | Structure | Frequency ratio / inharmonicity (0-100%) |
| 3 | Brightness | Spectral tilt — dark to bright (0-100%) |
| 4 | Damping | Resonance decay time (0-100%) |
| 5 | Chord | Chord voicing for the Sympathetic Quantized model (0-10) |
| 6 | Model | Resonator type (0-5, see table above; default 4 = Sympathetic Quantized) |
| 7 | Polyphony | Number of voices (1-4) |
| 8 | Arp | Arpeggiator pattern: Off, Up, Down, Up-Dn, Dn-Up, Up-P-Dn, Up-Dn-P, Dn-P-Up, Dn-Up-P |
| 9 | Arp Src | Arp note source: Chord (steps the selected Chord's tones) or Octaves (steps octaves of the root) |
| 10 | Arp Rate | Tempo-synced step length: 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 |
| 11 | Arp Oct | Octave span of the sequence (1-4) |

**Arpeggiator:** Rings sounds one held note at a time, so the built-in arpeggiator
builds a sequence of pitches and re-strums the resonator step-by-step, synced to the
drumlogue's tempo. The `Arp Src` knob picks what the sequence is built from — the tones
of the currently-selected `Chord` (so the Chord knob is musically useful on *every*
model, not just Sympathetic Quantized) or plain octaves of the root. `Arp Oct` spreads
the sequence across up to four octaves. The `-P-` patterns insert a silent rest step,
letting the resonance ring through the gap. Set `Arp` to `Off` for normal single-note
playing. (Tempo comes from the host clock; if a platform never reports a tempo the arp
runs at 120 BPM.)

**Output level:** The mono mixdown applies +3 dB of make-up gain overall, plus a further
+6 dB for the two sympathetic-string models (Sympathetic String and Sympathetic
Quantized), which are inherently quieter than the Modal/String/FM/Reverb models — so the
default patch sits at a comparable level to the others. Peaks are still clamped, so the
extra gain can't clip the output.

**Sound design tips:**
- The default model is 4 (Sympathetic Quantized) so the Chord parameter works right away — sweep Chord for different strummed voicings
- Turn on the `Arp` with `Arp Src = Chord` for instant tempo-synced strum patterns; try `Arp Oct = 2-3` for wider runs
- Switch to Model 0 (Modal) and sweep Structure for metallic to harmonic
- Model 2 (Karplus-Strong) with low Damping makes excellent plucked bass/guitar
- Increase Polyphony for chordal playing (uses more CPU per voice)
- Chord only affects Model 4 (Sympathetic Quantized) *as a resonator voicing*; the arpeggiator, however, uses the Chord selection on any model to build its note sequence

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
| 3 | Density | Extra grain rate / overlap on top of the base grain stream (0-100%) |
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
- Mode 0 (Granular) produces a continuous grain stream at any Density (grains are seeded automatically); Density adds extra overlap on top. Position feeds from the recording buffer, so a fresh voice takes a moment to fill before higher Position settings have material to granularize
- Mode 0 (Granular) + small Size + high Density = shimmering cloud texture
- Mode 1 (Stretch) + Freeze on = infinite sustain of any sound
- Mode 3 (Spectral) is CPU-heavy but produces unique frozen-spectrum effects
- Use SampleBank/SampleNum to process drumlogue's built-in samples as grain source
- Feedback > 70% creates self-oscillating loops — use with care

For more information please read the excellent [Mutable Instruments Clouds documentation](https://mutable-instruments.net/modules/clouds/manual/).

CloudsFX (Clouds as an insert effect)
----
*Granular delay/texture effect (drumlogue only)*

The synth `clouds` above has to invent an input — an internal sawtooth or a
loaded sample — because a drumlogue **synth** unit's render callback ignores
audio input. `CloudsFX` is the same Clouds engine built as a **delfx** unit
instead, so it granulates the **incoming FX-bus audio**: route any drumlogue
part through it and the grains, stretches, delays and spectral freezes are
made from *that* signal. Because the input is now real, Freeze, Feedback,
Dry/Wet and the Looping-Delay / Stretch modes all become genuinely useful.

It is a **drumlogue-only** unit (delfx units don't exist on the
prologue-class platforms) and is built via the SDK project `clouds_fx`
(`./build_drumlogue.sh clouds_fx`).

**Parameters (11):** identical to the Clouds synth, minus everything that
fed an internal source — no **Base Note**, and no **SampleBank / SampleNum /
SmplStart / SmplEnd**:

| # | Name | Description |
|---|------|-------------|
| 0 | Position | Where in the buffer to read grains (0-100%) |
| 1 | Size | Grain length / buffer region (0-100%) |
| 2 | Density | Extra grain rate / overlap on top of the base grain stream (0-100%) |
| 3 | Texture | Grain window shape / filtering (0-100%) |
| 4 | Pitch | Pitch transposition in semitones (-24 to +24, center=24) |
| 5 | Feedback | Amount of output fed back into input (0-100%) |
| 6 | Dry/Wet | Mix between the dry input and the processed output (0-100%, default 50%) |
| 7 | Reverb | Built-in reverb amount (0-100%) |
| 8 | Freeze | Freeze the audio buffer (on/off) |
| 9 | Mode | Playback mode (0-3: Granular / Stretch / Delay / Spectral) |
| 10 | Quality | Audio quality / stereo mode (0-3) |

**Sound design tips:**
- Mode 2 (Looping Delay) + Feedback = a pitch-shiftable, freezable delay on the incoming audio
- Mode 1 (Stretch) + Freeze on = infinite sustain of whatever was playing when you froze
- Dry/Wet defaults to 50% (an insert-FX default); turn it fully wet for pure granular textures
- Input/output levels are conservative and clip-safe; on hardware you can trim with the drumlogue's own FX send/return

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

**Parameters (24):**

| # | Name | Description |
|---|------|-------------|
| 0 | Base Note | MIDI note for trigger pad (0-127, default C4) |
| 1 | Phoneme | Vowel / phoneme selection; word selection in the LPC word region (0-100%) |
| 2 | Timbre | Vocal register / formant shift (0-100%) |
| 3 | Harmonics | Model blend and LPC word-bank selection (0-100%) |
| 4 | Morph | Morph within current model (0-100%) |
| 5 | Speed | LPC word playback speed (50=normal) and Staccato burst rate |
| 6 | Prosody | Pitch-contour replay amount for LPC words (0-100%) |
| 7 | Decay | Envelope decay AND release time, 5ms-5s (0-100%) |
| 8 | Mix | Main/auxiliary output crossfade (0-100%) |
| 9 | Model | Synthesis model (0-3, see table above) |
| 10 | Gate Mode | Trigger / Sustain / Continuous / Staccato |
| 11 | Voices | Unison voice count (1-4) |
| 12 | Detune | Unison detune amount (0-100%) |
| 13 | Spread | Stereo spread of unison voices (0-100%) |
| 14 | Gender | Formant shift — bass (0) to soprano (100), neutral at 50 |
| 15 | Attack | Envelope attack time, 1ms-2s (0-100%) |
| 16 | Style | Vocal style (0-5, see table below) |
| 17 | Key Mode | Phoneme selection mode (0-5, see table below) |
| 18 | Gliss | Glissando time for pitch and phoneme passage (0-100%) |
| 19 | Sustain | Envelope sustain level (0-100%) |
| 20 | LFO Shape | None / Sine / Square / Saw |
| 21 | LFO Dest | Modulation destination (see below) |
| 22 | LFO Rate | 0.05 Hz to 20 Hz, exponential (0-100%) |
| 23 | LFO Depth | Modulation depth (0-100%) |

**Envelope (ADSR):** Attack, Decay, Sustain and Gliss-free Release form a
full ADSR — the release always reuses the Decay time. Gate Mode changes
how the envelope is driven:

| # | Gate Mode | Behavior |
|---|-----------|----------|
| 0 | Trigger | One-shot attack-decay: the note falls to silence at the Decay rate even while the pad is held |
| 1 | Sustain | Full ADSR: decay to the Sustain level while held, release (= Decay time) on note-off |
| 2 | Continuous | Drone: always on. In the LPC word region the Phoneme knob scrubs through the word bank as an evolving vocal drone |
| 3 | Staccato | Free-running bursts of gates (1.5-13.5 Hz, rate set by the Speed knob) — each burst retriggers the engine, the envelope and the current word/syllable |

**The 6 vocal styles (Style):**

| # | Style | Character |
|---|-------|-----------|
| 0 | Male | Dark formants, subtle vibrato |
| 1 | Female | Raised formants, medium vibrato |
| 2 | Child | High formants, +1 octave, fast vibrato |
| 3 | Robot | Pitch quantized to semitones, no vibrato, defaults to the SAM model when Model=Blend |
| 4 | Alien | Phonemes snapped to unusual off-vowel positions, pitch quantized to an inharmonic Bohlen-Pierce step grid, unison voices stacked at inharmonic intervals, plus soft-clip distortion and a slow-swept phaser |
| 5 | Religious | Parallel organum: unison voices stacked at octave/fifth/sub-octave drone, slow gentle chant vibrato, vowels biased into the open a/o/e chant range, built-in legato (minimum glissando) and a very slow formant drift |

The Style formant character is added on top of the Gender parameter.
For Religious organum, raise Voices: 2 voices = octaves, 3 = adds the
fifth, 4 = adds a sub-octave drone. Alien does the same with inharmonic
intervals.

**The 6 key modes (Key Mode):**

| # | Mode | Behavior |
|---|------|----------|
| 0 | Normal | Phoneme knob selects the phoneme (previous behavior) |
| 1 | Syllable | Phoneme knob selects one of 8 simple syllables (Ka Te Mi Ko Tu La No Su) — each note plays a consonant→vowel transition |
| 2 | KeyVow A | Each key is assigned one of the 5 vowels (A E I O U), chromatic assignment |
| 3 | KeyVow B | As A but transposed assignment (different vowel on the same key) |
| 4 | KeySyl C | Each key is assigned one of the 8 syllables, chromatic assignment |
| 5 | KeySyl D | As C but transposed assignment |

Key modes 2-5 follow the key both on fresh triggers and on legato /
Base-Note changes while a note is sounding.

**LPC word banks (Italian / liturgical):** in Blend mode, Harmonics above
~38% selects one of 5 word banks and the Phoneme knob selects the word;
each trigger sings it. The banks are Madama Butterfly fragments and
liturgical phrases, synthesized as LPC10 bitstreams by
`tools/generate_lpc_words.py`:

| Bank | Words |
|------|-------|
| 1 | "un bel dì", "bello" |
| 2 | "giunto il tempo", "così" |
| 3 | "fan", "tutto" |
| 4 | "kyrie", "eleison", "kyrie eleison" |
| 5 | "oṃ", "maṇi", "padme", "hūṃ", "oṃ maṇi padme hūṃ" |

Speed changes the word tempo, Prosody replays each phrase's pitch
contour (rising "così", falling mantra endings), and Gender/Timbre
shift the singer's formants.

**The assignable LFO:** LFO Dest offers 15 destinations — Pitch, Phoneme,
Timbre, Harmonics, Morph, Speed, Prosody, Decay, Mix, Detune, Spread,
Gender, Attack, Sustain and Gliss. Pitch modulation spans ±1 octave at
full depth; all other destinations are modulated in their natural 0-100%
range. Structural switches (Model, Voices, Style, Gate Mode, Key Mode)
are not modulatable by design: they retrigger engines or word-bank
decodes. In forced-model mode the Harmonics destination is inactive
(the value is pinned to keep the engine on its cheap path) — use
Model=Blend to modulate Harmonics.

**Glissando (Gliss):** smooths the passage between phonemes (and pitch)
with a glide time from instant (0%) to about half a second (100%). In the
Syllable/KeySyl modes it also stretches the consonant→vowel transition.

**Sound design tips:**
- Sweep Phoneme slowly for vowel animation ("aah" to "eee" to "ooh")
- Model 1 (SAM) at low Phoneme values produces classic robot voice
- Voices=4 with Detune=30-50 and Spread=80 creates a wide stereo choir
- Gender shifts the formant spectrum — low values = bass voice, high = soprano
- Style=Robot + Key Mode=KeyVow A turns a melody line into robotic vowel speech
- Key Mode=KeySyl C + Gliss=40 gives chant-like syllabic singing across the keyboard
- Style=Religious + Voices=4 + Gate=Sustain + long Attack/Decay = gregorian choir pad
- Blend + Harmonics=75 + Gate=Staccato + Speed=30 chants "kyrie eleison" in rhythm
- Gate=Continuous + Blend + Harmonics>40: slowly turn Phoneme to scrub through an opera phrase as a drone
- LFO Sine → Gender at low rate adds a slow male/female morph to any patch

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

# Run the shipped .drmlgunit binaries on emulated ARM
make test-arm
```

**Testing the real unit binaries (`make test-arm`):**

`make test-all` links the port layer into an x86 host binary. That catches
logic bugs, but it cannot catch anything that only exists in the artifact the
drumlogue actually loads. `make test-arm` cross-compiles the real
`.drmlgunit` files and drives them through the SDK ABI under QEMU, checking
the ARM/NEON paths, parameter sweeps, partial buffers, stack usage, the
UI/preset callbacks over their whole declared range, cross-unit isolation,
and — see below — control-thread callbacks racing `unit_render()`.

```bash
apt-get install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf qemu-user
git submodule update --init logue-sdk
make test-arm                          # defaults to clouds clouds_fx mo2_va rings
make test-arm ARM_UNITS="rings mussola"
```

The target skips itself with a message if the toolchain, QEMU, or the SDK
submodule is missing, so it is safe to run anywhere.

**Cross-unit isolation (`drumlogue/unit_exports.map`):**

The drumlogue loads every unit in `Units/` into one address space. All the
units here are built from the same port layer, so they all define
`OSC_CYCLE`, `osc_adapter_*` and the eurorack DSP under the same names. When
those are exported, the dynamic linker binds every unit's internal calls to
whichever unit was loaded first — so the second unit onwards silently renders
the first unit's engine, and any unit's parameter changes are applied to the
first unit's engine. Where the two units disagree on buffer geometry (this
repo mixes `OSC_NATIVE_BLOCK_SIZE` 24 and 32) the mismatch becomes an
out-of-bounds write.

Every project's `config.mk` therefore links with
`--version-script=drumlogue/unit_exports.map`, which exports only the
callbacks the firmware looks up with `dlsym()`. `make test-arm` asserts that
no unit exports anything else.

**Control thread vs audio thread:**

The drumlogue calls `unit_set_param_value()`, `unit_reset()`, `unit_suspend()`
and `unit_resume()` from its control thread while `unit_render()` runs on the
audio thread. Anything a control callback does that re-seats state the
renderer is reading is a race, and two of them were real crashes:

- `unit_reset()` on CloudsFX used to re-initialize the engine inline, which
  rewrites the buffer pointers, heads and contents that `Process()` reads.
- `osc_adapter_reset()` used to clear the render cursor pair inline. Landing
  between the renderer's `n = min(need, avail)` and its `avail -= n` underflows
  `avail` to ~2^32, after which the read position walks further off the end of
  `s_render_buf` every block until it faults.

Both now latch a request that the audio thread applies at the top of its next
render, which is also where Clouds' Mode/Quality changes are applied — those
switch the engine between its 16-bit and 8-bit buffers, and only the following
`Prepare()` sets the matching buffer up. `make test-arm` runs a control thread
hammering all four callbacks against 4000 rendered blocks per unit; the
pre-fix `unit_reset()` segfaults under it.

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
