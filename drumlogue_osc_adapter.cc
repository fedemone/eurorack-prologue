/*
 * File: drumlogue_osc_adapter.cc
 *
 * OSC API Adapter Implementation for Drumlogue
 *
 * Manages the user_osc_param_t struct that the OSC API expects,
 * handles Q31 <-> float conversion, and translates between the
 * drumlogue unit wrapper calls and the OSC_* functions.
 *
 * The oscillator source code (macro-oscillator2.cc, modal-strike.cc)
 * is compiled unchanged - it sees the same OSC_INIT / OSC_CYCLE /
 * OSC_NOTEON / OSC_NOTEOFF / OSC_PARAM interface as on prologue.
 */

#include "drumlogue_osc_adapter.h"
#include "userosc.h"
#include <cstring>
#include <cmath>

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

/* ---- Static State ---- */

static struct {
  user_osc_param_t params;
  float            pitch_mod;      /* pitch bend in semitones */
  float            shape_lfo;      /* shape LFO value (float) */
  uint32_t         tempo;          /* tempo from drumlogue runtime */
  bool             initialized;
} s_adapter;

/* ---- Buffered Rendering State ---- */

#ifndef OSC_NATIVE_BLOCK_SIZE
#define OSC_NATIVE_BLOCK_SIZE 24
#endif

static float    s_render_buf[OSC_NATIVE_BLOCK_SIZE];
static uint32_t s_render_rd = 0;   /* read position */
static uint32_t s_render_avail = 0; /* samples available */

/* ---- Q31 / Float Helpers ---- */
static const float kQ31Reciprocal = 1.0f / (float)(1U << 31); // 2147483648.0f = 2^31

static inline int32_t float_to_q31(float f) {
  /* Q31 range is [-1.0, 1.0 - 2^-31]. Clamp after scaling to avoid overflow. */
  f = (f < -1.0f) ? -1.0f : (f > 1.0f) ? 1.0f : f;
  float scaled = f * 2147483648.0f;
  if (scaled >= 2147483647.0f) return 0x7FFFFFFF;
  return (int32_t)scaled;
}

static inline float q31_to_float(int32_t q31) {
  return (float)q31 * kQ31Reciprocal;
}

/* ---- Pitch Helpers ---- */

/**
 * Encode note + fractional pitch modulation into the OSC API pitch format.
 * Format: (note << 8) | frac, where frac is 0-255 representing 0..~1 semitone.
 */
static inline uint16_t note_to_osc_pitch(uint8_t note, float pitch_mod_semitones) {
  float total = (float)note + pitch_mod_semitones;
  if (total < 0.0f) total = 0.0f;
  if (total > 151.0f) total = 151.0f;
  uint8_t n = (uint8_t)total;
  uint8_t frac = (uint8_t)((total - n) * 255.0f);
  return ((uint16_t)n << 8) | frac;
}

/* ---- Adapter API ---- */

void osc_adapter_init(uint32_t platform, uint32_t api_version) {
  memset(&s_adapter.params, 0, sizeof(user_osc_param_t));

  s_adapter.params.pitch     = 60 << 8;  /* middle C */
  s_adapter.params.shape_lfo = 0;
  s_adapter.pitch_mod        = 0.0f;
  s_adapter.shape_lfo        = 0.0f;
  s_adapter.tempo            = 0;

  /* Flush render buffer */
  s_render_rd    = 0;
  s_render_avail = 0;

  OSC_INIT(platform, api_version);

  s_adapter.initialized = true;
}

void osc_adapter_teardown(void) {
  s_adapter.initialized = false;
  s_render_rd    = 0;
  s_render_avail = 0;
}

void osc_adapter_reset(void) {
  if (!s_adapter.initialized) return;

  s_adapter.pitch_mod        = 0.0f;
  s_adapter.shape_lfo        = 0.0f;
  s_adapter.params.shape_lfo = 0;

  /* Flush render buffer */
  s_render_rd    = 0;
  s_render_avail = 0;

  OSC_NOTEOFF(&s_adapter.params);
}

bool osc_adapter_is_initialized(void) {
  return s_adapter.initialized;
}

/* ---- Note Events ---- */

void osc_adapter_note_on(uint8_t note, uint8_t velocity) {
  if (!s_adapter.initialized) return;
  (void)velocity;  /* OSC API doesn't have a velocity parameter */

  s_adapter.params.pitch = note_to_osc_pitch(note, s_adapter.pitch_mod);
  OSC_NOTEON(&s_adapter.params);
}

void osc_adapter_note_off(uint8_t note) {
  if (!s_adapter.initialized) return;
  (void)note;

  OSC_NOTEOFF(&s_adapter.params);
}

/* ---- Pitch ---- */

void osc_adapter_pitch_bend(int16_t bend) {
  if (!s_adapter.initialized) return;

  /* Convert +/-8192 to +/-2 semitones */
  s_adapter.pitch_mod = ((float)bend / 8192.0f) * 2.0f;

  /* Update pitch in params struct */
  uint8_t current_note = (uint8_t)(s_adapter.params.pitch >> 8);
  s_adapter.params.pitch = note_to_osc_pitch(current_note, s_adapter.pitch_mod);
}

/* ---- Parameters ---- */

void osc_adapter_set_param(user_osc_param_id_t osc_id, uint16_t value) {
  if (!s_adapter.initialized) return;

  OSC_PARAM((uint16_t)osc_id, value);
}

void osc_adapter_set_shape_lfo(float lfo_value) {
  if (!s_adapter.initialized) return;

  s_adapter.shape_lfo = lfo_value;
  s_adapter.params.shape_lfo = float_to_q31(lfo_value);
}

/* ---- Tempo ---- */

void osc_adapter_set_tempo(uint32_t tempo) {
  if (!s_adapter.initialized) return;

  s_adapter.tempo = tempo;
  /* OSC modules don't have a direct tempo API;
     stored for potential future LFO sync use. */
}

/* ---- Audio Rendering (Buffered) ---- */

/*
 * OSC_NATIVE_BLOCK_SIZE: the fixed number of Q31 samples produced by
 * one call to OSC_CYCLE, regardless of the `frames` parameter it receives.
 *
 *   Plaits oscillators (macro-oscillator2.cc):
 *     plaits::kMaxBlockSize = 24 mono samples
 *     -> set OSC_NATIVE_BLOCK_SIZE=24 in the .mk file
 *
 *   Elements oscillator (modal-strike.cc):
 *     2 * elements::kMaxBlockSize = 32 samples (2x FIR-upsampled mono)
 *     -> set OSC_NATIVE_BLOCK_SIZE=32 in the .mk file
 *
 * If not defined, defaults to 24 (plaits).
 * Note: OSC_NATIVE_BLOCK_SIZE, s_render_buf, s_render_rd, s_render_avail
 * are defined at the top of this file so osc_adapter_reset() can access them.
 */

/**
 * Convert Q31 int32_t buffer to float buffer.
 * NEON path processes 4 samples at a time using SIMD.
 * Both block sizes (24, 32) are multiples of 4.
 */
static void q31_buf_to_float(const int32_t *src, float *dst, uint32_t count) {
#ifdef __ARM_NEON
  // This constant could be defined at file scope for wider use.
  const float32x4_t scale = vdupq_n_f32(kQ31Reciprocal);
  uint32_t i = 0;
  for (; i + 4 <= count; i += 4) {
    int32x4_t q = vld1q_s32(src + i);
    float32x4_t f = vcvtq_f32_s32(q);
    f = vmulq_f32(f, scale);
    vst1q_f32(dst + i, f);
  }
  for (; i < count; ++i) {
    dst[i] = (float)src[i] * kQ31Reciprocal;
  }
#else
  for (uint32_t i = 0; i < count; ++i) {
    dst[i] = q31_to_float(src[i]);
  }
#endif
}

/**
 * Render one native-sized block from OSC_CYCLE into the static buffer.
 */
static void render_one_block(void) {
  int32_t q31_buf[OSC_NATIVE_BLOCK_SIZE];

  OSC_CYCLE(&s_adapter.params, q31_buf, OSC_NATIVE_BLOCK_SIZE);

  q31_buf_to_float(q31_buf, s_render_buf, OSC_NATIVE_BLOCK_SIZE);
  s_render_rd    = 0;
  s_render_avail = OSC_NATIVE_BLOCK_SIZE;
}

void osc_adapter_render(float *output, uint32_t frames) {
  if (!s_adapter.initialized || !output) {
    if (output) memset(output, 0, frames * sizeof(float));
    return;
  }

  uint32_t written = 0;
  while (written < frames) {
    /* Refill buffer if empty */
    if (s_render_avail == 0) {
      render_one_block();
    }

    /* Copy available samples to output */
    uint32_t need = frames - written;
    uint32_t n = (need < s_render_avail) ? need : s_render_avail;
    memcpy(output + written, s_render_buf + s_render_rd, n * sizeof(float));

    s_render_rd    += n;
    s_render_avail -= n;
    written        += n;
  }
}
