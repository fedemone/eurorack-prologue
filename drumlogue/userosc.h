/*
 * File: drumlogue/userosc.h
 *
 * User Oscillator API compatibility header for Drumlogue platform.
 *
 * On prologue/minilogue-xd/NTS-1, this is provided by the logue-SDK
 * (logue-sdk/platform/<platform>/inc/userosc.h). For drumlogue,
 * we provide our own version that defines the same types and functions
 * so the oscillator source code compiles unchanged.
 *
 * The key difference: on prologue, OSC_INIT etc. are macros that rename
 * to _hook_init etc. On drumlogue, they are plain function names because
 * the unit wrapper calls them directly via the adapter.
 */

#ifndef USEROSC_H_
#define USEROSC_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/* Constants                                                                 */
/*===========================================================================*/

#define k_samplerate        (48000)
#define k_samplerate_recipf (2.08333333333333e-005f)

/** Note modulation scale factor: 1/255 */
#define k_note_mod_fscale   (0.00392156862745098f)

/*===========================================================================*/
/* Type Definitions                                                          */
/*===========================================================================*/

/**
 * @brief User oscillator parameter structure.
 *
 * Layout matches the logue-SDK user_osc_param_t exactly.
 * On drumlogue, the adapter fills this struct before calling OSC_CYCLE.
 */
#pragma pack(push, 1)
typedef struct user_osc_param {
  int32_t  shape_lfo;      /**< Shape LFO value in Q31 format */
  uint16_t pitch;          /**< Pitch: (note << 8) | mod  */
  uint16_t cutoff;         /**< Filter cutoff (0x0000-0x1fff), unused on drumlogue */
  uint16_t resonance;      /**< Filter resonance (0x0000-0x1fff), unused on drumlogue */
  uint16_t reserved0[3];
} user_osc_param_t;
#pragma pack(pop)

/**
 * @brief User oscillator parameter IDs.
 */
typedef enum {
  k_user_osc_param_id1 = 0,
  k_user_osc_param_id2,
  k_user_osc_param_id3,
  k_user_osc_param_id4,
  k_user_osc_param_id5,
  k_user_osc_param_id6,
  k_user_osc_param_shape,
  k_user_osc_param_shiftshape,
  k_num_user_osc_param_id
} user_osc_param_id_t;

/*===========================================================================*/
/* Utility Functions / Macros                                                */
/*===========================================================================*/

/** Convert 10-bit parameter value (0-1023) to float (0.0-1.0) */
#define param_val_to_f32(val) ((uint16_t)(val) * 9.77517106549365e-004f)

/** Convert Q31 fixed-point to float */
static inline float q31_to_f32(int32_t q31) {
  return (float)q31 * (1.f / (float)(1U << 31));
}

/** Convert float to Q31 fixed-point, clamped to [-1.0, 1.0] */
static inline int32_t f32_to_q31(float f) {
  f = (f < -1.f) ? -1.f : ((f > 1.f) ? 1.f : f);
  return (int32_t)(f * (float)(1U << 31));
}

/** Clip float to [0.0, 1.0] */
static inline float clip01f(float x) {
  return (x < 0.f) ? 0.f : ((x > 1.f) ? 1.f : x);
}

/** Clip float to [-1.0, 1.0] */
static inline float clipminusone_plusonef(float x) {
  return (x < -1.f) ? -1.f : ((x > 1.f) ? 1.f : x);
}

/** Clip float to [-1.0, 1.0] (alias used by osc_api.h) */
static inline float clip1m1f(float x) {
  return clipminusone_plusonef(x);
}

/** Clip float to [0.0, max] */
static inline float clipmaxf(float x, float m) {
  return (x > m) ? m : x;
}

/** Clip float to [min, max] */
static inline float clipminmaxf(float min, float x, float max) {
  return (x < min) ? min : ((x > max) ? max : x);
}

/** Clip float to [0, 1] (alias) */
static inline float clip1f(float x) {
  return (x > 1.f) ? 1.f : x;
}

/** Linear interpolation */
static inline float linintf(float fr, float x0, float x1) {
  return x0 + fr * (x1 - x0);
}

/** Absolute value (float) */
static inline float si_fabsf(float x) {
  return (x < 0.f) ? -x : x;
}

/** Copy sign */
static inline float si_copysignf(float x, float y) {
  return (y < 0.f) ? -si_fabsf(x) : si_fabsf(x);
}

/** Clip unsigned 32-bit to max */
static inline uint32_t clipmaxu32(uint32_t x, uint32_t m) {
  return (x > m) ? m : x;
}

/*===========================================================================*/
/* OSC API Function Declarations                                             */
/*===========================================================================*/

/*
 * On prologue, these are macros: #define OSC_INIT __attribute__((used)) _hook_init
 * On drumlogue, they are plain function names called by the adapter.
 */

void OSC_INIT(uint32_t platform, uint32_t api);
void OSC_CYCLE(const user_osc_param_t * const params, int32_t *yn, const uint32_t frames);
void OSC_NOTEON(const user_osc_param_t * const params);
void OSC_NOTEOFF(const user_osc_param_t * const params);
void OSC_PARAM(uint16_t index, uint16_t value);

#ifdef __cplusplus
}
#endif

#endif /* USEROSC_H_ */
