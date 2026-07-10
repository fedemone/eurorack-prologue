/*
 * Mussola - Abstract Vocal Synth Engine for drumlogue
 *
 * Based on Mutable Instruments Plaits SpeechEngine.
 * Produces abstract choral vocalizations (not realistic speech).
 *
 * Phase 2: Multi-voice unison with detune, gender, stereo spread.
 *
 * Three synthesis sub-models blended via Harmonics parameter:
 *   0.0-0.33: NaiveSpeechSynth (formant filters, warm choir pads)
 *   0.33-0.67: SAMSpeechSynth (retro robotic vocalization)
 *   0.67-1.0: LPCSpeechSynth (LPC10 codec, eerie vocal fragments)
 *
 * Parameters:
 *   id 0:  Base Note  (0-127 MIDI)
 *   id 1:  Phoneme    (shape knob, 0-100% -> vowel/phoneme selection)
 *   id 2:  Timbre     (shiftshape knob, 0-100% -> vocal register/formant)
 *   id 3:  Harmonics  (0-100% -> model blend Naive/SAM/LPC)
 *   id 4:  Morph      (0-100% -> additional phoneme modulation)
 *   id 5:  Speed      (0-100% -> LPC playback speed, centered at 50)
 *   id 6:  Prosody    (0-100% -> prosody replay amount for LPC words)
 *   id 7:  Decay      (0-100% -> envelope decay time)
 *   id 8:  Mix        (0-100% -> main/aux output crossfade)
 *   id 9:  Model      (0-3 -> force Naive/SAM/LPC, 3=blend)
 *   id 10: Gate Mode  (0-2 -> Trigger/Sustain/Continuous)
 *   id 11: Voices     (1-4 -> unison voice count)
 *   id 12: Detune     (0-100% -> unison detune amount, max ±15 cents)
 *   id 13: Spread     (0-100% -> stereo spread of unison voices)
 *   id 14: Gender     (0-100% -> formant shift, 50=neutral)
 *   id 15: Attack     (0-100% -> envelope attack time)
 *   id 16: Style      (0-4 -> Male/Female/Child/Robot/Alien vocal style)
 *   id 17: Key Mode   (0-5 -> Normal/Syllable/KeyVow A/KeyVow B/KeySyl C/KeySyl D)
 *   id 18: Gliss      (0-100% -> glissando/portamento time for pitch and phoneme)
 *
 * Output: Stereo float via mussola_render_stereo(), mono Q31 fallback via yn
 */

#include "userosc.h"
#include "stmlib/dsp/dsp.h"

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/engine/speech_engine.h"

#include <cstring>
#include <cmath>

/* --- Constants --- */
static const uint16_t kMaxVoices = 4;

/* --- Custom OSC_PARAM indices (beyond standard k_user_osc_param_*) --- */
enum {
  k_mussola_param_speed     = 8,
  k_mussola_param_prosody   = 9,
  k_mussola_param_decay     = 10,
  k_mussola_param_mix       = 11,
  k_mussola_param_model     = 12,
  k_mussola_param_gate_mode = 13,
  k_mussola_param_voices    = 14,
  k_mussola_param_detune    = 15,
  k_mussola_param_spread    = 16,
  k_mussola_param_gender    = 17,
  k_mussola_param_attack    = 18,
  k_mussola_param_style     = 19,
  k_mussola_param_key_mode  = 20,
  k_mussola_param_gliss     = 21,
};

/* --- Static state --- */
static plaits::SpeechEngine engines_[kMaxVoices];
static plaits::EngineParameters parameters_;

static uint16_t p_values_[6] = {0};
static float shape_ = 0, shiftshape_ = 0;
static float shape_lfo_ = 0;
static bool gate_ = false, previous_gate_ = false;
static float amp_ = 0.0f;

/* Custom param storage */
static float prosody_ = 0.0f;
static float speed_ = 1.0f;
static float decay_alpha_ = 0.002f;
static float attack_alpha_ = 0.05f;
static float mix_ = 0.0f;
static uint16_t model_select_ = 3; /* 0=Naive, 1=SAM, 2=LPC, 3=blend */
static uint16_t gate_mode_ = 0;    /* 0=Trigger, 1=Sustain, 2=Continuous */
static uint16_t num_voices_ = 1;   /* 1-4 */
static float detune_ = 0.0f;       /* 0.0-1.0 */
static float spread_ = 0.0f;       /* 0.0-1.0 */
static float gender_ = 0.0f;       /* -1.0 to +1.0 (0 = neutral) */

/* --- Vocal style / key mode / glissando (Phase 3) --- */
static uint16_t style_ = 0;        /* 0=Male 1=Female 2=Child 3=Robot 4=Alien */
static uint16_t key_mode_ = 0;     /* 0=Normal 1=Syllable 2-5=key-assign variants */
static float gliss_ = 0.0f;        /* 0.0-1.0 glissando amount */

/* Smoothed (glissando) state */
static float morph_z_ = 0.5f;      /* smoothed phoneme/morph */
static float note_z_ = 60.0f;      /* smoothed pitch (semitones) */
static bool  smooth_valid_ = false;

/* Syllable playback state */
static float syllable_time_ = 1.0f;   /* seconds since last trigger */
static uint16_t latched_sound_ = 0;   /* vowel/syllable index latched at trigger */

/* Style modulation state */
static float vibrato_phase_ = 0.0f;
static float formant_lfo_phase_ = 0.0f;

/*
 * Per-voice harmonics, updated round-robin (one voice per block).
 * Harmonics crossing an LPC word-bank boundary triggers a synchronous
 * bitstream decode of the whole bank inside the render callback; with
 * unison, all voices would otherwise decode in the SAME block and blow
 * the real-time budget (observed as a crash on hardware when sweeping
 * Harmonics). Staggering spreads the decodes across blocks (0.5ms per
 * voice of extra latency on a harmonics change - inaudible).
 */
static float voice_harmonics_[kMaxVoices] = {0.0f, 0.0f, 0.0f, 0.0f};
static uint32_t block_counter_ = 0;

/*
 * Per-style settings. Gender offset shifts the formant spectrum
 * (added to the user Gender parameter), pitch offset transposes,
 * vibrato adds pitch modulation, and Robot additionally quantizes
 * pitch to semitones and defaults to the SAM model. Alien sweeps
 * the formants with a slow LFO.
 */
struct StyleSettings {
  float gender_offset;     /* -1..+1 formant shift */
  float pitch_offset;      /* semitones */
  float vibrato_rate;      /* Hz */
  float vibrato_depth;     /* semitones */
  bool  quantize_pitch;    /* robot: stepped pitch, no gliss on pitch */
  float formant_lfo_rate;  /* Hz (0 = off) */
  float formant_lfo_depth; /* timbre modulation depth */
};

static const StyleSettings kStyles[5] = {
  /* Male   */ {-0.50f,  0.0f, 4.5f, 0.04f, false, 0.00f, 0.00f},
  /* Female */ { 0.35f,  0.0f, 5.5f, 0.08f, false, 0.00f, 0.00f},
  /* Child  */ { 0.70f, 12.0f, 6.0f, 0.12f, false, 0.00f, 0.00f},
  /* Robot  */ { 0.00f,  0.0f, 0.0f, 0.00f, true,  0.00f, 0.00f},
  /* Alien  */ {-0.20f,  0.0f, 0.8f, 0.60f, false, 0.35f, 0.45f},
};

/* Morph positions approximating the A/E/I/O/U vowels in the engines'
 * phoneme space (morph parameter 0..1). */
static const float kVowelMorph[5] = {0.02f, 0.27f, 0.50f, 0.73f, 0.98f};

/*
 * 8 simple syllables, built as consonant->vowel morph glides
 * (diphthong-style transitions in the abstract phoneme space).
 */
struct Syllable {
  float consonant;  /* morph start point (attack) */
  float vowel;      /* morph sustain point */
};

static const Syllable kSyllables[8] = {
  {0.98f, 0.02f},  /* "Ka" */
  {0.85f, 0.27f},  /* "Te" */
  {0.73f, 0.50f},  /* "Mi" */
  {0.98f, 0.73f},  /* "Ko" */
  {0.85f, 0.98f},  /* "Tu" */
  {0.60f, 0.02f},  /* "La" */
  {0.50f, 0.73f},  /* "No" */
  {0.35f, 0.98f},  /* "Su" */
};

/*
 * Engine buffers: each SpeechEngine.Init() uses BufferAllocator for:
 *   - LPCSpeechSynthWordBank frame storage:
 *     kLPCSpeechSynthMaxFrames (1024) × sizeof(LPCSpeechSynth::Frame) (14)
 *     = 14336 bytes
 *   - 2 × kMaxBlockSize float temp buffers (2 × 24 × 4 = 192 bytes)
 * If the arena is too small, BufferAllocator::Allocate returns NULL and
 * the LPC word bank writes through a NULL pointer on first use — size it
 * with headroom and verify at compile time.
 */
static const size_t kEngineBufferSize = 16384;
static_assert(kEngineBufferSize >=
                  plaits::kLPCSpeechSynthMaxFrames *
                      sizeof(plaits::LPCSpeechSynth::Frame) +
                  2 * plaits::kMaxBlockSize * sizeof(float),
              "Engine arena too small for SpeechEngine::Init allocations");
alignas(16) static uint8_t engine_buffers_[kMaxVoices][kEngineBufferSize];

/* Stereo output buffers filled by OSC_CYCLE, read by adapter */
static float s_stereo_left_[plaits::kMaxBlockSize] __attribute__((aligned(16)));
static float s_stereo_right_[plaits::kMaxBlockSize] __attribute__((aligned(16)));
static uint32_t s_stereo_frames_ = 0;

/*
 * Per-voice detune offsets (in units of detune_semitones).
 * Indexed by [num_voices - 1][voice_index].
 */
static const float kVoiceDetune[kMaxVoices][kMaxVoices] = {
  { 0.0f,   0.0f,  0.0f,  0.0f},   /* 1 voice: no detune */
  {-1.0f,   1.0f,  0.0f,  0.0f},   /* 2 voices: symmetric */
  {-1.0f,   0.0f,  1.0f,  0.0f},   /* 3 voices: center + sides */
  {-1.0f,   1.0f, -0.6f,  0.6f},   /* 4 voices: wide + narrow pair */
};

/*
 * Per-voice pan positions (0=left, 0.5=center, 1=right).
 * Modulated by spread_ parameter.
 */
static const float kVoicePan[kMaxVoices][kMaxVoices] = {
  {0.5f,  0.0f,  0.0f,  0.0f},     /* 1 voice: center */
  {0.25f, 0.75f, 0.0f,  0.0f},     /* 2 voices: L/R */
  {0.15f, 0.5f,  0.85f, 0.0f},     /* 3 voices: L/C/R */
  {0.1f,  0.9f,  0.35f, 0.65f},    /* 4 voices: L/R/CL/CR */
};

/* ======================================================================
 * Stereo accessor for adapter
 * ==================================================================== */

extern "C" void mussola_get_last_stereo(const float **left, const float **right) {
  *left = s_stereo_left_;
  *right = s_stereo_right_;
}

/* ======================================================================
 * Engine lifecycle / output watchdog
 * ==================================================================== */

/* (Re-)initialize one voice's engine over its static arena. Also used by
 * the render watchdog to self-heal a voice whose filter state latched
 * NaN/inf (the LPC lattice state is not clamped upstream). */
static void reset_engine(uint16_t v) {
  stmlib::BufferAllocator allocator;
  allocator.Init(engine_buffers_[v], kEngineBufferSize);
  engines_[v].Init(&allocator);
  engines_[v].set_prosody_amount(prosody_);
  engines_[v].set_speed(speed_);
}

/* True if the block contains NaN, inf, or runaway samples (|x| > 8).
 * Bit-level test: all of those have (bits & 0x7FFFFFFF) > 0x41000000
 * (8.0f). Works under -ffast-math, where isnan()/isfinite() may be
 * compiled away. */
static inline bool block_invalid(const float *x, uint32_t n) {
  for (uint32_t i = 0; i < n; ++i) {
    uint32_t b;
    memcpy(&b, &x[i], sizeof(b));
    if ((b & 0x7FFFFFFFu) > 0x41000000u) return true;
  }
  return false;
}

/* ======================================================================
 * OSC API Implementation
 * ==================================================================== */

void OSC_INIT(uint32_t platform, uint32_t api)
{
  (void)platform;
  (void)api;

  for (uint16_t v = 0; v < kMaxVoices; ++v) {
    reset_engine(v);
  }

  parameters_.trigger = plaits::TRIGGER_UNPATCHED;
  parameters_.note = 60.0f;
  parameters_.timbre = 0.5f;
  parameters_.morph = 0.5f;
  parameters_.harmonics = 0.0f;
  parameters_.accent = 0.5f;

  smooth_valid_ = false;
  syllable_time_ = 1.0f;
  latched_sound_ = 0;
  vibrato_phase_ = 0.0f;
  formant_lfo_phase_ = 0.0f;
}

void OSC_NOTEON(const user_osc_param_t *const params)
{
  (void)params;
  gate_ = true;
}

void OSC_NOTEOFF(const user_osc_param_t *const params)
{
  (void)params;
  gate_ = false;
}

void OSC_CYCLE(const user_osc_param_t *const params,
               int32_t *yn, const uint32_t frames)
{
  shape_lfo_ = q31_to_f32(params->shape_lfo);

  const uint32_t nframes = (frames <= plaits::kMaxBlockSize) ? frames : plaits::kMaxBlockSize;
  const float block_dt = (float)nframes / 48000.0f;
  const StyleSettings &style = kStyles[(style_ < 5) ? style_ : 0];

  /* Raw pitch from adapter (integer part = played MIDI key) */
  const uint8_t key_note = (uint8_t)(params->pitch >> 8);
  float note_target =
      ((float)key_note) +
      ((params->pitch & 0xFF) * k_note_mod_fscale) +
      style.pitch_offset;

  /* Trigger logic */
  bool triggered = false;
  {
    bool effective_gate = gate_;
    if (gate_mode_ == 2) /* Continuous */
      effective_gate = true;

    if (effective_gate && !previous_gate_) {
      parameters_.trigger = plaits::TRIGGER_RISING_EDGE;
      triggered = true;
    } else {
      parameters_.trigger = plaits::TRIGGER_LOW;
    }
    previous_gate_ = effective_gate;
  }

  /* On trigger: restart syllable envelope and latch the key-assigned
   * vowel/syllable. The 4 key-assign variants (A-D) differ in what is
   * assigned (vowels vs syllables) and in the assignment transpose. */
  if (triggered) {
    syllable_time_ = 0.0f;
    switch (key_mode_) {
      case 2: latched_sound_ = key_note % 5; break;        /* KeyVow A */
      case 3: latched_sound_ = (key_note + 3) % 5; break;  /* KeyVow B (transposed) */
      case 4: latched_sound_ = key_note % 8; break;        /* KeySyl C */
      case 5: latched_sound_ = (key_note + 4) % 8; break;  /* KeySyl D (transposed) */
      default: break;
    }
  }

  /* Morph fine modulation from the Morph parameter (id4) */
  const float morph_mod = p_values_[k_user_osc_param_id2] * 0.01f - 0.5f;

  /* ---- Phoneme source selection (Key Mode) ---- */
  float morph_target;
  switch (key_mode_) {
    default:
    case 0: /* Normal: Phoneme knob + LFO + Morph */
      morph_target = clip01f(shape_ + shape_lfo_ + morph_mod);
      break;

    case 1: { /* Syllable: Phoneme knob selects one of 8 syllables */
      uint16_t idx = (uint16_t)(clip01f(shape_ + shape_lfo_) * 7.999f);
      const Syllable &syl = kSyllables[idx];
      /* Consonant->vowel transition time scales with Gliss */
      const float t_trans = 0.03f + gliss_ * 0.25f;
      float s = (syllable_time_ >= t_trans) ? 1.0f : syllable_time_ / t_trans;
      s = s * s * (3.0f - 2.0f * s); /* smoothstep */
      morph_target = clip01f(syl.consonant + (syl.vowel - syl.consonant) * s
                             + morph_mod * 0.25f);
      break;
    }

    case 2: /* KeyVow A: vowel assigned per key */
    case 3: /* KeyVow B: transposed vowel assignment */
      morph_target = clip01f(kVowelMorph[latched_sound_ % 5] + morph_mod * 0.25f);
      break;

    case 4:   /* KeySyl C: syllable assigned per key */
    case 5: { /* KeySyl D: transposed syllable assignment */
      const Syllable &syl = kSyllables[latched_sound_ % 8];
      const float t_trans = 0.03f + gliss_ * 0.25f;
      float s = (syllable_time_ >= t_trans) ? 1.0f : syllable_time_ / t_trans;
      s = s * s * (3.0f - 2.0f * s);
      morph_target = clip01f(syl.consonant + (syl.vowel - syl.consonant) * s
                             + morph_mod * 0.25f);
      break;
    }
  }
  syllable_time_ += block_dt;

  /* ---- Glissando (smooth passage between phonemes and pitches) ----
   * One-pole glide toward the targets; time constant 0..~0.5s. */
  if (!smooth_valid_) {
    morph_z_ = morph_target;
    note_z_ = note_target;
    smooth_valid_ = true;
  }
  if (gliss_ > 0.001f) {
    const float tau = 0.02f + gliss_ * 0.5f;   /* seconds */
    float alpha = block_dt / tau;
    if (alpha > 1.0f) alpha = 1.0f;
    morph_z_ += (morph_target - morph_z_) * alpha;
    note_z_ += (note_target - note_z_) * alpha;
  } else {
    morph_z_ = morph_target;
    note_z_ = note_target;
  }

  float note_final = note_z_;

  /* ---- Style modulation ---- */
  if (style.quantize_pitch) {
    /* Robot: hard-stepped pitch, no vibrato */
    note_final = (float)(int)(note_final + 0.5f);
  } else if (style.vibrato_depth > 0.0f) {
    vibrato_phase_ += style.vibrato_rate * block_dt;
    if (vibrato_phase_ >= 1.0f) vibrato_phase_ -= 1.0f;
    note_final += sinf(vibrato_phase_ * 6.2831853f) * style.vibrato_depth;
  }

  float style_timbre_mod = 0.0f;
  if (style.formant_lfo_depth > 0.0f) {
    formant_lfo_phase_ += style.formant_lfo_rate * block_dt;
    if (formant_lfo_phase_ >= 1.0f) formant_lfo_phase_ -= 1.0f;
    style_timbre_mod = sinf(formant_lfo_phase_ * 6.2831853f) * style.formant_lfo_depth;
  }

  parameters_.note = note_final;
  parameters_.morph = clip01f(morph_z_);
  parameters_.timbre = clip01f(shiftshape_ + style_timbre_mod); /* Timbre → vocal register/formant */

  /* Harmonics controls model blend (0-1)
   * Model select overrides: force harmonics into the sub-range for that model */
  if (model_select_ < 3) {
    /* Force: 0=Naive(0.0), 1=SAM(0.166), 2=LPC(0.5).
     * SAM sits at group 0.996 (just below 1.0): the engine then renders
     * Naive+SAM with the blend at ~100% SAM - audibly pure SAM, but it
     * avoids the much more expensive LPC controller, whose internal
     * clock rate scales with formant shift (Gender). At the previous
     * 0.17 (group 1.02), Gender=100% with 4 voices tripled the LPC call
     * rate and overran the render deadline on hardware. */
    static const float model_harmonics[] = {0.0f, 0.166f, 0.5f};
    parameters_.harmonics = model_harmonics[model_select_];
  } else if (style.quantize_pitch) {
    /* Robot style with Model=Blend: default to the SAM (robotic) model */
    parameters_.harmonics = 0.166f;
  } else {
    /* Blend mode: Param 1 (id3) controls harmonics, scaled 0-100 -> 0.0-1.0 */
    parameters_.harmonics = clip01f(p_values_[k_user_osc_param_id1] * 0.01f);
  }

  /* Stagger harmonics across voices: at most one voice picks up a new
   * value per block, so word-bank decodes never pile up in one block. */
  if (num_voices_ == 1) {
    voice_harmonics_[0] = parameters_.harmonics;
  } else {
    voice_harmonics_[block_counter_ % num_voices_] = parameters_.harmonics;
  }
  ++block_counter_;

  parameters_.accent = 0.8f;

  /* ---- Multi-voice rendering ---- */
  /* Render directly into the stereo output buffers to avoid a final memcpy */
  float *left = s_stereo_left_;
  float *right = s_stereo_right_;
  memset(left, 0, nframes * sizeof(float));
  memset(right, 0, nframes * sizeof(float));

  const float detune_semitones = detune_ * 0.15f; /* max ±15 cents */
  const float voice_gain = 1.0f / sqrtf((float)num_voices_);
  const uint16_t vi = num_voices_ - 1; /* table index */
  bool any_enveloped = false;

  /* Precompute per-voice pan gains (avoid sqrtf inside the voice loop) */
  float gain_l[kMaxVoices], gain_r[kMaxVoices];
  for (uint16_t v = 0; v < num_voices_; ++v) {
    float pan = 0.5f + (kVoicePan[vi][v] - 0.5f) * spread_;
    gain_l[v] = sqrtf(1.0f - pan) * voice_gain;
    gain_r[v] = sqrtf(pan) * voice_gain;
    /* Set engine params once (invariant across frames) */
    engines_[v].set_prosody_amount(prosody_);
    engines_[v].set_speed(speed_);
  }

  for (uint16_t v = 0; v < num_voices_; ++v) {
    plaits::EngineParameters vp = parameters_;
    vp.harmonics = voice_harmonics_[v];

    /* Per-voice detune */
    vp.note += detune_semitones * kVoiceDetune[vi][v];

    /* Per-voice gender (formant shift via timbre offset), including
     * the style's gender/formant character */
    vp.timbre = clip01f(vp.timbre + (gender_ + style.gender_offset) * 0.5f);

    /* Render this voice */
    float vout[plaits::kMaxBlockSize], vaux[plaits::kMaxBlockSize];
    bool venveloped = false;
    engines_[v].Render(vp, vout, vaux, nframes, &venveloped);
    if (venveloped) any_enveloped = true;

    /* Watchdog: if this voice's filter state blew up (NaN/inf/runaway
     * latches permanently in the LPC lattice), drop the block and
     * reinitialize the engine - it recovers on the next block instead
     * of going silent forever. */
    if (block_invalid(vout, nframes) || block_invalid(vaux, nframes)) {
      reset_engine(v);
      continue;
    }

    /* Mix out/aux per voice, accumulate into L/R */
    const float gl = gain_l[v], gr = gain_r[v];
    for (uint32_t i = 0; i < nframes; ++i) {
      float mixed = stmlib::Crossfade(vout[i], vaux[i], mix_);
      left[i]  += mixed * gl;
      right[i] += mixed * gr;
    }
  }

  /* Apply envelope (if no engine did) and output gain in a single pass */
  {
    const float out_gain = 0.8f;
    if (!any_enveloped) {
      float target, alpha;
      switch (gate_mode_) {
        default:
        case 0: /* Trigger */
          target = gate_ ? 1.0f : 0.0f;
          alpha = gate_ ? attack_alpha_ : decay_alpha_;
          break;
        case 1: /* Sustain */
          target = gate_ ? 1.0f : 0.0f;
          alpha = gate_ ? attack_alpha_ : 0.01f;
          break;
        case 2: /* Continuous */
          target = 1.0f;
          alpha = attack_alpha_;
          break;
      }
      for (uint32_t i = 0; i < nframes; ++i) {
        amp_ += (target - amp_) * alpha;
        float g = amp_ * out_gain;
        left[i]  *= g;
        right[i] *= g;
      }
    } else {
      for (uint32_t i = 0; i < nframes; ++i) {
        left[i]  *= out_gain;
        right[i] *= out_gain;
      }
    }
  }

  s_stereo_frames_ = nframes;

  /* Output mono Q31 (L+R average) as fallback */
#ifdef __ARM_NEON
  {
    const float32x4_t vscale = vdupq_n_f32(2147483648.0f);
    const float32x4_t vhalf = vdupq_n_f32(0.5f);
    const float32x4_t vmin = vdupq_n_f32(-1.0f);
    const float32x4_t vmax = vdupq_n_f32(1.0f);
    uint32_t i = 0;
    for (; i + 4 <= nframes; i += 4) {
      float32x4_t l = vld1q_f32(left + i);
      float32x4_t r = vld1q_f32(right + i);
      float32x4_t m = vmulq_f32(vaddq_f32(l, r), vhalf);
      m = vmaxq_f32(vminq_f32(m, vmax), vmin);
      int32x4_t q = vcvtq_s32_f32(vmulq_f32(m, vscale));
      vst1q_s32(yn + i, q);
    }
    for (; i < nframes; ++i) {
      float mono = (left[i] + right[i]) * 0.5f;
      yn[i] = f32_to_q31(mono);
    }
  }
#else
  for (uint32_t i = 0; i < nframes; ++i) {
    float mono = (left[i] + right[i]) * 0.5f;
    yn[i] = f32_to_q31(mono);
  }
#endif
}

void OSC_PARAM(uint16_t index, uint16_t value)
{
  switch (index) {
    case k_user_osc_param_id1: /* Harmonics (blend) */
    case k_user_osc_param_id2: /* Morph */
      p_values_[index] = value;
      break;

    case k_user_osc_param_shape: /* Phoneme */
      shape_ = param_val_to_f32(value);
      break;

    case k_user_osc_param_shiftshape: /* Timbre/register */
      shiftshape_ = param_val_to_f32(value);
      break;

    case k_mussola_param_speed: /* Speed: 0-100 -> 0.0-2.0 (50 = 1.0 normal) */
      speed_ = value * 0.02f;
      break;

    case k_mussola_param_prosody: /* Prosody: 0-100 -> 0.0-1.0 */
      prosody_ = value * 0.01f;
      break;

    case k_mussola_param_decay: /* Decay: 0-100 -> decay alpha (0.0005 to 0.05) */
      decay_alpha_ = 0.0005f + value * 0.0005f;
      break;

    case k_mussola_param_mix: /* Mix: 0-100 -> 0.0-1.0 */
      mix_ = value * 0.01f;
      break;

    case k_mussola_param_model: /* Model: 0-3 (Naive/SAM/LPC/Blend) */
      model_select_ = (value > 3) ? 3 : value;
      break;

    case k_mussola_param_gate_mode: /* Gate Mode: 0-2 */
      gate_mode_ = (value > 2) ? 2 : value;
      break;

    case k_mussola_param_voices: /* Voices: 1-4 */
      num_voices_ = value;
      if (num_voices_ < 1) num_voices_ = 1;
      if (num_voices_ > kMaxVoices) num_voices_ = kMaxVoices;
      break;

    case k_mussola_param_detune: /* Detune: 0-100 -> 0.0-1.0 */
      detune_ = value * 0.01f;
      break;

    case k_mussola_param_spread: /* Spread: 0-100 -> 0.0-1.0 */
      spread_ = value * 0.01f;
      break;

    case k_mussola_param_gender: /* Gender: 0-100 -> -1.0 to +1.0 (50=neutral) */
      gender_ = (value - 50) * 0.02f;
      break;

    case k_mussola_param_attack: /* Attack: 0-100 -> alpha (0.1 fast to 0.001 slow) */
      attack_alpha_ = (value == 0) ? 0.1f : 0.1f / (1.0f + value * 0.5f);
      break;

    case k_mussola_param_style: /* Style: 0-4 (Male/Female/Child/Robot/Alien) */
      style_ = (value > 4) ? 4 : value;
      break;

    case k_mussola_param_key_mode: /* Key Mode: 0-5 */
      key_mode_ = (value > 5) ? 5 : value;
      break;

    case k_mussola_param_gliss: /* Gliss: 0-100 -> 0.0-1.0 */
      gliss_ = value * 0.01f;
      break;

    default:
      break;
  }
}
