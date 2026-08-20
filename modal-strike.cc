#include "userosc.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/limiter.h"
#include "stmlib/utils/dsp.h"
#include "elements/dsp/part.h"
#include "elements/resources.h"

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

#ifdef ELEMENTS_LFO2
#include <math.h>
#endif

using namespace elements;

inline float get_shape();
inline float get_shift_shape();
inline float get_strength();
inline float get_damping();
inline float get_timbre();
inline float get_brightness();
inline float get_mallet();

Exciter strike_;
Resonator resonator_;

#if defined(USE_LIMITER)
stmlib::Limiter limiter_;
#endif

bool previous_gate_ = false;
float exciter_level_ = 0.f;
float strength_ = 0.f;
float envelope_value_ = 0.f;

float strike_buffer_[kMaxBlockSize] __attribute__((aligned(16)));

float bow_strength_buffer_[kMaxBlockSize] __attribute__((aligned(16)));

float raw[kMaxBlockSize] __attribute__((aligned(16)));
float center[kMaxBlockSize+2] = {.0f};

Patch patch_ = {
  .exciter_envelope_shape = 1.0f,
  .exciter_bow_level = 0.0f,
  .exciter_bow_timbre = 0.5f,
  .exciter_blow_level = 0.0f,
  .exciter_blow_meta = 0.5f,
  .exciter_blow_timbre = 0.5f,
  .exciter_strike_level = 0.8f,
  .exciter_strike_meta = 0.5f,
  .exciter_strike_timbre = 0.5f,
  .exciter_signature = 0.0f,
  .resonator_geometry = 0.2f,
  .resonator_brightness = 0.5f,
  .resonator_damping = 0.25f,
  .resonator_position = 0.3f,
  .resonator_modulation_frequency = 0.5f / kSampleRate,
  .resonator_modulation_offset = 0.1f,
  .reverb_diffusion = 0.625f,
  .reverb_lp = 0.7f,
  .space = 0.5f
};

PerformanceState performance_state_ = {
  .gate = false,
  .note = 69.0f,
  .modulation = .0f,
  .strength = 1.0f
};

float shape_lfo;

/* Parameter storage - declared here so LFO2 helper functions below can access it */
static uint16_t p_values[k_num_user_osc_param_id] = {0};
static float shape = 0, shiftshape = 0;

#ifdef ELEMENTS_LFO2
float lfo2 = 0;
static float lfo2_phase = 0.0f;

/* Custom param indices for LFO2 (beyond standard user_osc_param_id_t range).
 * Passed via OSC_PARAM by the drumlogue wrapper. */
uint16_t lfo2_rate_value = 0;
uint16_t lfo2_depth_value = 0;
uint16_t lfo2_target_value = 0;
static uint16_t lfo1_shape_value = 0;
static uint16_t lfo2_shape_value = 0;

/* LFO waveshape transfer function for LFO1 (shape LFO modulation). */
static inline float apply_lfo1_shape(float x) {
  switch (lfo1_shape_value) {
    default:
    case 0: return x;
    case 1: { float ax = x < 0.f ? -x : x;
              float s = ax * (2.0f - ax);
              return x < 0.f ? -s : s; }
    case 2: return x < 0.f ? -(x * x) : (x * x);
    case 3: { if (x > 0.f) { float s = 1.0f - x; return 1.0f - s * s; }
              if (x < 0.f) { float s = 1.0f + x; return -(1.0f - s * s); }
              return 0.f; }
    case 4: return clipminusone_plusonef(x * (1.5f - 0.5f * x * x));
  }
}

enum LfoTarget {
  LfoTargetPosition,
  LfoTargetGeometry,
  LfoTargetStrength,
  LfoTargetMallet,
  LfoTargetTimbre,
  LfoTargetDamping,
  LfoTargetBrightness,
  LfoTargetLfo2Frequency,
  LfoTargetLfo2Depth
};

inline float get_lfo2_frequency() {
  return clip01f((lfo2_rate_value * 0.01f) +
    (p_values[k_user_osc_param_id6] == LfoTargetLfo2Frequency ? shape_lfo : 0.0f) +
    (lfo2_target_value == LfoTargetLfo2Frequency ? lfo2 : 0.0f));
}

inline float get_lfo2_depth() {
  return clip01f((lfo2_depth_value * 0.01f) +
    (p_values[k_user_osc_param_id6] == LfoTargetLfo2Depth ? shape_lfo : 0.0f) +
    (lfo2_target_value == LfoTargetLfo2Depth ? lfo2 : 0.0f));
}

inline float get_lfo_value(enum LfoTarget target) {
  return (p_values[k_user_osc_param_id6] == target ? shape_lfo : 0.0f) +
    (lfo2_target_value == target ? lfo2 : 0.0f);
}
#endif

/* Geometry indexes a lookup table one element wider than its own top value.
 *
 * Resonator::ComputeFilters() calls Interpolate(lut_stiffness, geometry_,
 * 256.0f), and stmlib's Interpolate reads table[floor(x * N)] and the element
 * after it.  lut_stiffness holds 257 entries, indices 0..256, so geometry_ at
 * exactly 1.0 reads lut_stiffness[257] -- one past the end.  Geometry at 100 %
 * is one knob position, not a corner case, and either LFO can drive it there
 * from anywhere below.
 *
 * The overrun is benign arithmetically -- an integral index of N means a
 * fractional part of zero, so the stray element is multiplied by zero -- and
 * the table is followed by more .rodata, so it reads a neighbouring table
 * rather than faulting.  It is still a read past the end of an array on the
 * audio thread, in an address space shared with every other loaded unit, and
 * the cost of not doing it is one 4096th of a knob's travel.
 *
 * Fixed here rather than in the engine, so no fork is needed: Geometry is the
 * only parameter that reaches this table, and this is the only place it is
 * set.  Damping also indexes a 257-entry table (lut_4_decades) but through
 * damping_ * 0.8f, which tops out at 204.8 and is safe.  Rings carries the
 * same helper, under the same name, for the same reason.
 *
 * Found by pointing AddressSanitizer at a host build of the unit and running
 * the test_drmlgunit parameter sweep at it; see `make test-asan`. */
static const float kLutSafeMax = 1.0f - 1.0f / 4096.0f;

static inline float clip_lut01f(float x) {
  x = clip01f(x);
  return (x > kLutSafeMax) ? kLutSafeMax : x;
}

inline uint8_t GetGateFlags(bool gate_in) {
  uint8_t flags = 0;
  if (gate_in) {
    if (!previous_gate_) {
      flags |= EXCITER_FLAG_RISING_EDGE;
    }
    flags |= EXCITER_FLAG_GATE;
  } else if (previous_gate_) {
    flags = EXCITER_FLAG_FALLING_EDGE;
  }
  previous_gate_ = gate_in;
  return flags;
}

void Seed(uint32_t* seed, size_t size) {
  // Scramble all bits from the serial number.
  uint32_t signature = 0xf0cacc1a;
  for (size_t i = 0; i < size; ++i) {
    signature ^= seed[i];
    signature = signature * 1664525L + 1013904223L;
  }
  float x;

  x = static_cast<float>(signature & 7) / 8.0f;
  signature >>= 3;
  patch_.resonator_modulation_frequency = (0.4f + 0.8f * x) / elements::kSampleRate;

  x = static_cast<float>(signature & 7) / 8.0f;
  signature >>= 3;
  patch_.resonator_modulation_offset = 0.05f + 0.1f * x;

  x = static_cast<float>(signature & 7) / 8.0f;
  signature >>= 3;
  patch_.reverb_diffusion = 0.55f + 0.15f * x;

  x = static_cast<float>(signature & 7) / 8.0f;
  signature >>= 3;
  patch_.reverb_lp = 0.7f + 0.2f * x;

  x = static_cast<float>(signature & 7) / 8.0f;
  signature >>= 3;
  patch_.exciter_signature = x;
}

void OSC_INIT(uint32_t platform, uint32_t api)
{
  uint32_t random = 0x82eef2a3;
  stmlib::Random::Seed(random);
  Seed(&random, 1);
  strike_.Init();
  resonator_.Init();

#if defined(USE_LIMITER)
  limiter_.Init();
#endif

#ifdef ELEMENTS_LFO2
  lfo2_phase = 0.0f;
#endif
}

/*

FIR filter designed with
http://t-filter.appspot.com

sampling frequency: 48000 Hz

* 0 Hz - 11000 Hz
  gain = 1
  actual ripple = 22.664844906794034 dB

* 12000 Hz - 24000 Hz
  gain = 0
  actual attenuation = -28.499686493575386 dB
*/

static const float ipf[] = { 0.10639816444506338f, 0.26598957651736876f, 0.3989644179169387f };
#define lp_even(a,b,c) f32_to_q31(stmlib::SoftLimit((ipf[1] * a) + (ipf[2] * b) + (ipf[0] * c)))
#define lp_odd(a,b,c)  f32_to_q31(stmlib::SoftLimit((ipf[0] * a) + (ipf[2] * b) + (ipf[1] * c)))

void OSC_CYCLE(const user_osc_param_t *const params, int32_t *yn, const uint32_t frames)
{
  shape_lfo = q31_to_f32(params->shape_lfo);

#ifdef ELEMENTS_LFO2
  shape_lfo = apply_lfo1_shape(shape_lfo);

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
  { float freq = get_lfo2_frequency() / 600.f;
    float depth = get_lfo2_depth();
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
      float raw_lfo;
      switch (lfo2_shape_value) {
        default:
        case 0: raw_lfo = cos_val; break;
        case 1: raw_lfo = (lfo2_phase < 0.5f) ? (4.0f * lfo2_phase - 1.0f)
                                              : (3.0f - 4.0f * lfo2_phase); break;
        case 2: raw_lfo = 2.0f * lfo2_phase - 1.0f; break;
        case 3: raw_lfo = 1.0f - 2.0f * lfo2_phase; break;
        case 4: raw_lfo = cos_val * (1.5f - 0.5f * cos_val * cos_val);
                raw_lfo = (raw_lfo > 1.0f) ? 1.0f : ((raw_lfo < -1.0f) ? -1.0f : raw_lfo);
                break;
      }
      lfo2 = raw_lfo * depth;
    }
  }
#endif

  performance_state_.note = ((float)(params->pitch >> 8)) + ((params->pitch & 0xFF) * k_note_mod_fscale);
  int32_t pitch = static_cast<int32_t>((performance_state_.note + 41.0f) * 256.0f);
  if (pitch < 0) {
    pitch = 0;
  } else if (pitch >= 65535) {
    pitch = 65535;
  }
  float frequency = lut_midi_to_f_high[pitch >> 8] * lut_midi_to_f_low[pitch & 0xff];

  //patch_.exciter_envelope_shape = 1.0f;
  patch_.exciter_strike_level = get_strength();
  patch_.exciter_strike_meta = get_mallet();
  patch_.exciter_strike_timbre = get_timbre();
  //patch_.exciter_signature = 0.0f;
  patch_.resonator_damping = get_damping();
  patch_.resonator_brightness = get_brightness();
  patch_.resonator_geometry = get_shift_shape();
  patch_.resonator_position = get_shape();

  uint8_t flags = GetGateFlags(performance_state_.gate);

  float strike_meta = patch_.exciter_strike_meta;
#ifdef ELEMENTS_FULL
  /* The sample-player exciter models are not present in this eurorack
   * fork (removed from the ExciterModel enum), so the widest available
   * strike span is MALLET..PARTICLES, with the meta control kept linear
   * (no range compression as in the reduced build below). */
  strike_.set_meta(
      strike_meta,
      EXCITER_MODEL_MALLET,
      EXCITER_MODEL_PARTICLES);
#else
  strike_.set_meta(
      strike_meta <= 0.4f ? strike_meta * 0.625f : strike_meta * 1.25f - 0.25f,
      EXCITER_MODEL_MALLET,
      EXCITER_MODEL_PARTICLES);
#endif
  strike_.set_timbre(patch_.exciter_strike_timbre);
  strike_.set_signature(patch_.exciter_signature);
  strike_.Process(flags, strike_buffer_, kMaxBlockSize);

  // The Strike exciter is implemented in such a way that raising the level
  // beyond a certain point doesn't change the exciter amplitude, but instead,
  // increasingly mixes the raw exciter signal into the resonator output.
  float strike_level, strike_bleed;
  strike_level = patch_.exciter_strike_level * 1.25f;
  strike_bleed = strike_level > 1.0f ? (strike_level - 1.0f) * 2.0f : 0.0f;
  strike_level = strike_level < 1.0f ? strike_level : 1.0f;

#if defined(USE_LIMITER)
  strike_level *= 1.5f;
#endif

  // Sum all sources of excitation.
#ifdef __ARM_NEON
  {
    const float32x4_t vlevel = vdupq_n_f32(strike_level);
    size_t i = 0;
    for (; i + 4 <= kMaxBlockSize; i += 4) {
      vst1q_f32(raw + i, vmulq_f32(vld1q_f32(strike_buffer_ + i), vlevel));
    }
    for (; i < kMaxBlockSize; ++i)
      raw[i] = strike_buffer_[i] * strike_level;
  }
#else
  for (size_t i = 0; i < kMaxBlockSize; ++i) {
    raw[i] = strike_buffer_[i] * strike_level;
  }
#endif

    // Some exciters can cause palm mutes on release.
  float damping = patch_.resonator_damping;
  damping -= strike_.damping() * strike_level * 0.125f;
  damping -= (1.0f - bow_strength_buffer_[0]) * \
      patch_.exciter_bow_level * 0.0625f;

  if (damping <= 0.0f) {
    damping = 0.0f;
  }

  // Configure resonator.
  resonator_.set_frequency(frequency);
  resonator_.set_geometry(patch_.resonator_geometry);
  resonator_.set_brightness(patch_.resonator_brightness);
  resonator_.set_position(patch_.resonator_position);
  resonator_.set_damping(damping);
  resonator_.set_modulation_frequency(patch_.resonator_modulation_frequency);
  resonator_.set_modulation_offset(patch_.resonator_modulation_offset);

  // Process through resonator.
  resonator_.Process(bow_strength_buffer_, raw, center+2, NULL, kMaxBlockSize);

  for (size_t i=0; i<kMaxBlockSize; ++i) {
    center[i+2] = (center[i+2] + (strike_bleed * strike_buffer_[i]));
  }

#if defined(USE_LIMITER)
  limiter_.Process(2.f, center+2, kMaxBlockSize);
#endif

  for (size_t i=0; i<kMaxBlockSize; ++i) {
    yn[i*2] = lp_even(center[i], center[i+1], center[i+2]);
    yn[i*2+1] = lp_odd(center[i], center[i+1], center[i+2]);
  }
  center[0] = center[kMaxBlockSize];
  center[1] = center[kMaxBlockSize+1];
}

void OSC_NOTEON(const user_osc_param_t * const params)
{
  performance_state_.gate = true;
#ifdef ELEMENTS_LFO2
  lfo2_phase = 0.0f;
#endif
}
void OSC_NOTEOFF(const user_osc_param_t * const params)
{
  performance_state_.gate = false;
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

#ifdef ELEMENTS_LFO2
  case 8: /* LFO2 Rate (0-100) */
    lfo2_rate_value = value;
    break;
  case 9: /* LFO2 Depth (0-100) */
    lfo2_depth_value = value;
    break;
  case 10: /* LFO2 Target (enum) */
    lfo2_target_value = value;
    break;
  case 11: /* LFO1 Shape (0-4) */
    lfo1_shape_value = value;
    break;
  case 12: /* LFO2 Shape (0-4) */
    lfo2_shape_value = value;
    break;
#endif

  default:
    break;
  }
}

#ifdef ELEMENTS_LFO2
inline float get_shape() {
  return clip01f(shape + get_lfo_value(LfoTargetPosition));
}
inline float get_shift_shape() {
  return clip_lut01f(shiftshape + get_lfo_value(LfoTargetGeometry));
}
inline float get_strength() {
  return clip01f((p_values[k_user_osc_param_id1] * 0.01f) + get_lfo_value(LfoTargetStrength));
}
inline float get_mallet() {
  return clip01f((p_values[k_user_osc_param_id2] * 0.01f) + get_lfo_value(LfoTargetMallet));
}
inline float get_timbre() {
  return clip01f((p_values[k_user_osc_param_id3] * 0.01f) + get_lfo_value(LfoTargetTimbre));
}
inline float get_damping() {
  return clip01f((p_values[k_user_osc_param_id4] * 0.01f) + get_lfo_value(LfoTargetDamping));
}
inline float get_brightness() {
  return clip01f((p_values[k_user_osc_param_id5] * 0.01f) + get_lfo_value(LfoTargetBrightness));
}
#else
inline float get_shape() {
  return clip01f(shape + (p_values[k_user_osc_param_id6] == 0 ? shape_lfo : 0.0f));
}
inline float get_shift_shape() {
  return clip_lut01f(shiftshape + (p_values[k_user_osc_param_id6] == 1 ? shape_lfo : 0.0f));
}
inline float get_strength() {
  return clip01f((p_values[k_user_osc_param_id1] * 0.01f) + (p_values[k_user_osc_param_id6] == 2 ? shape_lfo : 0.0f));
}
inline float get_mallet() {
  return clip01f((p_values[k_user_osc_param_id2] * 0.01f) + (p_values[k_user_osc_param_id6] == 3 ? shape_lfo : 0.0f));
}
inline float get_timbre() {
  return clip01f((p_values[k_user_osc_param_id3] * 0.01f) + (p_values[k_user_osc_param_id6] == 4 ? shape_lfo : 0.0f));
}
inline float get_damping() {
  return clip01f((p_values[k_user_osc_param_id4] * 0.01f) + (p_values[k_user_osc_param_id6] == 5 ? shape_lfo : 0.0f));
}
inline float get_brightness() {
  return clip01f((p_values[k_user_osc_param_id5] * 0.01f) + (p_values[k_user_osc_param_id6] == 6 ? shape_lfo : 0.0f));
}
#endif
