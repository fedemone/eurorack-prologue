/*
 * Mussola - Abstract Vocal Synth Engine for drumlogue
 *
 * Based on Mutable Instruments Plaits SpeechEngine.
 * Produces abstract choral vocalizations (not realistic speech).
 *
 * Phase 2: Multi-voice unison with detune, gender, stereo spread.
 * Phase 3: Vocal styles, key modes, glissando.
 * Phase 4: ADSR envelope, staccato gate, assignable LFO,
 *          Alien rework (warped phonemes, distortion/phaser,
 *          inharmonic pitch grid), Religious style (organum),
 *          Italian/liturgical LPC word banks (mussola_words.cc).
 *
 * Three synthesis sub-models blended via Harmonics parameter:
 *   0.0-0.33: NaiveSpeechSynth (formant filters, warm choir pads)
 *   0.33-0.67: SAMSpeechSynth (retro robotic vocalization)
 *   0.67-1.0: LPCSpeechSynth (LPC10 codec, word banks)
 *
 * Parameters:
 *   id 0:  Base Note  (0-127 MIDI)
 *   id 1:  Phoneme    (shape knob, 0-100% -> vowel/phoneme selection)
 *   id 2:  Timbre     (shiftshape knob, 0-100% -> vocal register/formant)
 *   id 3:  Harmonics  (0-100% -> model blend Naive/SAM/LPC)
 *   id 4:  Morph      (0-100% -> additional phoneme modulation)
 *   id 5:  Speed      (0-100% -> LPC word playback speed / staccato rate)
 *   id 6:  Prosody    (0-100% -> prosody replay amount for LPC words)
 *   id 7:  Decay      (0-100% -> envelope decay AND release time)
 *   id 8:  Mix        (0-100% -> main/aux output crossfade)
 *   id 9:  Model      (0-3 -> force Naive/SAM/LPC, 3=blend)
 *   id 10: Gate Mode  (0-3 -> Trigger/Sustain/Continuous/Staccato)
 *   id 11: Voices     (1-4 -> unison voice count)
 *   id 12: Detune     (0-100% -> unison detune amount, max ±15 cents)
 *   id 13: Spread     (0-100% -> stereo spread of unison voices)
 *   id 14: Gender     (0-100% -> formant shift, 50=neutral)
 *   id 15: Attack     (0-100% -> envelope attack time)
 *   id 16: Style      (0-5 -> Male/Female/Child/Robot/Alien/Religious)
 *   id 17: Key Mode   (0-5 -> Normal/Syllable/KeyVow A/KeyVow B/KeySyl C/KeySyl D)
 *   id 18: Gliss      (0-100% -> glissando/portamento time for pitch and phoneme)
 *   id 19: Sustain    (0-100% -> envelope sustain level)
 *   id 20: LFO Shape  (0-3 -> None/Sine/Square/Saw)
 *   id 21: LFO Dest   (0-14 -> modulation destination, see kLfoDest*)
 *   id 22: LFO Rate   (0-100% -> 0.05 Hz .. 20 Hz, exponential)
 *   id 23: LFO Depth  (0-100% -> modulation depth)
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
  k_mussola_param_sustain   = 22,
  k_mussola_param_lfo_shape = 23,
  k_mussola_param_lfo_dest  = 24,
  k_mussola_param_lfo_rate  = 25,
  k_mussola_param_lfo_depth = 26,
};

/* LFO destinations (values of k_mussola_param_lfo_dest).
 * Gate Mode, Key Mode, Model, Voices and Style are deliberately not
 * modulatable: they are structural switches whose changes retrigger
 * engines or word-bank decodes. */
enum {
  k_lfo_dest_pitch = 0,
  k_lfo_dest_phoneme,
  k_lfo_dest_timbre,
  k_lfo_dest_harmonics,
  k_lfo_dest_morph,
  k_lfo_dest_speed,
  k_lfo_dest_prosody,
  k_lfo_dest_decay,
  k_lfo_dest_mix,
  k_lfo_dest_detune,
  k_lfo_dest_spread,
  k_lfo_dest_gender,
  k_lfo_dest_attack,
  k_lfo_dest_sustain,
  k_lfo_dest_gliss,
  k_num_lfo_dest
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
static float decay_norm_ = 0.3f;   /* 0..1, Decay knob */
static float attack_norm_ = 0.0f;  /* 0..1, Attack knob */
static float sustain_ = 1.0f;      /* 0..1, Sustain level */
static float mix_ = 0.0f;
static uint16_t model_select_ = 3; /* 0=Naive, 1=SAM, 2=LPC, 3=blend */
static uint16_t gate_mode_ = 0;    /* 0=Trigger, 1=Sustain, 2=Continuous, 3=Staccato */
static uint16_t num_voices_ = 1;   /* 1-4 */
static float detune_ = 0.0f;       /* 0.0-1.0 */
static float spread_ = 0.0f;       /* 0.0-1.0 */
static float gender_ = 0.0f;       /* -1.0 to +1.0 (0 = neutral) */

/* --- Vocal style / key mode / glissando (Phase 3) --- */
static uint16_t style_ = 0;        /* 0..5, see kStyles */
static uint16_t key_mode_ = 0;     /* 0=Normal 1=Syllable 2-5=key-assign variants */
static float gliss_ = 0.0f;        /* 0.0-1.0 glissando amount */

/* --- Assignable LFO (Phase 4) --- */
static uint16_t lfo_shape_ = 0;    /* 0=None 1=Sine 2=Square 3=Saw */
static uint16_t lfo_dest_ = 0;     /* k_lfo_dest_* */
static float lfo_rate_hz_ = 1.0f;  /* 0.05..20 Hz */
static float lfo_depth_ = 0.0f;    /* 0..1 */
static float lfo_phase_ = 0.0f;

/* --- Envelope (Phase 4: ADSR, release = decay time) --- */
enum EnvStage { ENV_IDLE = 0, ENV_ATTACK, ENV_DECAY, ENV_RELEASE };
static uint8_t env_stage_ = ENV_IDLE;

/* Staccato gate generator */
static float burst_phase_ = 0.0f;

/* Smoothed (glissando) state */
static float morph_z_ = 0.5f;      /* smoothed phoneme/morph */
static float note_z_ = 60.0f;      /* smoothed pitch (semitones) */
static bool  smooth_valid_ = false;

/* Syllable playback state */
static float syllable_time_ = 1.0f;   /* seconds since last (re)articulation */
static uint16_t latched_sound_ = 0;   /* vowel/syllable index for key modes */
static uint8_t last_key_note_ = 0xFF; /* key tracking for KeyVow/KeySyl */

/* Style modulation state */
static float vibrato_phase_ = 0.0f;
static float formant_lfo_phase_ = 0.0f;

/* Alien phaser state: [channel][stage] first-order allpass memories */
static float phaser_phase_ = 0.0f;
static float ap_x_[2][2] = {{0, 0}, {0, 0}};
static float ap_y_[2][2] = {{0, 0}, {0, 0}};

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
 * vibrato adds pitch modulation. Robot quantizes pitch to semitones
 * and defaults to the SAM model. Alien snaps phonemes to unusual
 * off-vowel positions, quantizes pitch to a Bohlen-Pierce-style
 * inharmonic step grid and post-processes with distortion + phaser.
 * Religious stacks unison voices in parallel organum intervals
 * (octave/fifth/drone) with slow chant vibrato and vowels biased
 * toward the open a/o/e chant range.
 */
struct StyleSettings {
  float gender_offset;     /* -1..+1 formant shift */
  float pitch_offset;      /* semitones */
  float vibrato_rate;      /* Hz */
  float vibrato_depth;     /* semitones */
  bool  quantize_pitch;    /* robot: stepped 12-TET pitch */
  float formant_lfo_rate;  /* Hz (0 = off) */
  float formant_lfo_depth; /* timbre modulation depth */
  bool  alien_grid;        /* quantize pitch to inharmonic (BP) step grid */
  bool  warp_phonemes;     /* snap morph to unusual off-vowel positions */
  bool  chant_vowels;      /* compress morph into open-vowel chant range */
  float drive;             /* output waveshaper amount (0 = off) */
  float phaser_depth;      /* output allpass phaser mix (0 = off) */
  const float* voice_intervals; /* per-voice semitone offsets (NULL = none) */
};

/* Religious: parallel organum - unison, octave, fifth, sub-octave drone */
static const float kOrganumIntervals[kMaxVoices] = {0.0f, 12.0f, 7.0f, -12.0f};

/* Alien: inharmonic stack on the Bohlen-Pierce step (1.46305 semitones):
 * +5 steps, -13 steps (one "tritave" down), +9 steps */
static const float kAlienIntervals[kMaxVoices] = {0.0f, 7.3152f, -19.0196f, 13.1674f};

static const StyleSettings kStyles[6] = {
  /* Male   */ {-0.50f,  0.0f, 4.5f, 0.04f, false, 0.00f, 0.00f,
                false, false, false, 0.0f, 0.0f, 0},
  /* Female */ { 0.35f,  0.0f, 5.5f, 0.08f, false, 0.00f, 0.00f,
                false, false, false, 0.0f, 0.0f, 0},
  /* Child  */ { 0.70f, 12.0f, 6.0f, 0.12f, false, 0.00f, 0.00f,
                false, false, false, 0.0f, 0.0f, 0},
  /* Robot  */ { 0.00f,  0.0f, 0.0f, 0.00f, true,  0.00f, 0.00f,
                false, false, false, 0.0f, 0.0f, 0},
  /* Alien  */ {-0.20f,  0.0f, 0.0f, 0.00f, false, 0.00f, 0.00f,
                true,  true,  false, 0.6f, 0.5f, kAlienIntervals},
  /* Relig. */ {-0.15f,  0.0f, 0.3f, 0.05f, false, 0.07f, 0.10f,
                false, false, true,  0.0f, 0.0f, kOrganumIntervals},
};
static const uint16_t kNumStyles = 6;

/* Bohlen-Pierce step: 13 equal divisions of the tritave (19.01955 st) */
static const float kAlienStep = 1.46304f;

/* Morph positions approximating the A/E/I/O/U vowels in the engines'
 * phoneme space (morph parameter 0..1). */
static const float kVowelMorph[5] = {0.02f, 0.27f, 0.50f, 0.73f, 0.98f};

/* Alien: deliberately off-vowel morph positions (between-phoneme
 * territory where the formant interpolation produces non-human timbres) */
static const float kAlienMorph[7] = {
  0.09f, 0.16f, 0.38f, 0.44f, 0.61f, 0.86f, 0.93f
};

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

/* Everything that is engine state rather than a parameter value.  Shared by
 * OSC_INIT and OSC_RESET, because the drumlogue's reset is contracted to
 * produce exactly this -- envelopes idle, phases rewound, the LPC and phaser
 * histories cleared -- while leaving parameters alone. */
static void reset_engine_state(void)
{
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
  last_key_note_ = 0xFF;
  vibrato_phase_ = 0.0f;
  formant_lfo_phase_ = 0.0f;
  lfo_phase_ = 0.0f;
  burst_phase_ = 0.0f;
  env_stage_ = ENV_IDLE;
  phaser_phase_ = 0.0f;
  memset(ap_x_, 0, sizeof(ap_x_));
  memset(ap_y_, 0, sizeof(ap_y_));
}

void OSC_INIT(uint32_t platform, uint32_t api)
{
  (void)platform;
  (void)api;

  reset_engine_state();
}

/* Neutral state, on the audio thread between blocks.  See OSC_RESET in
 * drumlogue/userosc.h. */
void OSC_RESET(void)
{
  gate_          = false;
  previous_gate_ = false;
  amp_           = 0.0f;
  shape_lfo_     = 0.0f;
  reset_engine_state();
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
  const StyleSettings &style = kStyles[(style_ < kNumStyles) ? style_ : 0];

  /* ---- Assignable LFO (one value per block) ---- */
  float lfo = 0.0f;
  if (lfo_shape_ != 0 && lfo_depth_ > 0.0f) {
    lfo_phase_ += lfo_rate_hz_ * block_dt;
    if (lfo_phase_ >= 1.0f) lfo_phase_ -= (float)(int)lfo_phase_;
    switch (lfo_shape_) {
      case 1: lfo = sinf(lfo_phase_ * 6.2831853f); break;         /* Sine */
      case 2: lfo = (lfo_phase_ < 0.5f) ? 1.0f : -1.0f; break;    /* Square */
      default: lfo = 2.0f * lfo_phase_ - 1.0f; break;             /* Saw */
    }
    lfo *= lfo_depth_;
  }
  /* Per-destination modulation term (0 unless routed there) */
  #define LFO_MOD(dest) ((lfo_dest_ == (dest)) ? lfo : 0.0f)

  /* Effective (LFO-modulated) parameter values for this block */
  const float speed_eff   = clipminmaxf(0.0f, speed_ + 2.0f * LFO_MOD(k_lfo_dest_speed), 2.0f);
  const float prosody_eff = clip01f(prosody_ + LFO_MOD(k_lfo_dest_prosody));
  const float mix_eff     = clip01f(mix_ + LFO_MOD(k_lfo_dest_mix));
  const float detune_eff  = clip01f(detune_ + LFO_MOD(k_lfo_dest_detune));
  const float spread_eff  = clip01f(spread_ + LFO_MOD(k_lfo_dest_spread));
  const float gender_eff  = clipminmaxf(-1.0f, gender_ + 2.0f * LFO_MOD(k_lfo_dest_gender), 1.0f);
  const float shape_eff   = clip01f(shape_ + LFO_MOD(k_lfo_dest_phoneme));
  float gliss_eff         = clip01f(gliss_ + LFO_MOD(k_lfo_dest_gliss));
  const float sustain_eff = clip01f(sustain_ + LFO_MOD(k_lfo_dest_sustain));
  const float decay_eff   = clip01f(decay_norm_ + LFO_MOD(k_lfo_dest_decay));
  const float attack_eff  = clip01f(attack_norm_ + LFO_MOD(k_lfo_dest_attack));

  /* One-pole envelope coefficients from normalized times.
   * Exponential ranges: attack 1ms..2s, decay/release 5ms..5s.
   * (The previous linear alpha mapping topped out around 42ms, which
   * is why the Decay knob seemed to do nothing and notes ended
   * abruptly.) */
  const float attack_tau = 0.001f * expf(attack_eff * 7.6009f); /* ln(2000) */
  const float decay_tau  = 0.005f * expf(decay_eff * 6.9078f);  /* ln(1000) */
  const float attack_alpha = 1.0f / (attack_tau * 48000.0f);
  const float decay_alpha  = 1.0f / (decay_tau * 48000.0f);

  /* Religious: chant needs a minimum of legato even with Gliss at 0 */
  if (style.chant_vowels && gliss_eff < 0.15f) gliss_eff = 0.15f;

  /* Raw pitch from adapter (integer part = played MIDI key) */
  const uint8_t key_note = (uint8_t)(params->pitch >> 8);
  float note_target =
      ((float)key_note) +
      ((params->pitch & 0xFF) * k_note_mod_fscale) +
      style.pitch_offset;

  /* Alien: quantize the pitch target to an inharmonic step grid
   * (13 equal divisions of the tritave). Applied before glissando so
   * Gliss glides between the alien steps. */
  if (style.alien_grid) {
    note_target = (float)(int)(note_target / kAlienStep + 0.5f) * kAlienStep;
  }

  /* ---- Gate logic ---- */
  bool triggered = false;
  {
    bool effective_gate = gate_;
    if (gate_mode_ == 2) {          /* Continuous: always on */
      effective_gate = true;
    } else if (gate_mode_ == 3) {   /* Staccato: free-running gate bursts */
      const float burst_rate = 1.5f + speed_eff * 6.0f;  /* 1.5..13.5 Hz */
      burst_phase_ += burst_rate * block_dt;
      if (burst_phase_ >= 1.0f) burst_phase_ -= (float)(int)burst_phase_;
      effective_gate = burst_phase_ < 0.6f;
    }

    if (effective_gate && !previous_gate_) {
      parameters_.trigger = plaits::TRIGGER_RISING_EDGE;
      triggered = true;
      env_stage_ = ENV_ATTACK;
    } else {
      parameters_.trigger = plaits::TRIGGER_LOW;
      if (!effective_gate && previous_gate_) {
        env_stage_ = ENV_RELEASE;
      }
    }
    if (gate_mode_ == 2) {
      /* Continuous: run the engine free (TRIGGER_UNPATCHED). In the LPC
       * word region this selects Plaits' scan mode - Phoneme/Morph scrub
       * through the word bank as an evolving drone - instead of playing
       * the word once and then holding its (silent) last frame forever. */
      parameters_.trigger = (plaits::TriggerState)(
          parameters_.trigger | plaits::TRIGGER_UNPATCHED);
    }
    /* A mode switch can land here with the gate high but the envelope
     * released (e.g. entering Continuous right after a note ended) -
     * without a fresh rising edge the note would stay silent forever. */
    if (effective_gate &&
        (env_stage_ == ENV_RELEASE || env_stage_ == ENV_IDLE)) {
      env_stage_ = ENV_ATTACK;
    }
    previous_gate_ = effective_gate;
  }

  /* On trigger or key change: (re)latch the key-assigned vowel/syllable.
   * Latching on key change too is what makes KeyVow/KeySyl follow legato
   * notes and Base Note changes that arrive without a fresh gate. */
  {
    const bool key_changed = (key_note != last_key_note_);
    last_key_note_ = key_note;

    if (triggered) {
      syllable_time_ = 0.0f;
    }
    if (triggered || (key_changed && key_mode_ >= 2)) {
      switch (key_mode_) {
        case 2: latched_sound_ = key_note % 5; break;        /* KeyVow A */
        case 3: latched_sound_ = (key_note + 3) % 5; break;  /* KeyVow B (transposed) */
        case 4: latched_sound_ = key_note % 8; break;        /* KeySyl C */
        case 5: latched_sound_ = (key_note + 4) % 8; break;  /* KeySyl D (transposed) */
        default: break;
      }
      /* New key = new syllable: re-articulate the consonant->vowel glide */
      if (key_changed && !triggered && key_mode_ >= 4) {
        syllable_time_ = 0.0f;
      }
    }
  }

  /* Morph fine modulation from the Morph parameter (id4) */
  const float morph_mod = p_values_[k_user_osc_param_id2] * 0.01f - 0.5f
      + LFO_MOD(k_lfo_dest_morph);

  /* ---- Phoneme source selection (Key Mode) ---- */
  float morph_target;
  switch (key_mode_) {
    default:
    case 0: /* Normal: Phoneme knob + LFO + Morph */
      morph_target = clip01f(shape_eff + shape_lfo_ + morph_mod);
      break;

    case 1: { /* Syllable: Phoneme knob selects one of 8 syllables */
      uint16_t idx = (uint16_t)(clip01f(shape_eff + shape_lfo_) * 7.999f);
      const Syllable &syl = kSyllables[idx];
      /* Consonant->vowel transition time scales with Gliss */
      const float t_trans = 0.03f + gliss_eff * 0.25f;
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
      const float t_trans = 0.03f + gliss_eff * 0.25f;
      float s = (syllable_time_ >= t_trans) ? 1.0f : syllable_time_ / t_trans;
      s = s * s * (3.0f - 2.0f * s);
      morph_target = clip01f(syl.consonant + (syl.vowel - syl.consonant) * s
                             + morph_mod * 0.25f);
      break;
    }
  }
  syllable_time_ += block_dt;

  /* Alien: snap the phoneme target to unusual off-vowel positions.
   * The glissando smoothing below turns the snaps into slides. */
  if (style.warp_phonemes) {
    uint16_t widx = (uint16_t)(morph_target * 6.999f);
    morph_target = kAlienMorph[widx];
  }

  /* Religious: compress into the open-vowel chant range (a/o/e) */
  if (style.chant_vowels) {
    morph_target = 0.12f + morph_target * 0.62f;
  }

  /* ---- Glissando (smooth passage between phonemes and pitches) ----
   * One-pole glide toward the targets; time constant 0..~0.5s. */
  if (!smooth_valid_) {
    morph_z_ = morph_target;
    note_z_ = note_target;
    smooth_valid_ = true;
  }
  if (gliss_eff > 0.001f) {
    const float tau = 0.02f + gliss_eff * 0.5f;   /* seconds */
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

  /* LFO on pitch: applied post-gliss, like a vibrato (±12 st at 100%) */
  note_final += LFO_MOD(k_lfo_dest_pitch) * 12.0f;

  float style_timbre_mod = 0.0f;
  if (style.formant_lfo_depth > 0.0f) {
    formant_lfo_phase_ += style.formant_lfo_rate * block_dt;
    if (formant_lfo_phase_ >= 1.0f) formant_lfo_phase_ -= 1.0f;
    style_timbre_mod = sinf(formant_lfo_phase_ * 6.2831853f) * style.formant_lfo_depth;
  }

  parameters_.note = note_final;
  parameters_.morph = clip01f(morph_z_);
  parameters_.timbre = clip01f(shiftshape_ + style_timbre_mod
                               + LFO_MOD(k_lfo_dest_timbre)); /* Timbre → vocal register/formant */

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
    /* Blend mode: Param 1 (id3) controls harmonics, scaled 0-100 -> 0.0-1.0.
     * The LFO can only reach Harmonics in Blend mode: in forced-model
     * mode the value is pinned to keep the engine on its cheap path. */
    parameters_.harmonics = clip01f(p_values_[k_user_osc_param_id1] * 0.01f
                                    + LFO_MOD(k_lfo_dest_harmonics));
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

  const float detune_semitones = detune_eff * 0.15f; /* max ±15 cents */
  const float voice_gain = 1.0f / sqrtf((float)num_voices_);
  const uint16_t vi = num_voices_ - 1; /* table index */
  bool any_enveloped = false;

  /* Precompute per-voice pan gains (avoid sqrtf inside the voice loop) */
  float gain_l[kMaxVoices], gain_r[kMaxVoices];
  for (uint16_t v = 0; v < num_voices_; ++v) {
    float pan = 0.5f + (kVoicePan[vi][v] - 0.5f) * spread_eff;
    gain_l[v] = sqrtf(1.0f - pan) * voice_gain;
    gain_r[v] = sqrtf(pan) * voice_gain;
    /* Set engine params once (invariant across frames) */
    engines_[v].set_prosody_amount(prosody_eff);
    engines_[v].set_speed(speed_eff);
  }

  for (uint16_t v = 0; v < num_voices_; ++v) {
    plaits::EngineParameters vp = parameters_;
    vp.harmonics = voice_harmonics_[v];

    /* Per-voice detune, plus style intervals (organum / alien stack) */
    vp.note += detune_semitones * kVoiceDetune[vi][v];
    if (style.voice_intervals) {
      vp.note += style.voice_intervals[v];
    }

    /* Per-voice gender (formant shift via timbre offset), including
     * the style's gender/formant character */
    vp.timbre = clip01f(vp.timbre + (gender_eff + style.gender_offset) * 0.5f);

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
      float mixed = stmlib::Crossfade(vout[i], vaux[i], mix_eff);
      left[i]  += mixed * gl;
      right[i] += mixed * gr;
    }
  }

  /* ---- Alien post-processing: soft-clip drive + swept allpass phaser ---- */
  if (style.drive > 0.0f) {
    const float d = style.drive;
    const float post = 1.0f / (1.0f + 0.6f * d); /* keep loudness in check */
    for (uint32_t i = 0; i < nframes; ++i) {
      float l = left[i], r = right[i];
      float al = (l < 0.0f) ? -l : l;
      float ar = (r < 0.0f) ? -r : r;
      left[i]  = l * (1.0f + d) / (1.0f + d * al) * post;
      right[i] = r * (1.0f + d) / (1.0f + d * ar) * post;
    }
  }
  if (style.phaser_depth > 0.0f) {
    phaser_phase_ += 0.25f * block_dt; /* slow sweep */
    if (phaser_phase_ >= 1.0f) phaser_phase_ -= 1.0f;
    const float a = 0.35f + 0.4f * (0.5f + 0.5f * sinf(phaser_phase_ * 6.2831853f));
    const float wet = style.phaser_depth;
    float *chan[2] = {left, right};
    for (int c = 0; c < 2; ++c) {
      float *x = chan[c];
      float x1a = ap_x_[c][0], y1a = ap_y_[c][0];
      float x1b = ap_x_[c][1], y1b = ap_y_[c][1];
      for (uint32_t i = 0; i < nframes; ++i) {
        const float in = x[i];
        /* two cascaded first-order allpasses */
        float ya = -a * in + x1a + a * y1a;
        x1a = in;  y1a = ya;
        float yb = -a * ya + x1b + a * y1b;
        x1b = ya;  y1b = yb;
        x[i] = in + (yb - in) * wet;  /* dry+shifted mix -> moving notches */
      }
      ap_x_[c][0] = x1a; ap_y_[c][0] = y1a;
      ap_x_[c][1] = x1b; ap_y_[c][1] = y1b;
    }
  }

  /* ---- ADSR envelope + output gain ----
   * Applied unconditionally: the LPC word replay envelopes its own
   * energy contour (any_enveloped), but its controller holds the last
   * frame forever after the word ends, so without the ADSR a note-off
   * in the word region would never end the note. */
  (void)any_enveloped;
  {
    const float out_gain = 0.8f;
    /* Decay target: Trigger mode is a one-shot AD (falls to zero even
     * while the gate is held); the other modes decay to the Sustain
     * level. Release always uses the Decay time. */
    const float decay_target = (gate_mode_ == 0) ? 0.0f : sustain_eff;
    for (uint32_t i = 0; i < nframes; ++i) {
      switch (env_stage_) {
        case ENV_ATTACK:
          amp_ += (1.05f - amp_) * attack_alpha;
          if (amp_ >= 1.0f) {
            amp_ = 1.0f;
            env_stage_ = ENV_DECAY;
          }
          break;
        case ENV_DECAY:
          amp_ += (decay_target - amp_) * decay_alpha;
          break;
        case ENV_RELEASE:
          amp_ += (0.0f - amp_) * decay_alpha;
          if (amp_ < 0.0001f) {
            amp_ = 0.0f;
            env_stage_ = ENV_IDLE;
          }
          break;
        default: /* ENV_IDLE */
          amp_ += (0.0f - amp_) * 0.01f;
          break;
      }
      float g = amp_ * out_gain;
      left[i]  *= g;
      right[i] *= g;
    }
  }

  s_stereo_frames_ = nframes;

  #undef LFO_MOD

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

    case k_mussola_param_decay: /* Decay: 0-100 -> decay/release time */
      decay_norm_ = value * 0.01f;
      break;

    case k_mussola_param_mix: /* Mix: 0-100 -> 0.0-1.0 */
      mix_ = value * 0.01f;
      break;

    case k_mussola_param_model: /* Model: 0-3 (Naive/SAM/LPC/Blend) */
      model_select_ = (value > 3) ? 3 : value;
      break;

    case k_mussola_param_gate_mode: /* Gate Mode: 0-3 */
      gate_mode_ = (value > 3) ? 3 : value;
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

    case k_mussola_param_attack: /* Attack: 0-100 -> 0.0-1.0 (0 = instant) */
      attack_norm_ = value * 0.01f;
      break;

    case k_mussola_param_style: /* Style: 0-5 */
      style_ = (value >= kNumStyles) ? (kNumStyles - 1) : value;
      break;

    case k_mussola_param_key_mode: /* Key Mode: 0-5 */
      key_mode_ = (value > 5) ? 5 : value;
      break;

    case k_mussola_param_gliss: /* Gliss: 0-100 -> 0.0-1.0 */
      gliss_ = value * 0.01f;
      break;

    case k_mussola_param_sustain: /* Sustain: 0-100 -> 0.0-1.0 */
      sustain_ = value * 0.01f;
      break;

    case k_mussola_param_lfo_shape: /* LFO Shape: 0-3 None/Sine/Square/Saw */
      lfo_shape_ = (value > 3) ? 3 : value;
      break;

    case k_mussola_param_lfo_dest: /* LFO Dest: 0-14 */
      lfo_dest_ = (value >= k_num_lfo_dest) ? (k_num_lfo_dest - 1) : value;
      break;

    case k_mussola_param_lfo_rate: /* LFO Rate: 0-100 -> 0.05..20 Hz (exp) */
      lfo_rate_hz_ = 0.05f * expf(value * 0.01f * 5.99146f); /* ln(400) */
      break;

    case k_mussola_param_lfo_depth: /* LFO Depth: 0-100 -> 0.0-1.0 */
      lfo_depth_ = value * 0.01f;
      break;

    default:
      break;
  }
}
