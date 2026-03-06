/*
 * Mussola - Abstract Vocal Synth Engine for drumlogue
 *
 * Based on Mutable Instruments Plaits SpeechEngine.
 * Produces abstract choral vocalizations (not realistic speech).
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
 *   id 4:  Morph      (0-100% -> morph within current model)
 *   id 5:  Speed      (0-100% -> LPC playback speed, centered at 50)
 *   id 6:  Prosody    (0-100% -> prosody replay amount for LPC words)
 *   id 7:  Decay      (0-100% -> envelope decay time)
 *   id 8:  Mix        (0-100% -> main/aux output crossfade)
 *   id 9:  Model      (0-2 -> force Naive/SAM/LPC, 3=blend)
 *   id 10: Gate Mode  (0-2 -> Trigger/Sustain/Continuous)
 *
 * Output: Mono Q31 (main output, with envelope)
 */

#include "userosc.h"
#include "stmlib/dsp/dsp.h"

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/engine/speech_engine.h"

/* --- Static state --- */
static plaits::SpeechEngine engine_;
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
static float mix_ = 0.0f;
static uint16_t model_select_ = 3; /* 0=Naive, 1=SAM, 2=LPC, 3=blend */
static uint16_t gate_mode_ = 0;    /* 0=Trigger, 1=Sustain, 2=Continuous */

/*
 * Engine buffer: SpeechEngine.Init() uses BufferAllocator for:
 *   - LPCSpeechSynthWordBank internal buffers (~4KB)
 *   - 2 × kMaxBlockSize float temp buffers (2 × 32 × 4 = 256 bytes)
 * Total ~5KB is sufficient, allocate 8KB for safety.
 */
static const size_t kEngineBufferSize = 8192;
static uint8_t engine_buffer_[kEngineBufferSize];

/* ======================================================================
 * OSC API Implementation
 * ==================================================================== */

void OSC_INIT(uint32_t platform, uint32_t api)
{
  (void)platform;
  (void)api;

  stmlib::BufferAllocator allocator;
  allocator.Init(engine_buffer_, kEngineBufferSize);
  engine_.Init(&allocator);
  engine_.set_prosody_amount(0.0f);
  engine_.set_speed(1.0f);

  parameters_.trigger = plaits::TRIGGER_UNPATCHED;
  parameters_.note = 60.0f;
  parameters_.timbre = 0.5f;
  parameters_.morph = 0.5f;
  parameters_.harmonics = 0.0f;
  parameters_.accent = 0.5f;

  amp_ = 0.0f;
  gate_ = false;
  previous_gate_ = false;
  prosody_ = 0.0f;
  speed_ = 1.0f;
  decay_alpha_ = 0.002f;
  mix_ = 0.0f;
  model_select_ = 3;
  gate_mode_ = 0;
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
  static float out[plaits::kMaxBlockSize], aux[plaits::kMaxBlockSize];
  static bool enveloped;

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

  /* Map parameters */
  parameters_.timbre = clip01f(shape_);                    /* Phoneme */
  parameters_.morph = clip01f(shiftshape_);                /* Timbre/register */

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

  /* Morph param (id4) adds to morph */
  float morph_mod = p_values_[k_user_osc_param_id2] * 0.01f;
  parameters_.morph = clip01f(parameters_.morph + (morph_mod - 0.5f));

  parameters_.accent = 0.8f;

  /* Update engine speed/prosody */
  engine_.set_prosody_amount(prosody_);
  engine_.set_speed(speed_);

  /* Render */
  enveloped = false;
  engine_.Render(parameters_, out, aux, plaits::kMaxBlockSize, &enveloped);

  /* Apply envelope if engine didn't */
  if (!enveloped) {
    float target, alpha;
    switch (gate_mode_) {
      default:
      case 0: /* Trigger */
        target = gate_ ? 1.0f : 0.0f;
        alpha = gate_ ? 0.05f : decay_alpha_;
        break;
      case 1: /* Sustain */
        target = gate_ ? 1.0f : 0.0f;
        alpha = gate_ ? 0.1f : 0.01f;
        break;
      case 2: /* Continuous */
        target = 1.0f;
        alpha = 0.05f;
        break;
    }
    for (size_t i = 0; i < plaits::kMaxBlockSize; ++i) {
      amp_ += (target - amp_) * alpha;
      out[i] *= amp_;
      aux[i] *= amp_;
    }
  }

  /* Mix out/aux and convert to Q31 */
  const float out_gain = 0.8f;
  const float aux_gain = 0.8f;
#ifdef __ARM_NEON
  {
    const float32x4_t vscale = vdupq_n_f32(2147483648.0f);
    const float32x4_t vmin = vdupq_n_f32(-1.0f);
    const float32x4_t vmax = vdupq_n_f32(1.0f);
    const float32x4_t vout_gain = vdupq_n_f32(out_gain);
    const float32x4_t vaux_gain = vdupq_n_f32(aux_gain);
    const float32x4_t vmix = vdupq_n_f32(mix_);
    size_t i = 0;
    for (; i + 4 <= plaits::kMaxBlockSize; i += 4) {
      float32x4_t o = vmulq_f32(vld1q_f32(out + i), vout_gain);
      float32x4_t a = vmulq_f32(vld1q_f32(aux + i), vaux_gain);
      float32x4_t v = vmlaq_f32(o, vsubq_f32(a, o), vmix);
      v = vmaxq_f32(vminq_f32(v, vmax), vmin);
      int32x4_t q = vcvtq_s32_f32(vmulq_f32(v, vscale));
      vst1q_s32(yn + i, q);
    }
    for (; i < plaits::kMaxBlockSize; ++i) {
      float o2 = out[i] * out_gain, a2 = aux[i] * aux_gain;
      yn[i] = f32_to_q31(stmlib::Crossfade(o2, a2, mix_));
    }
  }
#else
  for (size_t i = 0; i < plaits::kMaxBlockSize; ++i) {
    float o = out[i] * out_gain, a = aux[i] * aux_gain;
    yn[i] = f32_to_q31(stmlib::Crossfade(o, a, mix_));
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

    case 8: /* Speed: 0-100 -> 0.0-2.0 (50 = 1.0 normal) */
      speed_ = value * 0.02f;
      break;

    case 9: /* Prosody: 0-100 -> 0.0-1.0 */
      prosody_ = value * 0.01f;
      break;

    case 10: /* Decay: 0-100 -> decay alpha (0.0005 to 0.05) */
      decay_alpha_ = 0.0005f + value * 0.0005f;
      break;

    case 11: /* Mix: 0-100 -> 0.0-1.0 */
      mix_ = value * 0.01f;
      break;

    case 12: /* Model: 0-3 (Naive/SAM/LPC/Blend) */
      model_select_ = (value > 3) ? 3 : value;
      break;

    case 13: /* Gate Mode: 0-2 */
      gate_mode_ = (value > 2) ? 2 : value;
      break;

    default:
      break;
  }
}
