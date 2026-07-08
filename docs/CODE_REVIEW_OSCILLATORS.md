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

## Known issues, not fixed here (upstream submodule)

* `plaits/dsp/speech/sam_speech_synth.cc` (`InterpolatePhonemeData`) and
  `lpc_speech_synth_controller.cc` (consonant trigger:
  `last_playback_frame_ = playback_frame_ + 1`) read **one element past**
  their phoneme tables when the last consonant is selected. The
  out-of-bounds value is interpolated with fraction 0.0, so the audible
  result is correct, but it is UB and shows up under ASan. Fixing it
  requires patching the `eurorack` submodule
  (`peterall/eurorack`), which is pinned read-only from this repo.
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

* **Style** (param 16): Male / Female / Child / Robot / Alien —
  per-style formant offset (stacked with Gender), pitch offset,
  vibrato; Robot quantizes pitch to semitones and defaults to the SAM
  model in Blend mode; Alien adds a slow formant-sweep LFO.
* **Key Mode** (param 17): Normal / Syllable (8 consonant→vowel
  syllables selected by the Phoneme knob) / 4 key-assign variants
  (vowel-per-key A/B, syllable-per-key C/D, each with a transposed
  assignment table).
* **Gliss** (param 18): one-pole glissando (0 → ~0.5 s) applied to both
  pitch and phoneme morph; also stretches the consonant→vowel
  transition in the syllable modes.
