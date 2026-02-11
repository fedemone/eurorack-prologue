/*
 * File: drumlogue_osc_adapter.h
 *
 * OSC API Adapter for Drumlogue
 *
 * Bridges between the drumlogue unit wrapper and the OSC API functions
 * (OSC_INIT, OSC_CYCLE, OSC_NOTEON, OSC_NOTEOFF, OSC_PARAM) implemented
 * in the oscillator source files.
 *
 * This adapter manages the user_osc_param_t state, Q31<->float conversion,
 * and pitch/parameter translation so the oscillator source code can run
 * unchanged on drumlogue.
 */

#ifndef DRUMLOGUE_OSC_ADAPTER_H_
#define DRUMLOGUE_OSC_ADAPTER_H_

#include <stdint.h>
#include <stdbool.h>
#include "userosc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Lifecycle ---- */

/** Initialize adapter and call OSC_INIT */
void osc_adapter_init(uint32_t platform, uint32_t api_version);

/** Tear down adapter: mark as uninitialized, flush buffers */
void osc_adapter_teardown(void);

/** Reset adapter state and trigger note off */
void osc_adapter_reset(void);

/** Check if adapter has been initialized */
bool osc_adapter_is_initialized(void);

/* ---- Note Events ---- */

/** Trigger note on: sets pitch and calls OSC_NOTEON */
void osc_adapter_note_on(uint8_t note, uint8_t velocity);

/** Trigger note off: calls OSC_NOTEOFF */
void osc_adapter_note_off(uint8_t note);

/* ---- Pitch ---- */

/** Apply pitch bend (signed, in 8192ths of 2 semitones) */
void osc_adapter_pitch_bend(int16_t bend);

/* ---- Parameters ---- */

/**
 * Set an OSC parameter.
 * Maps directly to OSC_PARAM(osc_id, value).
 * For shape/shiftshape: value is 10-bit (0-1023).
 * For id1-id6: value range depends on the specific parameter.
 */
void osc_adapter_set_param(user_osc_param_id_t osc_id, uint16_t value);

/** Set the shape LFO value (normalized -1.0 to 1.0) */
void osc_adapter_set_shape_lfo(float lfo_value);

/* ---- Tempo ---- */

/** Set tempo (BPM in fixed-point format from drumlogue runtime) */
void osc_adapter_set_tempo(uint32_t tempo);

/* ---- Audio Rendering ---- */

/**
 * Render audio into a mono float buffer.
 *
 * Calls OSC_CYCLE internally with the current param state,
 * converts Q31 output to float.
 *
 * @param output  Mono float output buffer (must hold at least `frames` floats)
 * @param frames  Number of mono frames to render
 */
void osc_adapter_render(float *output, uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif /* DRUMLOGUE_OSC_ADAPTER_H_ */
