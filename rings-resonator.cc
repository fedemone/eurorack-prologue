/*
 * Rings resonator port for drumlogue
 *
 * Port of Mutable Instruments Rings (physical modelling resonator)
 * to the drumlogue User Oscillator API.
 *
 * Rings runs at 48 kHz natively — matching drumlogue's sample rate exactly,
 * so no sample-rate conversion is needed (unlike Elements at 24 kHz).
 *
 * Output: Rings produces stereo (out + aux). We interleave them into
 * the Q31 output buffer as L/R pairs (same pattern as Elements/modal-strike).
 *
 * Models:
 *   0 = Modal resonator (struck object)
 *   1 = Sympathetic strings
 *   2 = Inharmonic string (with dispersion)
 *   3 = FM voice
 *   4 = Sympathetic strings (quantized chords)
 *   5 = String + reverb
 */

#include "userosc.h"
#include "stmlib/dsp/dsp.h"

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

#include "rings/dsp/part.h"
#include "rings/resources.h"

using namespace rings;

/* --- Static allocations --- */

static Part part_;
static uint16_t reverb_buffer_[32768];

static rings::Patch patch_;
static rings::PerformanceState performance_state_;

static alignas(16) float in_buffer_[kMaxBlockSize];
static alignas(16) float out_buffer_[kMaxBlockSize];
static alignas(16) float aux_buffer_[kMaxBlockSize];

static bool gate_ = false;
static bool previous_gate_ = false;

/* User-facing parameter storage */
uint16_t p_values[k_num_user_osc_param_id] = {0};
float shape = 0, shiftshape = 0;
float shape_lfo = 0;

/* Custom param indices beyond standard user_osc_param_id_t range */
static uint16_t model_value = 0;
static uint16_t polyphony_value = 0;

/* ======================================================================
 * OSC API Implementation
 * ==================================================================== */

void OSC_INIT(uint32_t platform, uint32_t api)
{
  (void)platform;
  (void)api;

  part_.Init(reverb_buffer_);
  part_.set_model(RESONATOR_MODEL_MODAL);
  part_.set_polyphony(1);

  patch_.structure  = 0.5f;
  patch_.brightness = 0.5f;
  patch_.damping    = 0.5f;
  patch_.position   = 0.5f;

  performance_state_.strum            = false;
  performance_state_.internal_exciter = true;
  performance_state_.internal_strum   = true;
  performance_state_.internal_note    = false;
  performance_state_.tonic            = 0.0f;
  performance_state_.note             = 69.0f;
  performance_state_.fm               = 0.0f;
  performance_state_.chord            = 0;

  previous_gate_ = false;

  std::fill(&in_buffer_[0], &in_buffer_[kMaxBlockSize], 0.0f);
}

void OSC_CYCLE(const user_osc_param_t *const params, int32_t *yn, const uint32_t frames)
{
  (void)frames;

  shape_lfo = q31_to_f32(params->shape_lfo);

  /* Pitch from adapter (note.fraction encoding) */
  performance_state_.note =
      ((float)(params->pitch >> 8)) +
      ((params->pitch & 0xFF) * k_note_mod_fscale);

  /* Patch parameters */
  patch_.position   = clip01f(shape);
  patch_.structure  = clip01f(shiftshape);
  patch_.brightness = clip01f(p_values[k_user_osc_param_id1] * 0.01f);
  patch_.damping    = clip01f(p_values[k_user_osc_param_id2] * 0.01f);

  /* Chord from param */
  performance_state_.chord = p_values[k_user_osc_param_id3];
  if (performance_state_.chord >= kNumChords)
    performance_state_.chord = kNumChords - 1;

  /* Gate / strum detection */
  performance_state_.strum = (gate_ && !previous_gate_);
  previous_gate_ = gate_;

  /* Clear input (internal exciter mode) */
  std::fill(&in_buffer_[0], &in_buffer_[kMaxBlockSize], 0.0f);

  /* Process Rings */
  part_.Process(
      performance_state_, patch_,
      in_buffer_, out_buffer_, aux_buffer_,
      kMaxBlockSize);

  /* Mix stereo (out + aux) to mono Q31.
   * The adapter expects exactly kMaxBlockSize mono Q31 samples. */
#ifdef __ARM_NEON
  {
    const float32x4_t vhalf = vdupq_n_f32(0.5f);
    const float32x4_t vscale = vdupq_n_f32(2147483648.0f);
    const float32x4_t vmin = vdupq_n_f32(-1.0f);
    const float32x4_t vmax = vdupq_n_f32(1.0f);
    size_t i = 0;
    for (; i + 4 <= kMaxBlockSize; i += 4) {
      float32x4_t l = vld1q_f32(out_buffer_ + i);
      float32x4_t r = vld1q_f32(aux_buffer_ + i);
      float32x4_t m = vmulq_f32(vaddq_f32(l, r), vhalf);
      m = vmaxq_f32(vminq_f32(m, vmax), vmin);
      int32x4_t q = vcvtq_s32_f32(vmulq_f32(m, vscale));
      vst1q_s32(yn + i, q);
    }
    for (; i < kMaxBlockSize; ++i) {
      yn[i] = f32_to_q31((out_buffer_[i] + aux_buffer_[i]) * 0.5f);
    }
  }
#else
  for (size_t i = 0; i < kMaxBlockSize; ++i) {
    yn[i] = f32_to_q31((out_buffer_[i] + aux_buffer_[i]) * 0.5f);
  }
#endif
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

void OSC_PARAM(uint16_t index, uint16_t value)
{
  switch (index) {
    case k_user_osc_param_id1:
    case k_user_osc_param_id2:
    case k_user_osc_param_id3:
    case k_user_osc_param_id4:
    case k_user_osc_param_id5:
    case k_user_osc_param_id6:
      p_values[index] = value;
      break;

    case k_user_osc_param_shape:
      shape = param_val_to_f32(value);
      break;

    case k_user_osc_param_shiftshape:
      shiftshape = param_val_to_f32(value);
      break;

    /* Custom params passed by the wrapper */
    case 8: /* Model (0-5) */
      model_value = value;
      if (value < RESONATOR_MODEL_LAST)
        part_.set_model(static_cast<ResonatorModel>(value));
      break;

    case 9: /* Polyphony (1-4) */
      polyphony_value = value;
      if (value >= 1 && value <= kMaxPolyphony)
        part_.set_polyphony(value);
      break;

    default:
      break;
  }
}
