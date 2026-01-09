/*
 * File: drumlogue_osc_adapter.h
 *
 * OSC API to Synth Module API Adapter Header
 * 
 * This adapter bridges the Drumlogue OSC API with the Synth Module API,
 * allowing OSC-based synthesizers to work with the synth module interface.
 *
 * Copyright (c) 2026
 *
 */

#ifndef DRUMLOGUE_OSC_ADAPTER_H_
#define DRUMLOGUE_OSC_ADAPTER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/* Type Definitions                                                          */
/*===========================================================================*/

/**
 * @brief OSC API state structure
 */
typedef struct {
    void *osc_instance;     // Pointer to OSC synthesizer instance
    float pitch;            // Current pitch value
    float shape;            // Shape parameter
    float shift_shape;      // Shifted shape parameter
    uint8_t note;           // MIDI note number
    uint8_t velocity;       // Note velocity
    uint8_t flags;          // State flags
} osc_adapter_state_t;

/**
 * @brief Adapter configuration structure
 */
typedef struct {
    uint32_t sample_rate;   // Sample rate in Hz
    uint16_t frames_per_buffer; // Number of frames per buffer
    uint8_t num_params;     // Number of parameters
} osc_adapter_config_t;

/*===========================================================================*/
/* Function Prototypes                                                       */
/*===========================================================================*/

/**
 * @brief Initialize the OSC adapter
 * 
 * @param config Pointer to adapter configuration
 * @return Pointer to initialized adapter state, NULL on failure
 */
osc_adapter_state_t* osc_adapter_init(const osc_adapter_config_t *config);

/**
 * @brief Cleanup and free adapter resources
 * 
 * @param state Pointer to adapter state
 */
void osc_adapter_cleanup(osc_adapter_state_t *state);

/**
 * @brief Trigger note on event
 * 
 * @param state Pointer to adapter state
 * @param note MIDI note number (0-127)
 * @param velocity Note velocity (0-127)
 */
void osc_adapter_note_on(osc_adapter_state_t *state, uint8_t note, uint8_t velocity);

/**
 * @brief Trigger note off event
 * 
 * @param state Pointer to adapter state
 */
void osc_adapter_note_off(osc_adapter_state_t *state);

/**
 * @brief Set pitch parameter
 * 
 * @param state Pointer to adapter state
 * @param pitch Pitch value (normalized 0.0-1.0)
 */
void osc_adapter_set_pitch(osc_adapter_state_t *state, float pitch);

/**
 * @brief Set shape parameter
 * 
 * @param state Pointer to adapter state
 * @param shape Shape value (normalized 0.0-1.0)
 */
void osc_adapter_set_shape(osc_adapter_state_t *state, float shape);

/**
 * @brief Set shift-shape parameter
 * 
 * @param state Pointer to adapter state
 * @param shift_shape Shift-shape value (normalized 0.0-1.0)
 */
void osc_adapter_set_shift_shape(osc_adapter_state_t *state, float shift_shape);

/**
 * @brief Set parameter by index
 * 
 * @param state Pointer to adapter state
 * @param index Parameter index
 * @param value Parameter value (normalized 0.0-1.0)
 */
void osc_adapter_set_param(osc_adapter_state_t *state, uint8_t index, float value);

/**
 * @brief Get parameter by index
 * 
 * @param state Pointer to adapter state
 * @param index Parameter index
 * @return Parameter value (normalized 0.0-1.0)
 */
float osc_adapter_get_param(const osc_adapter_state_t *state, uint8_t index);

/**
 * @brief Process audio buffer (mono)
 * 
 * @param state Pointer to adapter state
 * @param output Output buffer
 * @param frames Number of frames to process
 */
void osc_adapter_process(osc_adapter_state_t *state, float *output, uint32_t frames);

/**
 * @brief Process audio buffer (stereo)
 * 
 * @param state Pointer to adapter state
 * @param output_l Left channel output buffer
 * @param output_r Right channel output buffer
 * @param frames Number of frames to process
 */
void osc_adapter_process_stereo(osc_adapter_state_t *state, 
                                float *output_l, 
                                float *output_r, 
                                uint32_t frames);

/**
 * @brief Reset adapter state
 * 
 * @param state Pointer to adapter state
 */
void osc_adapter_reset(osc_adapter_state_t *state);

/**
 * @brief Convert OSC API wave function to synth module format
 * 
 * @param state Pointer to adapter state
 * @param output Output buffer
 * @param frames Number of frames
 */
void osc_adapter_wave_func(osc_adapter_state_t *state, float *output, uint32_t frames);

/*===========================================================================*/
/* Additional Adapter Functions (for unit wrapper)                          */
/*===========================================================================*/

#include "userosc.h"

/**
 * @brief Initialize OSC adapter with platform and API version
 * 
 * @param platform Platform identifier
 * @param api_version API version
 */
void osc_adapter_init(uint32_t platform, uint32_t api_version);

/**
 * @brief Trigger note on event
 * 
 * @param note MIDI note number
 * @param velocity Note velocity
 */
void osc_adapter_note_on(uint8_t note, uint8_t velocity);

/**
 * @brief Trigger note off event
 * 
 * @param note MIDI note number
 */
void osc_adapter_note_off(uint8_t note);

/**
 * @brief Set pitch bend
 * 
 * @param bend Pitch bend value (signed)
 */
void osc_adapter_pitch_bend(int16_t bend);

/**
 * @brief Set parameter value
 * 
 * @param id Parameter ID
 * @param param Parameter structure
 */
void osc_adapter_param(user_osc_param_id_t id, const user_osc_param_t *param);

/**
 * @brief Get parameter string representation
 * 
 * @param id Parameter ID
 * @param value Parameter value
 * @return String representation or nullptr
 */
const char* osc_adapter_get_param_str(user_osc_param_id_t id, int32_t value);

/**
 * @brief Set tempo
 * 
 * @param tempo_bpm_x10 Tempo in BPM * 10
 */
void osc_adapter_set_tempo(uint32_t tempo_bpm_x10);

/**
 * @brief Tempo clock tick
 * 
 * @param counter Tick counter
 */
void osc_adapter_tempo_tick(uint32_t counter);

/**
 * @brief Render audio
 * 
 * @param output Output buffer
 * @param frames Number of frames to render
 */
void osc_adapter_render(float *output, uint32_t frames);

/**
 * @brief Reset adapter
 */
void osc_adapter_reset();

/**
 * @brief Check if adapter is initialized
 * 
 * @return true if initialized, false otherwise
 */
bool osc_adapter_is_initialized();

#ifdef __cplusplus
}
#endif

#endif /* DRUMLOGUE_OSC_ADAPTER_H_ */
