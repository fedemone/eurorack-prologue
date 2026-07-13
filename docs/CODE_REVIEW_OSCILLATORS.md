# Oscillator Code Review — bugs, performance, NEON (July 2026)

Scope: `macro-oscillator2.cc`, `modal-strike.cc`, `rings-resonator.cc`,
`clouds-granular.cc`, `mussola.cc`, `drumlogue_osc_adapter.cc`,
`drumlogue_unit_wrapper.cc`, `header.c`, and the drumlogue build system.

## Fixed in this pass

### Mussola could not be loaded — two independent root causes

1. **Undefined symbol at dlopen (the actual loader failure).**
   `lpc_speech_synth_controller.cc` / `lpc_speech_synth.cc` use
   `stmlib::Random`, whose state lives in `stmlib/utils/random.cc`.
   That file was missing from `osc_mussola.sources` and
   `drumlogue/mussola/config.mk`, so `mussola.drmlgunit` shipped with an
   unresolved `stmlib::Random::rng_state_`. Linking a shared object with
   undefined symbols succeeds silently; `dlopen()` on the drumlogue then
   rejects the unit. Every other unit that needs `Random` already linked
   `random.cc` — Mussola now does too. Verified with
   `nm -D --undefined-only`: only glibc/libm/libstdc++ versioned symbols
   remain.

2. **NULL-pointer write from an undersized engine arena.**
   Each `SpeechEngine::Init()` allocates
   `kLPCSpeechSynthMaxFrames (1024) × sizeof(LPCSpeechSynth::Frame) (14)
   = 14336` bytes plus two 96-byte temp buffers from its
   `BufferAllocator` arena — but `mussola.cc` provided only 8192 bytes
   per voice. `BufferAllocator::Allocate` returns NULL on exhaustion,
   `frames_` stayed NULL, and the first render that touched the LPC
   word-bank path (Model=LPC, or Blend with Harmonics ≳ 40%) wrote
   through NULL and crashed the unit. Reproduced host-side under ASan;
   arena raised to 16 KB per voice with a `static_assert` documenting
   the requirement.

   Note on the "C arrays declared const" hypothesis: not the cause —
   the `const` tables (`kVoiceDetune`, `kVoicePan`, LPC word banks) are
   fine in `.rodata`. The allocation issue above was the memory-related
   culprit.

### Clouds: Spectral mode never linked (same dlopen failure class)

`GranularProcessor` calls `clouds::PhaseVocoder` for playback mode 3
(Spectral), but `phase_vocoder.cc`, `frame_transformation.cc`, `stft.cc`
and `stmlib/dsp/atan.cc` were not in the clouds source lists. The unit
therefore had four unresolved symbols and would fail `dlopen()` exactly
like Mussola. Added to `osc_clouds.mk`, `drumlogue/clouds/config.mk`,
and `generate_sdk_projects.sh`.

### Build-system defects

* `static alignas(16)` (alignas after storage class) is rejected by
  GCC ≥ 12 — fixed in `clouds-granular.cc`, `rings-resonator.cc`,
  `mussola.cc`, `drumlogue_osc_adapter.cc` (`alignas(16) static ...`).
* SDK-generated wavetable projects defined `-DOSCILLATOR_TYPE=wt_a`
  while the Plaits resources are named `wav_integrated_waves_wta` —
  all six `mo2_wt*` units failed to compile via the SDK path. Fixed in
  the six `config.mk` files and `generate_sdk_projects.sh`.
* `elements_full` used `EXCITER_MODEL_GRANULAR_SAMPLE_PLAYER`, which is
  commented out of this eurorack fork's `ExciterModel` enum — the unit
  could not compile at all. The strike exciter now spans the widest
  available range (`MALLET`…`PARTICLES`) with a linear meta control.
* SDK-generated clouds project missed the `stmlib/third_party/STM`
  include directories and the `STM32F401xx` device define that
  `makefile.inc` supplies (needed by `clouds/drivers/debug_pin.h`).
* `.gitmodules` pointed `logue-sdk` at `korginc/logue-sdk`, but the
  pinned commit `dd2182e0` doesn't exist there —
  `git clone --recursive` failed for every fresh checkout. Re-pointed
  to `fedemone/logue-sdk` and pinned its `main`.
* `mono_to_stereo()` was compiled-but-unused in the Mussola wrapper
  build (warning noise) — now compiled out for `MUSSOLA_VOCAL`.
* Dead scratch arrays (`lvals`/`rvals`) removed from the clouds NEON
  mono-mix loop.

All 19 drumlogue units now cross-compile (armv7-a + NEON VFPv4) with
clean dynamic symbol tables, and all 352 host-side tests pass.

## Upstream speech-synth defects — now fixed via `plaits_patches/`

The Mussola build no longer compiles the upstream `sam_speech_synth.cc`,
`lpc_speech_synth.cc`, `lpc_speech_synth_controller.cc` and
`lpc_speech_synth_phonemes.cc`; it compiles patched copies from
`plaits_patches/` instead (see the README there for details). The
submodule itself stays untouched — only `.cc` files are replaced, so no
include-path shadowing is involved. Fixed defects:

* **Mid-word region-crossing OOB read (hardware SIGSEGV root cause).**
  When Harmonics drops from the LPC word-bank region into the phoneme
  region while a word is playing, `LPCSpeechSynthController::Render`
  keeps `playback_frame_` pointing deep into the word bank but swaps
  `frames` to the 16-entry phoneme table — reading kilobytes past it.
  Reproduced under ASan with the exact reported hardware recipe
  (Model=Blend, 4 voices, Phoneme 83%, Timbre 100%, Harmonics sweep).
  Harmless on MMU-less STM32 (Plaits proper), a segfault on the
  drumlogue's Linux/MMU. Patched to fall back to scan mode.
* `sam_speech_synth.cc` (`InterpolatePhonemeData`) and the LPC
  consonant trigger read **one element past** their phoneme tables when
  the last consonant is selected (interpolated with fraction 0, so
  inaudible, but UB). Patched with a clamp / a padded 16-entry table.
* `lpc_speech_synth.cc` could index `lut_lpc_excitation_pulse` with a
  **negative** index right after `Init()`. Patched with a clamp.
* `LPCSpeechSynthWordBank::LoadNextWord/Load` trusted the bank
  bitstream blindly and could write past the 1024-frame arena /
  32-entry word-boundary table. Patched with bounds.

## Known issues, not fixed here (upstream submodule)

* `macro-oscillator2.cc` `OSC_CYCLE` ignores the `frames` argument and
  always renders `plaits::kMaxBlockSize` (24) samples. Safe on
  drumlogue (the adapter always asks for exactly 24) and matches the
  original prologue port behavior, but worth knowing when reusing the
  code.

## Performance observations / NEON status (Cortex-A7, NEONv2/VFPv4)

What is already good:

* Hot Q31↔float conversion loops are NEON-vectorized everywhere
  (`vcvtq`, `vmulq`, clamp via `vminq/vmaxq`) — mussola, mo2, rings,
  adapter.
* Stereo interleave uses `vst2q_f32` (wrapper) and de-interleave uses
  `vld2_s16` (clouds) — the right idioms.
* The builds use `-mfpu=neon-vfpv4 -mvectorize-with-neon-quad
  -ftree-vectorize -funsafe-math-optimizations`, so straight-line float
  loops (envelopes, crossfades, gain staging) auto-vectorize; manual
  intrinsics there would add little.
* Mussola: per-voice pan gains are precomputed outside the frame loop;
  render writes directly into the stereo output buffers (no extra
  memcpy).

Remaining opportunities (none critical at current CPU budgets):

1. **Mussola voice mix loop** (`Crossfade + accumulate` per voice) is
   scalar C; at 4 voices × 24 frames it auto-vectorizes reasonably, but
   an explicit `vmlaq_f32` version would shave a few percent.
2. **modal-strike FIR upsampler** (`lp_even`/`lp_odd` macros) computes
   `SoftLimit` per output sample in scalar code; a NEON polynomial
   evaluation of both output phases at once is possible if Elements
   variants ever get CPU-bound.
3. **LFO trig calls** (`cosf`/`sinf` once per 24-sample block in the
   wrapper and mussola styles) are negligible (~2k calls/s) — fine.
4. `lfo.InitApproximate(freq)` is recomputed every block in
   `macro-oscillator2.cc`/`modal-strike.cc` even when the rate hasn't
   changed; caching the last rate would save a small amount of work.
5. The top-level `Makefile` defines the `$(OSCILLATORS)` rule twice
   (the second overrides the first; make prints a warning) and the
   `all` recipe builds the drumlogue platform twice — cosmetic, but the
   duplicate drumlogue pass doubles CI time for that target.

## Mussola feature additions (this branch)

* **Style** (param 16): Male / Female / Child / Robot / Alien /
  Religious — per-style formant offset (stacked with Gender), pitch
  offset, vibrato; Robot quantizes pitch to semitones and defaults to
  the SAM model in Blend mode. Alien snaps phonemes to off-vowel morph
  positions, quantizes pitch to a Bohlen-Pierce step grid (13 equal
  divisions of the tritave), stacks unison voices at inharmonic
  intervals and post-processes with a soft-clip waveshaper plus a
  slow-swept two-stage allpass phaser. Religious stacks voices in
  parallel organum (octave / fifth / sub-octave drone), adds slow
  chant vibrato, compresses vowels into the open a/o/e range and
  enforces a minimum glissando.
* **Key Mode** (param 17): Normal / Syllable (8 consonant→vowel
  syllables selected by the Phoneme knob) / 4 key-assign variants
  (vowel-per-key A/B, syllable-per-key C/D, each with a transposed
  assignment table). Key modes re-latch on legato/Base-Note changes,
  not just on fresh gates (a note change without a new gate previously
  kept the old vowel/syllable).
* **Gliss** (param 18): one-pole glissando (0 → ~0.5 s) applied to both
  pitch and phoneme morph; also stretches the consonant→vowel
  transition in the syllable modes.
* **ADSR envelope** (params 7/15/19): exponential attack 1 ms–2 s and
  decay/release 5 ms–5 s (the previous linear alpha mapping topped out
  around 42 ms, which is why the Decay knob audibly did nothing and
  notes ended abruptly), new Sustain level param; Trigger mode is a
  one-shot AD, Sustain mode a full ADSR with release = Decay time.
  The envelope now also applies in the LPC word region, where the
  engine's own prosody envelope previously left note-off unhandled
  (the word's last frame droned forever).
* **Staccato gate mode** (param 10 value 3): free-running gate bursts
  at 1.5–13.5 Hz (rate from the Speed knob, 60% duty); each burst
  retriggers the engine, the envelope and the current word/syllable.
* **Assignable LFO** (params 20–23): None/Sine/Square/Saw, 15
  destinations (all continuous params, plus Pitch at ±12 st full
  depth), exponential 0.05–20 Hz rate, depth. Evaluated once per
  24-sample block. Structural switches (Model, Voices, Style, Gate
  Mode, Key Mode) are deliberately not modulatable.
* **Italian/liturgical LPC word banks** (`mussola_words.cc`, generated
  by `tools/generate_lpc_words.py`): replaces the Plaits TI-ROM banks
  with Madama Butterfly fragments ("un bel dì", "bello", "giunto il
  tempo", "così", "fan", "tutto") and liturgical phrases ("kyrie
  eleison", "oṃ maṇi padme hūṃ"). Phrases are formant-synthesized,
  converted to reflection coefficients via a step-down recursion,
  quantized to the TMS5100-style codebooks and packed as LPC10
  bitstreams. The generator round-trips every bank through a literal
  Python port of the Plaits decoder, checks lattice stability and
  formant placement, and appends a silence frame per word (the LPC
  controller holds the last frame forever after a word ends). Total
  bank data is 957 bytes vs ~11 KB upstream, cutting the worst-case
  synchronous bank decode inside the render callback by ~10× — a
  large real-time-margin win for Harmonics sweeps with 4 voices.
* **Continuous gate mode** now runs the engine with
  `TRIGGER_UNPATCHED`, so in the word region the Phoneme knob scrubs
  through the word bank as an evolving drone (Plaits scan mode)
  instead of the word playing once and freezing on its last frame.
