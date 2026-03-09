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

/*
 * Engine buffers: each SpeechEngine.Init() uses BufferAllocator for:
 *   - LPCSpeechSynthWordBank internal buffers (~4KB)
 *   - 2 × kMaxBlockSize float temp buffers (2 × 32 × 4 = 256 bytes)
 * 8KB per engine × 4 engines = 32KB total.
 */
static const size_t kEngineBufferSize = 8192;
static uint8_t engine_buffers_[kMaxVoices][kEngineBufferSize];

/* Stereo output buffers filled by OSC_CYCLE, read by adapter */
static float s_stereo_left_[plaits::kMaxBlockSize] __attribute__((aligned(16)));
static float s_stereo_right_[plaits::kMaxBlockSize] __attribute__((aligned(16)));

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
 * OSC API Implementation
 * ==================================================================== */

void OSC_INIT(uint32_t platform, uint32_t api)
{
  (void)platform;
  (void)api;

  for (uint16_t v = 0; v < kMaxVoices; ++v) {
    stmlib::BufferAllocator allocator;
    allocator.Init(engine_buffers_[v], kEngineBufferSize);
    engines_[v].Init(&allocator);
    engines_[v].set_prosody_amount(0.0f);
    engines_[v].set_speed(1.0f);
  }

  parameters_.trigger = plaits::TRIGGER_UNPATCHED;
  parameters_.note = 60.0f;
  parameters_.timbre = 0.5f;
  parameters_.morph = 0.5f;
  parameters_.harmonics = 0.0f;
  parameters_.accent = 0.5f;
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
  (void)frames;

  shape_lfo_ = q31_to_f32(params->shape_lfo);

  /* Pitch from adapter */
  parameters_.note =
      ((float)(params->pitch >> 8)) +
      ((params->pitch & 0xFF) * k_note_mod_fscale);

  /* Trigger logic */
  {
    bool effective_gate = gate_;
    if (gate_mode_ == 2) /* Continuous */
      effective_gate = true;

    if (effective_gate && !previous_gate_) {
      parameters_.trigger = plaits::TRIGGER_RISING_EDGE;
    } else {
      parameters_.trigger = plaits::TRIGGER_LOW;
    }
    previous_gate_ = effective_gate;
  }

  /* Map parameters — shape_lfo_ modulates Phoneme (phoneme selection) */
  parameters_.morph = clip01f(shape_ + shape_lfo_);        /* Phoneme + LFO → phoneme/vowel */
  parameters_.timbre = clip01f(shiftshape_);               /* Timbre → vocal register/formant */

  /* Harmonics controls model blend (0-1)
   * Model select overrides: force harmonics into the sub-range for that model */
  if (model_select_ < 3) {
    /* Force: 0=Naive(0.0), 1=SAM(0.17), 2=LPC(0.5) */
    static const float model_harmonics[] = {0.0f, 0.17f, 0.5f};
    parameters_.harmonics = model_harmonics[model_select_];
  } else {
    /* Blend mode: Param 1 (id3) controls harmonics, scaled 0-100 -> 0.0-1.0 */
    parameters_.harmonics = clip01f(p_values_[k_user_osc_param_id1] * 0.01f);
  }

  /* Morph param (id4) further modulates phoneme selection */
  float morph_mod = p_values_[k_user_osc_param_id2] * 0.01f;
  parameters_.morph = clip01f(parameters_.morph + (morph_mod - 0.5f));

  parameters_.accent = 0.8f;

  /* ---- Multi-voice rendering ---- */
  float left[plaits::kMaxBlockSize] __attribute__((aligned(16)));
  float right[plaits::kMaxBlockSize] __attribute__((aligned(16)));
  memset(left, 0, sizeof(left));
  memset(right, 0, sizeof(right));

  const float detune_semitones = detune_ * 0.15f; /* max ±15 cents */
  const float voice_gain = 1.0f / sqrtf((float)num_voices_);
  const uint16_t vi = num_voices_ - 1; /* table index */
  bool any_enveloped = false;

  for (uint16_t v = 0; v < num_voices_; ++v) {
    plaits::EngineParameters vp = parameters_;

    /* Per-voice detune */
    vp.note += detune_semitones * kVoiceDetune[vi][v];

    /* Per-voice gender (formant shift via timbre offset) */
    vp.timbre = clip01f(vp.timbre + gender_ * 0.5f);

    /* Update engine speed/prosody */
    engines_[v].set_prosody_amount(prosody_);
    engines_[v].set_speed(speed_);

    /* Render this voice */
    float vout[plaits::kMaxBlockSize], vaux[plaits::kMaxBlockSize];
    bool venveloped = false;
    engines_[v].Render(vp, vout, vaux, plaits::kMaxBlockSize, &venveloped);
    if (venveloped) any_enveloped = true;

    /* Pan position: interpolate toward center when spread=0 */
    float pan = 0.5f + (kVoicePan[vi][v] - 0.5f) * spread_;
    float gain_l = (1.0f - pan) * voice_gain;
    float gain_r = pan * voice_gain;

    /* Mix out/aux per voice, accumulate into L/R */
    for (size_t i = 0; i < plaits::kMaxBlockSize; ++i) {
      float mixed = stmlib::Crossfade(vout[i], vaux[i], mix_);
      left[i]  += mixed * gain_l;
      right[i] += mixed * gain_r;
    }
  }

  /* Apply envelope if no engine did */
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
    for (size_t i = 0; i < plaits::kMaxBlockSize; ++i) {
      amp_ += (target - amp_) * alpha;
      left[i]  *= amp_;
      right[i] *= amp_;
    }
  }

  /* Apply output gain */
  const float out_gain = 0.8f;
  for (size_t i = 0; i < plaits::kMaxBlockSize; ++i) {
    left[i]  *= out_gain;
    right[i] *= out_gain;
  }

  /* Store stereo for adapter's stereo path */
  memcpy(s_stereo_left_, left, sizeof(float) * plaits::kMaxBlockSize);
  memcpy(s_stereo_right_, right, sizeof(float) * plaits::kMaxBlockSize);

  /* Output mono Q31 (L+R average) as fallback */
  for (size_t i = 0; i < plaits::kMaxBlockSize; ++i) {
    float mono = (left[i] + right[i]) * 0.5f;
    yn[i] = f32_to_q31(mono);
  }
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
      num_voices_ = (value < 1) ? 1 : (value > kMaxVoices) ? kMaxVoices : value;
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

    default:
      break;
  }
}
