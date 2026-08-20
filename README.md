Eurorack Oscillators for Korg prologue, minilogue xd, Nu:tekt NTS-1, and drumlogue
=================================

Ports of some of Mutable Instruments (tm) oscillators to the Korg "logue" multi-engine.

> ## ⚠ Warning — `clouds` and `clouds_fx` on drumlogue: Mode 3 (Spectral) is CPU-marginal
>
> **The freeze is fixed. Spectral is still not comfortable to use.**
>
> Spectral used to crackle and then **lock the whole drumlogue up within
> seconds — the power cable had to be pulled.** Shrinking the phase vocoder's
> FFT from 4096 to 512 points and splitting the stereo pair's two transforms
> across the hop removed that: **re-tested on hardware, Spectral no longer
> hangs the instrument.**
>
> What hardware also said is that it was **still heavy, and audibly so — it
> clicked.** The response was to halve the phase vocoder's overlap, which
> halves how often it transforms at all. Measured per render, that took
> Spectral from 20.3% to **13.6%** on the synth and 32.0% to **23.9%** on the
> FX — on the synth it is now the *second cheapest* of the four modes rather
> than the most expensive. Reconstruction is unaffected and measured
> (`make test-clouds-cola`); what it costs is a grainier, less smoothed sound
> on heavily modified spectra, which is a deliberate trade.
>
> **That build has not been back to hardware.** Until it has, still treat
> Mode 3 as a mode to use deliberately:
>
> - Expect clicks may remain, more so with other parts and effects running.
> - **Avoid fast parameter changes while Spectral is playing.** This has not
>   been seen to crash, but the margin was thin and knob sweeps are the
>   obvious way to spend what is left of it.
> - Do not use it in a live set, or anywhere a dropout would cost you
>   something.
>
> Modes 0-2 (Granular, Stretch, Looping Delay) were fine on hardware
> throughout. Stretch has since had its own burst — the WSOLA correlator's
> window load — split across two blocks, which cuts its worst case at large
> Size settings and leaves output bit-identical. Every other oscillator in
> this repository is unaffected by all of it.
>
> Full measurements, and the two structural options still on the table (a
> transform spread across several audio blocks, or moved to a worker thread),
> are in
> [docs/CLOUDS_DRUMLOGUE_AUDIO_NOTES.md](docs/CLOUDS_DRUMLOGUE_AUDIO_NOTES.md).

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

`LFO2 Rate` at 0 means no modulation: Depth does nothing until the rate leaves
its end stop. This is the same in all three ports that have an LFO2, and it is
checked exactly rather than approximately — `make test-arm` renders each unit
twice at rate 0, once at each end of Depth, and requires the two renders to
agree sample for sample.

It used to drift instead of holding, and holding is not the same as reading
zero: a stopped phase accumulator still has a value. Both are fixed. Every
shape now comes from one phase accumulator rather than from stmlib's
`CosineOscillator`, whose recursion degenerates into an integrator at rate 0,
and the LFO reports zero rather than its parked value while the rate is at
zero. See the comment above the LFO2 block in `macro-oscillator2.cc`.

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

**Parameters (21):**

| # | Name | Description |
|---|------|-------------|
| 0 | Base Note | MIDI note for trigger pad (0-127, default C4) |
| 1 | Position | Excitation point along the resonator (0-100%) |
| 2 | Structure | Frequency ratio / inharmonicity (0-100%) |
| 3 | Brightness | Spectral tilt — dark to bright (0-100%) |
| 4 | Damping | Resonance decay time (0-100%) |
| 5 | Chord | Chord voicing for the Sympathetic Quantized model (0-13, see below) |
| 6 | Model | Resonator type (0-5, see table above; default 4 = Sympathetic Quantized) |
| 7 | Polyphony | Number of voices (1-4) |
| 8 | Arp | Arpeggiator pattern: Off, Up, Down, Up-Dn, Dn-Up, Up-P-Dn, Up-Dn-P, Dn-P-Up, Dn-Up-P |
| 9 | Arp Src | Arp note source: Chord (steps the selected Chord's tones) or Octaves (steps octaves of the root) |
| 10 | Arp Rate | Tempo-synced step length: 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 |
| 11 | Arp Oct | Octave span of the sequence (1-4) |
| 12 | LFO1 Target | What LFO1 modulates: Position, Struct., Bright., Damping, Note, Chord, LFO2 Frq, LFO2 Dep |
| 13 | LFO1 Shape | Transfer curve: Cosine, Triangle, Ramp Up, Ramp Down, Fat Sine |
| 14 | LFO1 Rate | 0-100% (0 = LFO1 off) |
| 15 | LFO1 Depth | 0-100% (default 100%) |
| 16 | LFO2 Target | As LFO1 Target, minus the two LFO2 entries — LFO2 cannot modulate itself |
| 17 | LFO2 Shape | As LFO1 Shape |
| 18 | LFO2 Rate | 0-100% (0 = LFO2 off) |
| 19 | LFO2 Depth | 0-100% |
| 20 | Note Range | Semitones of pitch modulation at full LFO swing (0-24, default 2) |

**Modulation:** two LFOs, one destination each, laid out as on the Plaits and
Elements ports but with a Rate, Depth, Shape and Target apiece — pages 4 and 5
are one LFO each, in the same order, so the pair reads the same way. LFO1's
waveform still arrives from outside (the host's shape LFO on prologue, the
wrapper's own oscillator on drumlogue); Depth scales it after its Shape curve
has been applied, so turning it down turns the modulation down rather than
bending the curve. Either LFO at Rate 0 contributes nothing.

Alongside the four continuous knobs, two destinations are particular to this
engine:

- **Note** transposes the sounding pitch by up to `Note Range` semitones at
  full swing — 2 for vibrato, 12 for octave sweeps. Applied after the
  arpeggiator, so a running pattern transposes as a whole instead of having its
  intervals rewritten under it.
- **Chord** steps along the chord list rather than sliding through it, because
  it selects a table of string tunings rather than naming a quantity. Slow, it
  is a chord sequencer; fast, it is closer to a broken arpeggio. It clamps
  rather than wraps, so `Chord` itself decides where in the list the sweep sits
  — put it mid-list for a symmetric one.

**Chords:** the first eleven — `Oct`, `5th`, `sus4`, `min`, `min7`, `min9`, `min11`,
`69`, `Maj9`, `Maj7`, `Maj` — are Rings' own. Three more are added here, chosen for
things a resonator can do that a keyboard cannot:

| | | |
|---|---|---|
| 11 | `4ths` | Stacked perfect fourths. No third at all, so it is neither major nor minor — open and unresolved. Not the same as `sus4`, which is a triad with the third moved; this is fourths all the way up. |
| 12 | `Just7` | A just-intoned dominant seventh — harmonics 4:5:6:7 of the root. The seventh is 31 cents flat of the tempered one, so instead of beating against the root it locks into its overtone series and the chord fuses into a single tone. |
| 13 | `Slendro` | The Javanese gamelan pentatonic, as five equal steps to the octave. Every interval is foreign to equal temperament; struck and left to ring it is recognisably gong-like. |

The last two are microtonal, which is why they are worth having on *this* engine
specifically: each string is tuned independently and rings sympathetically, so
intervals that merely sound out of tune on a piano audibly lock or beat here. The
arpeggiator walks the same intervals, fractions included.

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
- `Just7` and `Slendro` show their character best with Damping high and Polyphony 1: the strings need time to ring before the tuning is audible as tuning rather than as a chord shape
- Point an LFO at `Chord` with a slow rate and a high `Damping` to get a chord sequence that overlaps itself — each change retunes the strings while the previous chord is still ringing
- `Note Range` is what decides whether `Note` is an effect or a gesture: 1-3 semitones is vibrato, 12 turns a slow LFO into an octave sweep, and 7 with a Ramp shape steps the whole arpeggio up a fifth and drops it back
- Turn on the `Arp` with `Arp Src = Chord` for instant tempo-synced strum patterns; try `Arp Oct = 2-3` for wider runs
- Switch to Model 0 (Modal) and sweep Structure for metallic to harmonic
- Model 2 (Karplus-Strong) with low Damping makes excellent plucked bass/guitar
- Increase Polyphony for chordal playing (uses more CPU per voice)
- Chord only affects Model 4 (Sympathetic Quantized) *as a resonator voicing*; the arpeggiator, however, uses the Chord selection on any model to build its note sequence

For more information please read the excellent [Mutable Instruments Rings documentation](https://mutable-instruments.net/modules/rings/manual/).

Clouds (based on Clouds)
----
*Granular audio processor*

> **⚠ Warning — Mode 3 (Spectral) is CPU-marginal.** It used to freeze the
> instrument; that is fixed and confirmed on hardware. It is still heavy
> enough to click, and fast parameter changes while it plays are the thing
> most likely to push it over. Modes 0-2 are unaffected. See the warning at
> the top of this README and
> [docs/CLOUDS_DRUMLOGUE_AUDIO_NOTES.md](docs/CLOUDS_DRUMLOGUE_AUDIO_NOTES.md).

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
| 3 | Density | Grain rate / overlap — sparse individual grains to a dense cloud (0-100%) |
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
- Mode 0 (Granular): Density runs the grain scheduler, from about one grain at a time up to roughly 18 grains. The knob is linear in grain count — the engine's own law is cubic, which put almost all the range and almost all the CPU in the top fifth of the travel, so it is inverted here and capped where the drumlogue can still pay for it. It remains the main CPU control (about 2x from 0% to 100%). Position feeds from the recording buffer, so a fresh voice takes a moment to fill before higher Position settings have material to granularize
- Mode 0 (Granular) + small Size + high Density = shimmering cloud texture
- Mode 1 (Stretch) and Mode 3 (Spectral) are the expensive modes. Both do their work in `Prepare()`, which the original firmware runs in its idle loop and this port runs on the audio thread, so it arrives as a burst rather than as steady load. Running the engine at 32 kHz (see below) cut Stretch by 17% and Spectral by 27% and made the bursts a third less frequent, but did not remove them; Modes 0 (Granular) and 2 (Looping Delay) have no burst at all. **Spectral's burst is what hung the instrument** — its average cost is only ~10% of the block budget, but nearly all of it landed in one block per hop, measuring ~4x the budget for that block. Shrinking the FFT from 4096 to 512 points brought that under the deadline; Stretch's burst is the correlator, not the FFT, and is untouched. See [docs/CLOUDS_DRUMLOGUE_AUDIO_NOTES.md](docs/CLOUDS_DRUMLOGUE_AUDIO_NOTES.md)
- The engine runs at Clouds' native 32 kHz, converted to and from the drumlogue's 48 kHz at the edges. Size, delay times and the buffer's capacity are therefore 1.5x longer in real time than earlier builds — that is what the hardware sounds like — and the buffer takes 1.5x longer to fill, so give a fresh voice a moment before high Position settings have material. Pitch is unchanged. The top end rolls off above 13 kHz, roughly like Clouds' own codec
- Reverb at 0% and Texture at or below 75% are now genuinely free: the engine used to run its reverb and diffuser every block whatever the knobs said, and the fork in `eurorack-opt/` skips them when their amount is zero. Together with the 32 kHz change that is about a third off every mode. Turning Reverb up from 0 starts the tail from silence rather than releasing the history the stock engine had been quietly accumulating
- Mode 3 (Spectral) now runs a 512-point FFT instead of 4096, so its analysis window is 16 ms rather than 128 ms at the engine's 32 kHz: much tighter and more transient, with little of the long smeared freeze left. It also runs at 50% overlap instead of upstream's 75%, halving how often it transforms — that is what took it from the most expensive mode to the second cheapest, and it costs some smoothness on heavily modified spectra. Reconstruction of an unmodified signal is unaffected This was done to keep the per-hop FFT burst inside the audio deadline — at 4096 it hung the instrument, and 1024 was enough for the synth but not for CloudsFX. Two side effects worth knowing: **Position now works in Spectral**, where it previously did nothing at all (the old FFT size left room for only one magnitude texture, and Position indexes between textures), and the whole mode got slightly cheaper on average as well. In stereo the two channels' transforms are also spread across the hop instead of running back to back, which halves the remaining peak without changing the sound at all. A synth-only build can raise `CLOUDS_FFT_SIZE` to 1024 in `eurorack-opt/clouds/dsp/pvoc/stft.h` and get the longer window back — with the split in place it clears the synth's deadline comfortably
- Mode 1 (Stretch) + Freeze on = infinite sustain of any sound
- Use SampleBank/SampleNum to process drumlogue's built-in samples as grain source
- Feedback > 70% creates self-oscillating loops — use with care

For more information please read the excellent [Mutable Instruments Clouds documentation](https://mutable-instruments.net/modules/clouds/manual/).

CloudsFX (Clouds as an insert effect)
----
*Granular delay/texture effect (drumlogue only)*

> **⚠ Warning — usable only with care.** Hardware testing found the crash
> **fixed** (the unit no longer silences the audio interface), but the CPU
> cost is high enough that the unit is hard to use in practice, and the output
> was unstable in **Mode 3 (Spectral)** and at **Position 100% + Density
> 100%**. CloudsFX builds against the same engine fork as the synth, so it
> picks up both Spectral fixes described at the top of this README — and the
> second one, splitting the stereo pair's transforms across the hop, was
> driven by this unit: it is the one whose deadline the FFT could not fit
> inside. **Spectral is the most expensive thing either unit does, and
> CloudsFX is the more expensive of the two** — it measures about half again
> the synth's cost per render, because it resamples stereo in both directions
> with the dry path included. Expect clicks in Mode 3 and avoid fast parameter
> changes while it plays. The Position/Density instability is a separate,
> undiagnosed issue: that setting measures 15.9% mean with zero deadline
> misses, so the FFT burst does not explain it. Measurements in
> [docs/CLOUDS_DRUMLOGUE_AUDIO_NOTES.md](docs/CLOUDS_DRUMLOGUE_AUDIO_NOTES.md).
>
> Changing Mode, Quality or resetting the unit mutes it for 2-4 blocks while
> the engine is rebuilt; that is by design.
>
> CloudsFX now matches the synth: the same grain-linear Density mapping and
> the same 32 kHz engine. It adds about 2.2 ms of latency, dry included —
> passing the dry signal round the conversion would put it ahead of the wet
> path and comb-filter the mix.

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
| 2 | Density | Grain rate / overlap — sparse individual grains to a dense cloud (0-100%) |
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

# Build every unit as a host shared object and run the drumlogue probe
# battery at it under AddressSanitizer and UndefinedBehaviorSanitizer.
# No ARM toolchain, no QEMU, no Docker.  This is what finds reads one
# element past a lookup table, which test-arm cannot see -- the stray
# read lands in the next table and returns a plausible number.
make test-asan

# Clouds-specific suites (also part of test-all)
make test-clouds-synth          # real engine behind OSC_*: pitch across the
                                # 48/32 kHz boundary, all modes x qualities
make test-clouds-fx             # FX bus in -> engine -> out, dry and wet
make test-clouds-fx-reconfig    # render thread vs control thread doing
                                # Mode/Quality/reset (the park handshake)
make test-clouds-engine-opt     # eurorack-opt/ fork vs the stock submodule
                                # engine, compared sample for sample
make test-clouds-grain-window   # grain envelope, fork vs stock, plus an ASan
                                # run over the endpoint that used to read one
                                # element past lut_window
make test-clouds-pvoc-rr        # phase vocoder scheduling: one channel per
                                # call vs upstream's loop, sample for sample
                                # at every FFT size from 256 to 4096
make test-clouds-cola           # STFT overlap-add reconstruction at hop ratio
                                # 4, 2 and 1 -- what backs CLOUDS_PVOC_HOP_RATIO

# Run the shipped .drmlgunit binaries on emulated ARM
make test-arm

# Per-block cost distribution on emulated ARM: mean, worst block and the
# fraction of blocks over deadline, per mode.  This is what identified the
# Spectral freeze -- Spectral's mean is low, its worst block is ~4x budget
make bench-clouds-spike

# Per-render cost of whole units, through the drumlogue ABI, on emulated ARM.
# bench-clouds-spike benches the Clouds engine; this benches units, which is
# the only scale on which two different units can be compared
make bench-units ARM_UNITS="mo2_va mussola rings clouds clouds_fx"
```

**Testing the real unit binaries (`make test-arm`):**

`make test-all` links the port layer into an x86 host binary. That catches
logic bugs, but it cannot catch anything that only exists in the artifact the
drumlogue actually loads. `make test-arm` cross-compiles the real
`.drmlgunit` files and drives them through the SDK ABI under QEMU, checking
the ARM/NEON paths, parameter sweeps, partial buffers, stack usage, the
UI/preset callbacks over their whole declared range, parameter values outside
the declared range, the note/gate/tempo/expression callbacks, cross-unit
isolation, and — see below — control-thread callbacks racing `unit_render()`.

Load more than one unit at a time when running it. Several of the faults it
has caught only exist between units — the address space is shared, so whether
a stray write lands on a mapped page depends on what else is loaded, and a
unit that passes alone can segfault the moment a second one is present.

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
renderer is reading is a race, and three of them were real crashes:

- **Rings' `Polyphony`** went straight into `Part::set_polyphony()` from the
  parameter callback. `Part::Process()` reads `polyphony_` twice and derives
  an array index from the second read: the voice loop runs to `polyphony_`,
  and inside it `RenderStringVoice()` computes
  `num_strings = 2 * kMaxPolyphony / polyphony_` and then
  `string_[voice + string * polyphony_]`, into an eight-element array. Those
  agree only while `polyphony_` holds still. Lower it between the loop bound
  and the divide — one parameter push while a note is sounding, which is
  exactly what the firmware does when a unit is selected or a project is
  recalled — and at 4 → 1 the index reaches `string_[10]` and is *written*
  to. A wild write on the audio thread, into the address space every loaded
  unit shares. `Model` had the same shape.
- `unit_reset()` on CloudsFX used to re-initialize the engine inline, which
  rewrites the buffer pointers, heads and contents that `Process()` reads.
- `osc_adapter_reset()` used to clear the render cursor pair inline. Landing
  between the renderer's `n = min(need, avail)` and its `avail -= n` underflows
  `avail` to ~2^32, after which the read position walks further off the end of
  `s_render_buf` every block until it faults.

Rings' `Model`/`Polyphony` and `osc_adapter_reset()`'s flush now latch a
request that the audio thread applies at the top of its next render, which is
also where the Clouds synth applies Mode/Quality changes — those switch the
engine between its 16-bit and 8-bit buffers, and only the following
`Prepare()` sets the matching buffer up. `make test-arm` runs a control thread
hammering all four callbacks against 4000 rendered blocks per unit; the
pre-fix `unit_reset()` and the pre-fix Rings `Polyphony` both segfault under
it, the latter only when a second unit is loaded alongside.

Rings' latch also stops the reconfiguration being paid twice. The firmware
pushes a value for every parameter slot whenever a unit is loaded, and
`Part::set_polyphony()` marks the part dirty whether or not the value changed
— so the default push bought a full `ConfigureResonators()`, which for the
string models re-initializes all eight strings and clears ~96 KB of delay
line. `OSC_INIT` now spends that once on the control thread instead, by
rendering and discarding a single block; the first audio block after the unit
is selected went from 356% of its deadline to 145% under
`make bench-units`, the remainder being the page faults any freshly
`dlopen()`ed unit pays.

Deferring to the audio thread only works when the deferred work is small,
though, and CloudsFX's was not: a reallocating `Prepare()` clears ~180 KB,
which trades a data race for a blown deadline. CloudsFX therefore does the
opposite — the control thread asks the renderer to stand down, waits for it to
acknowledge, reconfigures the engine itself, and hands it back. The renderer
never blocks and never reallocates; it emits silence for the two to four
blocks the handover takes. `make test-clouds-fx-reconfig` drives that from two
real threads at three buffer sizes. The protocol, including why the park
transition has to be a compare-exchange and why the wait needs two different
timeouts, is written up in
[docs/CLOUDS_DRUMLOGUE_AUDIO_NOTES.md](docs/CLOUDS_DRUMLOGUE_AUDIO_NOTES.md).

**What a unit owes the instrument (`drumlogue_guards.h`):**

Three guarantees the wrappers now enforce for every unit, because getting any
of them wrong takes down more than the unit that got it wrong:

- **No non-finite sample leaves `unit_render()`.** The drumlogue mixes every
  part through shared send effects, and a reverb or delay is an IIR with
  feedback: one NaN reaching its delay line makes every later output of that
  effect NaN, forever, whatever is fed in afterwards. A unit that emits one
  bad block does not cost one bad block of audio — it silences the whole
  instrument, and *keeps* it silent after the user has moved to a different
  unit, which reads exactly like the audio engine having crashed. The last
  thing a render does is drop non-finite samples, so the unit goes quiet
  instead. This is containment, not a cure: an engine whose filter state has
  latched NaN keeps producing it, and recovering that is the engine's own
  business (`mussola.cc` re-initializes the offending voice).
- **Parameter values are held to the range the header declares.**
  `unit_set_param_value()` takes an `int32_t` and the firmware pushes a value
  for every one of the 24 slots. The mappings cast it to `uint16_t`, so an
  out-of-range value does not arrive small — it arrives *large*: −100 becomes
  65436, and mussola's LFO Rate exponentiates its argument into a frequency,
  which at 65436 is `+inf`, an infinite LFO phase, a NaN sine and NaN output
  from then on.
- **The target check does not read `unit_header`.** That is the one data
  symbol `unit_exports.map` must keep exported, every drumlogue unit defines
  it, and they all share one dynamic scope — so `unit_header.target` read from
  inside `unit_init()` binds to the *first-loaded* unit's header. An FX unit
  loaded after a synth then compares delfx against synth, fails its own target
  check, and never initializes. The shipped binaries escape this only because
  the SDK builds with `-flto` and the constant gets folded before a relocation
  is emitted; a non-LTO build of the same sources reproduces it exactly.
  `header.c` and both wrappers now use one `UNIT_OWN_TARGET` constant.

**Forked engine sources (`eurorack-opt/`):**

`eurorack/` is a submodule this repo does not edit, so the two engine changes
that were worth making live in `eurorack-opt/`, which shadows the submodule on
the include path: an early-out for the reverb and diffuser when their amount
is zero (about a quarter of a block, at both units' default settings), and a
one-line switch to LUT twiddle factors in the FFT. `make test-clouds-engine-opt`
compiles the same rendering against both engines and compares it sample for
sample, because "this optimisation is inaudible" is a claim worth checking
rather than asserting.

Two of the forked files are headers that change `sizeof(GranularProcessor)`,
so the fork is all-or-nothing: `eurorack-opt` must precede `eurorack` on the
include path, and `clouds-granular.cc`/`clouds-fx.cc` `#error` if the build
says it wants the fork but the headers say otherwise. See
[eurorack-opt/README.md](eurorack-opt/README.md) for the wiring and for how to
re-sync when the submodule moves.

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
