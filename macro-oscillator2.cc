#include "userosc.h"
#include "stmlib/dsp/dsp.h"

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif
#include <math.h>
#include "stmlib/utils/random.h"
#include "plaits/dsp/dsp.h"
#include "plaits/dsp/engine/engine.h"

uint16_t p_values[k_num_user_osc_param_id] = {0};
float shape = 0, shiftshape = 0, shape_lfo = 0, lfo2 = 0, mix = 0;
bool gate = false, previous_gate = false;
static float amp = 0.0f;
static float lfo2_phase = 0.0f;
static uint16_t lfo1_shape_value = 0;
static uint16_t lfo2_shape_value = 0;
static uint16_t gate_mode_value = 0;

/* LFO1's depth, 0-100.  On prologue there is no such parameter -- LFO1 is the
 * host's shape LFO and arrives at whatever the panel sends -- so the default
 * is full scale and that platform simply never writes it. */
static uint16_t lfo1_depth_value = 100;

/* Semitones of pitch modulation at full LFO swing, for the Pitch destination.
 * This was fixed at half a semitone, which is vibrato and nothing else; a
 * whole tone is vibrato too, 12 is an octave sweep, and no one value serves
 * both. Defaults to 1 -- twice the old fixed depth, because a semitone is a
 * representable default and half of one is not, and the knob is now there to
 * put it wherever you want. Range is checked in OSC_PARAM rather than
 * trusted, because it scales a value that becomes a pitch. */
#define kPitchRangeSemitonesMax 24
static uint16_t pitch_range_semitones_ = 1;

enum GateMode {
  GateModeTrigger = 0,   /* One-shot envelope per gate (default) */
  GateModeSustain = 1,   /* Hold while gate on, release on off */
  GateModeContinuous = 2 /* Always full amplitude (drone) */
};

/* LFO waveshape transfer function for LFO1 (shape LFO modulation).
 * Shapes the incoming modulation value through different curves. */
static inline float apply_lfo1_shape(float x) {
  switch (lfo1_shape_value) {
    default:
    case 0: /* Cosine: pass-through (linear response) */
      return x;
    case 1: /* Triangle: S-curve (steeper mid-range, gentler extremes) */
      { float ax = x < 0.f ? -x : x;
        float s = ax * (2.0f - ax);
        return x < 0.f ? -s : s; }
    case 2: /* Ramp Up: quadratic (gentle start, steep end) */
      return x < 0.f ? -(x * x) : (x * x);
    case 3: /* Ramp Down: inverse quadratic (steep start, gentle end) */
      { if (x > 0.f) { float s = 1.0f - x; return 1.0f - s * s; }
        if (x < 0.f) { float s = 1.0f + x; return -(1.0f - s * s); }
        return 0.f; }
    case 4: /* Fat Sine: soft-clip / fatten (boosted mid-range) */
      return clip1m1f(x * (1.5f - 0.5f * x * x));
  }
}

plaits::EngineParameters parameters = {
    .trigger = plaits::TRIGGER_UNPATCHED,
    .note = 0,
    .timbre = 0,
    .morph = 0,
    .harmonics = 0,
    .accent = 0
};

enum LfoTarget {
  LfoTargetShape,
  LfoTargetShiftShape,
  LfoTargetParam1,
  LfoTargetParam2,
  LfoTargetPitch,
  LfoTargetAmplitude,
  LfoTargetLfo2Frequency,
  LfoTargetLfo2Depth
};

inline float get_lfo_value(enum LfoTarget target) {
  return (p_values[k_user_osc_param_id3] == target ? shape_lfo : 0.0f) +
    (p_values[k_user_osc_param_id6] == target ? lfo2 : 0.0f);
}

inline float get_shape() {
  return clip01f(shape + get_lfo_value(LfoTargetShape));
}
inline float get_shift_shape() {
  return clip01f(shiftshape + get_lfo_value(LfoTargetShiftShape));
}
inline float get_param_id1() {
  return clip01f((p_values[k_user_osc_param_id1] * 0.005f) + get_lfo_value(LfoTargetParam1));
}
inline float get_param_id2() {
  return clip01f((p_values[k_user_osc_param_id2] * 0.01f) + get_lfo_value(LfoTargetParam2));
}
inline float get_param_lfo2_frequency() {
  return clip01f((p_values[k_user_osc_param_id4] * 0.01f) + get_lfo_value(LfoTargetLfo2Frequency));
}
inline float get_param_lfo2_depth() {
  return clip01f((p_values[k_user_osc_param_id5] * 0.01f) + get_lfo_value(LfoTargetLfo2Depth));
}

#ifdef OSC_VA
#include "plaits/dsp/engine/virtual_analog_engine.h"
plaits::VirtualAnalogEngine engine;
float out_gain = 0.8f, aux_gain = 0.8f;
void update_parameters() {
  parameters.harmonics = get_param_id1();
  parameters.timbre = get_shift_shape();
  parameters.morph = get_shape();
  mix = get_param_id2();
}
#endif

#ifdef OSC_WSH
#include "plaits/dsp/engine/waveshaping_engine.h"
plaits::WaveshapingEngine engine;
float out_gain = 0.7f, aux_gain = 0.6f;
void update_parameters() {
  parameters.harmonics = get_shift_shape();
  parameters.timbre = get_shape();
  parameters.morph = get_param_id1();
  mix = get_param_id2();
}
#endif

#ifdef OSC_FM
#include "plaits/dsp/engine/fm_engine.h"
plaits::FMEngine engine;
float out_gain = 0.6f, aux_gain = 0.6f;
void update_parameters() {
  parameters.harmonics = get_shift_shape();
  parameters.timbre = get_shape();
  parameters.morph = get_param_id1();
  mix = get_param_id2();
}
#endif

#ifdef OSC_GRN
#include "plaits/dsp/engine/grain_engine.h"
plaits::GrainEngine engine;
float out_gain = 0.7f, aux_gain = 0.6f;
void update_parameters() {
  parameters.harmonics = get_shape();
  parameters.timbre = get_shift_shape();
  parameters.morph = get_param_id1();
  mix = get_param_id2();
}
#endif

#ifdef OSC_ADD
#include "plaits/dsp/engine/additive_engine.h"
plaits::AdditiveEngine engine;
float out_gain = 0.8f, aux_gain = 0.8f;
void update_parameters() {
  parameters.harmonics = get_param_id1();
  parameters.timbre = get_shape();
  parameters.morph = get_shift_shape();
  mix = get_param_id2();
}
#endif

#if defined(OSC_WTA) || defined(OSC_WTB) || defined(OSC_WTC) || defined(OSC_WTD) || defined(OSC_WTE) || defined(OSC_WTF)
#include "plaits/dsp/engine/wavetable_engine.h"
plaits::WavetableEngine engine;
float out_gain = 0.6f, aux_gain = 0.6f;
void update_parameters() {
  parameters.harmonics = p_values[k_user_osc_param_id1] == 0 ? 0.f : 1.f;
  parameters.timbre = get_shape();
  parameters.morph = get_shift_shape();
  mix = get_param_id2();
}
#endif

#if defined(OSC_STRING)
#define USE_LIMITER
#define STRING_PRE_GAIN 1.8f
#include "plaits/dsp/engine/string_engine.h"
plaits::StringEngine engine;
void update_parameters() {
  parameters.harmonics = get_param_id1();
  parameters.timbre = get_shift_shape();
  parameters.morph = get_shape();
  mix = get_param_id2();
}
#endif

#if defined(OSC_MODAL)
#define USE_LIMITER
#include "plaits/dsp/engine/modal_engine.h"
plaits::ModalEngine engine;
void update_parameters() {
  parameters.harmonics = get_param_id1();
  parameters.timbre = get_shift_shape();
  parameters.morph = get_shape();
  mix = get_param_id2();
}
#endif

#if defined(USE_LIMITER)
#include "stmlib/dsp/limiter.h"
stmlib::Limiter limiter_;
#endif

void OSC_INIT(uint32_t platform, uint32_t api)
{
  (void)platform;
  (void)api;
  amp = 0.0f;
#if defined(OSC_STRING)
  stmlib::Random::Seed(0x82eef2a3);
  static uint8_t engine_buffer[4096*sizeof(float)] = {0};
#else
 #if defined(OSC_MODAL)
  stmlib::Random::Seed(0x82eef2a3);
  static uint8_t engine_buffer[plaits::kMaxBlockSize*sizeof(float)] = {0};
 #else
  static uint8_t engine_buffer[plaits::kMaxBlockSize*sizeof(float)] = {0};
 #endif
#endif

#if defined(USE_LIMITER)
  limiter_.Init();
#endif

  stmlib::BufferAllocator allocator;
  allocator.Init(engine_buffer, sizeof(engine_buffer));
  engine.Init(&allocator);
  lfo2_phase = 0.0f;
  lfo1_depth_value = 100;
  pitch_range_semitones_ = 1;

  p_values[0] = 100;
}

void OSC_NOTEON(const user_osc_param_t * const params)
{
  (void)params;
  gate = true;
  lfo2_phase = 0.0f;
}
void OSC_NOTEOFF(const user_osc_param_t * const params)
{
  (void)params;
  gate = false;
}

/* Neutral state, on the audio thread between blocks.  See OSC_RESET in
 * drumlogue/userosc.h for why it is not done where unit_reset() is called.
 *
 * Engine::Reset() is what clears the parts with memory -- the string model's
 * delay line, the wavetable readers' interpolation history.  Parameters are
 * deliberately untouched: the drumlogue keeps them across a reset and pushes
 * them again itself. */
void OSC_RESET(void)
{
  gate = false;
  previous_gate = false;
  amp = 0.0f;
  shape_lfo = 0.0f;
  lfo2 = 0.0f;
  lfo2_phase = 0.0f;
  parameters.trigger = plaits::TRIGGER_LOW;
  engine.Reset();
#if defined(USE_LIMITER)
  limiter_.Init();
#endif
}

void OSC_CYCLE(const user_osc_param_t *const params, int32_t *yn, const uint32_t frames)
{
  (void)frames;
  static float out[plaits::kMaxBlockSize] __attribute__((aligned(16)));
  static float aux[plaits::kMaxBlockSize] __attribute__((aligned(16)));
  static bool enveloped;

  /* Shape first, then depth: the shapes are transfer curves, so scaling the
   * input would bend the curve rather than turn the modulation down. */
  shape_lfo = apply_lfo1_shape(q31_to_f32(params->shape_lfo)) *
              (lfo1_depth_value * 0.01f);

  /* Multi-shape LFO2 generation.  Every shape, cosine included, is derived
   * from the one phase accumulator, so switching shape mid-cycle continues
   * rather than jumps.
   *
   * The cosine used to come from stmlib's CosineOscillator.  That class is a
   * two-pole resonator, and InitApproximate() sets its coefficient to
   * 2 - 32*freq^2 -- which at freq 0 is exactly 2, a double pole at z = 1.
   * There the recursion stops oscillating and starts integrating: it ramped
   * linearly from whatever state the last non-zero rate left it in, about 1.7
   * per thousand blocks measured.  Rate 0 is not an exotic setting, it is
   * where the knob starts, so turning Depth up and leaving Rate alone was
   * enough to reach it.  Every destination here runs through clip01f(), so
   * the symptom was a modulated knob sliding to its rail and staying there
   * rather than a fault -- in Rings, where the ramp reached an unclipped
   * destination (Note), the same code segfaulted on 9 runs in 30.
   * cosf() of a bounded phase cannot drift at any rate, including zero. */
  { float freq = get_param_lfo2_frequency() / 600.f;
    float depth = get_param_lfo2_depth();
    if (freq <= 0.0f) {
      /* Rate 0 means no modulation, not modulation parked somewhere.  A
       * stopped phase accumulator still has a value -- cos(0) is 1 -- and
       * reporting that would make Depth a DC offset whenever Rate is at its
       * end stop, which is where it starts.  The phase is left where it is
       * rather than rewound: Rate can itself be modulated (LfoTargetLfo2-
       * Frequency), and rewinding would re-trigger the shape every time the
       * effective rate crossed zero. */
      lfo2 = 0.0f;
    } else {
      lfo2_phase += freq;
      if (lfo2_phase >= 1.0f) lfo2_phase -= (float)(int)lfo2_phase;
      const float cos_val = cosf(2.0f * 3.1415926535f * lfo2_phase); /* [-1, 1] */
      float raw;
      switch (lfo2_shape_value) {
        default:
        case 0: /* Cosine */
          raw = cos_val;
          break;
        case 1: /* Triangle */
          raw = (lfo2_phase < 0.5f) ? (4.0f * lfo2_phase - 1.0f)
                                    : (3.0f - 4.0f * lfo2_phase);
          break;
        case 2: /* Ramp Up */
          raw = 2.0f * lfo2_phase - 1.0f;
          break;
        case 3: /* Ramp Down */
          raw = 1.0f - 2.0f * lfo2_phase;
          break;
        case 4: /* Fat Sine (deformed cosine with soft-clip) */
          raw = cos_val * (1.5f - 0.5f * cos_val * cos_val);
          raw = (raw > 1.0f) ? 1.0f : ((raw < -1.0f) ? -1.0f : raw);
          break;
      }
      lfo2 = raw * depth;
    }
  }

  parameters.note = ((float)(params->pitch >> 8)) + ((params->pitch & 0xFF) * k_note_mod_fscale);
  parameters.note += get_lfo_value(LfoTargetPitch) *
                     (float)pitch_range_semitones_;

  /* Gate mode affects trigger and envelope behavior */
  { bool effective_gate = gate;
    if (gate_mode_value == GateModeContinuous)
      effective_gate = true; /* always on in continuous mode */

    if (effective_gate && !previous_gate) {
      parameters.trigger = plaits::TRIGGER_RISING_EDGE;
    } else if (gate_mode_value == GateModeContinuous && !previous_gate) {
      /* First cycle in continuous mode: trigger */
      parameters.trigger = plaits::TRIGGER_RISING_EDGE;
    } else {
      parameters.trigger = plaits::TRIGGER_LOW;
    }
    previous_gate = effective_gate;
  }

  update_parameters();

  enveloped = false;
  engine.Render(parameters, out, aux, plaits::kMaxBlockSize, &enveloped);

#if defined(STRING_PRE_GAIN)
  for (size_t i = 0; i < plaits::kMaxBlockSize; ++i) {
    out[i] *= STRING_PRE_GAIN;
    aux[i] *= STRING_PRE_GAIN;
  }
#endif

#if !defined(OSC_STRING) && !defined(OSC_MODAL)
  if (!enveloped) {
    float target, alpha;
    switch (gate_mode_value) {
      default:
      case GateModeTrigger: /* Standard: attack on gate, decay on release */
        target = gate ? 1.0f : 0.0f;
        alpha = gate ? 0.05f : 0.002f;
        break;
      case GateModeSustain: /* Hold at full while gate on, fast release */
        target = gate ? 1.0f : 0.0f;
        alpha = gate ? 0.1f : 0.01f; /* faster attack, faster release */
        break;
      case GateModeContinuous: /* Always full amplitude */
        target = 1.0f;
        alpha = 0.05f;
        break;
    }
    for (size_t i = 0; i < plaits::kMaxBlockSize; ++i) {
      amp += (target - amp) * alpha;
      out[i] *= amp;
      aux[i] *= amp;
    }
  }
#endif

#if defined(USE_LIMITER)
  limiter_.Process(1.0 - mix, out, plaits::kMaxBlockSize);
#ifdef __ARM_NEON
  {
    const float32x4_t vscale = vdupq_n_f32(2147483648.0f);
    const float32x4_t vmin = vdupq_n_f32(-1.0f);
    const float32x4_t vmax = vdupq_n_f32(1.0f);
    size_t i = 0;
    for (; i + 4 <= plaits::kMaxBlockSize; i += 4) {
      float32x4_t v = vld1q_f32(out + i);
      v = vmaxq_f32(vminq_f32(v, vmax), vmin);
      int32x4_t q = vcvtq_s32_f32(vmulq_f32(v, vscale));
      vst1q_s32(yn + i, q);
    }
    for (; i < plaits::kMaxBlockSize; ++i)
      yn[i] = f32_to_q31(out[i]);
  }
#else
  for(size_t i=0;i<plaits::kMaxBlockSize;i++) {
    yn[i] = f32_to_q31(out[i]);
  }
#endif
#else
#ifdef __ARM_NEON
  {
    const float32x4_t vscale = vdupq_n_f32(2147483648.0f);
    const float32x4_t vmin = vdupq_n_f32(-1.0f);
    const float32x4_t vmax = vdupq_n_f32(1.0f);
    const float32x4_t vout_gain = vdupq_n_f32(out_gain);
    const float32x4_t vaux_gain = vdupq_n_f32(aux_gain);
    const float32x4_t vmix = vdupq_n_f32(mix);
    size_t i = 0;
    for (; i + 4 <= plaits::kMaxBlockSize; i += 4) {
      float32x4_t o = vmulq_f32(vld1q_f32(out + i), vout_gain);
      float32x4_t a = vmulq_f32(vld1q_f32(aux + i), vaux_gain);
      /* Crossfade: o + (a - o) * mix */
      float32x4_t v = vmlaq_f32(o, vsubq_f32(a, o), vmix);
      v = vmaxq_f32(vminq_f32(v, vmax), vmin);
      int32x4_t q = vcvtq_s32_f32(vmulq_f32(v, vscale));
      vst1q_s32(yn + i, q);
    }
    for (; i < plaits::kMaxBlockSize; ++i) {
      float o2 = out[i] * out_gain, a2 = aux[i] * aux_gain;
      yn[i] = f32_to_q31(stmlib::Crossfade(o2, a2, mix));
    }
  }
#else
  for(size_t i=0;i<plaits::kMaxBlockSize;i++) {
    float o = out[i] * out_gain, a = aux[i] * aux_gain;
    yn[i] = f32_to_q31(stmlib::Crossfade(o, a, mix));
  }
#endif
#endif
}

inline float percentage_to_f32(int16_t value) {
  return clip01f(value * 0.01f);
}

void OSC_PARAM(uint16_t index, uint16_t value)
{
  switch (index)
  {
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

  case 11: /* LFO1 Shape (0-4) */
    lfo1_shape_value = value;
    break;
  case 12: /* LFO2 Shape (0-4) */
    lfo2_shape_value = value;
    break;
  case 13: /* Gate Mode (0-2) */
    gate_mode_value = value;
    break;
  case 14: /* LFO1 Depth (0-100) */
    lfo1_depth_value = value > 100 ? 100 : value;
    break;
  case 15: /* Pitch Range, in semitones at full LFO swing */
    pitch_range_semitones_ =
        value > kPitchRangeSemitonesMax ? kPitchRangeSemitonesMax : value;
    break;

  default:
    break;
  }
}
