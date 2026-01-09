/*
 * File: userosc.h
 * 
 * User Oscillator API Header
 * Stub implementation for Drumlogue compatibility
 * 
 * This file provides type definitions and function declarations for the
 * User Oscillator API used by the logue SDK platforms.
 * 
 * Copyright (c) 2026
 */

#ifndef USEROSC_H_
#define USEROSC_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/* Constants                                                                  */
/*===========================================================================*/

/** Note modulation scale factor for pitch calculation */
#define k_note_mod_fscale (1.f / 255.f)

/** Q31 fixed-point scale factor (2^31) */
#define Q31_SCALE_FACTOR 2147483648.0f

/*===========================================================================*/
/* Type Definitions                                                          */
/*===========================================================================*/

/**
 * @brief User oscillator parameter IDs
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

/**
 * @brief User oscillator parameter structure
 */
typedef struct {
  uint16_t pitch;      // Pitch value (note << 8 | fine)
  int32_t shape_lfo;   // Shape LFO value (Q31 format)
  uint16_t value;      // Current parameter value
  uint8_t reserved[2]; // Reserved for alignment
} user_osc_param_t;

/*===========================================================================*/
/* Utility Functions                                                         */
/*===========================================================================*/

/**
 * @brief Convert parameter value (0-1023) to float (0.0-1.0)
 */
static inline float param_val_to_f32(uint16_t value) {
  return (value & 0x3FF) * (1.f / 1023.f);
}

/**
 * @brief Convert Q31 fixed-point to float
 */
static inline float q31_to_f32(int32_t q31) {
  return q31 * (1.f / Q31_SCALE_FACTOR);
}

/**
 * @brief Convert float to Q31 fixed-point
 * Clamps input to valid range [-1.0, 1.0]
 */
static inline int32_t f32_to_q31(float f) {
  f = (f < -1.f) ? -1.f : ((f > 1.f) ? 1.f : f);
  return (int32_t)(f * Q31_SCALE_FACTOR);
}

/**
 * @brief Clip float value to 0.0-1.0 range
 */
static inline float clip01f(float x) {
  return (x < 0.f) ? 0.f : ((x > 1.f) ? 1.f : x);
}

/**
 * @brief Clip float value to -1.0 to 1.0 range
 */
static inline float clipminusone_plusonef(float x) {
  return (x < -1.f) ? -1.f : ((x > 1.f) ? 1.f : x);
}

/*===========================================================================*/
/* OSC API Function Declarations                                            */
/*===========================================================================*/

/**
 * @brief Initialize user oscillator
 * @param platform Platform identifier
 * @param api API version
 */
void OSC_INIT(uint32_t platform, uint32_t api);

/**
 * @brief Process audio samples
 * @param params Oscillator parameters
 * @param yn Output buffer (Q31 format)
 * @param frames Number of frames to process
 */
void OSC_CYCLE(const user_osc_param_t * const params, int32_t *yn, const uint32_t frames);

/**
 * @brief Note on event
 * @param params Oscillator parameters with pitch/velocity
 */
void OSC_NOTEON(const user_osc_param_t * const params);

/**
 * @brief Note off event
 * @param params Oscillator parameters
 */
void OSC_NOTEOFF(const user_osc_param_t * const params);

/**
 * @brief Parameter change event
 * @param index Parameter ID
 * @param value Parameter value (0-1023 for most params)
 */
void OSC_PARAM(uint16_t index, uint16_t value);

#ifdef __cplusplus
}
#endif

#endif /* USEROSC_H_ */
