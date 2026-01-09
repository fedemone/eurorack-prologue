/*
 * File: drumlogue_osc_adapter.cc
 * 
 * OSC API Adapter Implementation for Drumlogue
 * 
 * This adapter provides helper functions for OSC modules that need
 * additional features not directly available in the OSC API, such as
 * advanced LFO routing, parameter smoothing, and format conversions.
 * 
 * Most OSC calls are now made directly from the unit wrapper, but this
 * file provides optional utilities for more complex scenarios.
 * 
 * Copyright (c) 2026
 */

#include "drumlogue_osc_adapter.h"
#include "userosc.h"
#include <cstring>
#include <cmath>

// ---- External OSC API Functions ----
extern "C" {
  void OSC_INIT(uint32_t platform, uint32_t api);
  void OSC_CYCLE(const user_osc_param_t * const params, int32_t *yn, const uint32_t frames);
  void OSC_NOTEON(const user_osc_param_t * const params);
  void OSC_NOTEOFF(const user_osc_param_t * const params);
  void OSC_PARAM(uint16_t index, uint16_t value);
}

// ---- Static State ----
static struct {
  user_osc_param_t params;
  float pitch_mod;
  float shape_lfo;
  uint32_t tempo_bpm_x10;  // BPM * 10
  bool initialized;
} s_adapter = {
  .params = {},
  .pitch_mod = 0.0f,
  .shape_lfo = 0.0f,
  .tempo_bpm_x10 = 1200,  // Default 120.0 BPM
  .initialized = false
};

// ---- Helper Functions ----

// Convert float (-1.0 to 1.0) to Q31 fixed-point
static inline int32_t float_to_q31(float f) {
  f = (f < -1.0f) ? -1.0f : (f > 1.0f) ? 1.0f : f;
  return static_cast<int32_t>(f * 2147483647.0f);
}

// Convert Q31 fixed-point to float
static inline float q31_to_float(int32_t q31) {
  return static_cast<float>(q31) * (1.0f / 2147483648.0f);
}

// Convert MIDI note + pitch mod to OSC pitch format
static inline uint16_t note_to_osc_pitch(uint8_t note, float pitch_mod_semitones) {
  float total_note = static_cast<float>(note) + pitch_mod_semitones;
  uint8_t note_int = static_cast<uint8_t>(total_note);
  uint8_t note_frac = static_cast<uint8_t>((total_note - note_int) * 255.0f);
  return (static_cast<uint16_t>(note_int) << 8) | note_frac;
}

// ---- Adapter API Implementation ----

void osc_adapter_init(uint32_t platform, uint32_t api_version) {
  // Initialize state
  memset(&s_adapter.params, 0, sizeof(user_osc_param_t));
  
  s_adapter.params.pitch = 60 << 8;  // Middle C
  s_adapter.params.shape_lfo = 0;
  s_adapter.params.value = 0;
  
  s_adapter.pitch_mod = 0.0f;
  s_adapter.shape_lfo = 0.0f;
  s_adapter.tempo_bpm_x10 = 1200;
  
  // Call OSC init
  OSC_INIT(platform, api_version);
  
  s_adapter.initialized = true;
}

void osc_adapter_note_on(uint8_t note, uint8_t velocity) {
  if (!s_adapter.initialized) return;
  
  // Update pitch with current modulation
  s_adapter.params.pitch = note_to_osc_pitch(note, s_adapter.pitch_mod);
  
  // Store velocity in value field if needed
  // (Some OSC modules may use this for velocity sensitivity)
  
  // Trigger note on
  OSC_NOTEON(&s_adapter.params);
}

void osc_adapter_note_off(uint8_t note) {
  if (!s_adapter.initialized) return;
  
  (void)note;  // Note number not used in noteoff
  OSC_NOTEOFF(&s_adapter.params);
}

void osc_adapter_pitch_bend(int16_t bend) {
  if (!s_adapter.initialized) return;
  
  // Convert bend value (±8192) to semitones (±2 typical)
  s_adapter.pitch_mod = (static_cast<float>(bend) / 8192.0f) * 2.0f;
  
  // Recalculate pitch - extract current note from params
  uint8_t current_note = (s_adapter.params.pitch >> 8) & 0xFF;
  s_adapter.params.pitch = note_to_osc_pitch(current_note, s_adapter.pitch_mod);
}

void osc_adapter_set_param(user_osc_param_id_t id, uint16_t value) {
  if (!s_adapter.initialized) return;
  
  // Update params struct for shape/shiftshape
  if (id == k_user_osc_param_shape || id == k_user_osc_param_shiftshape) {
    s_adapter.params.value = value;
  }
  
  // Forward to OSC module
  OSC_PARAM(id, value);
}

void osc_adapter_set_shape_lfo(float lfo_value) {
  if (!s_adapter.initialized) return;
  
  // Store float value
  s_adapter.shape_lfo = lfo_value;
  
  // Convert to Q31 for OSC API
  s_adapter.params.shape_lfo = float_to_q31(lfo_value);
}

float osc_adapter_get_shape_lfo() {
  return s_adapter.shape_lfo;
}

void osc_adapter_set_tempo(uint32_t tempo_bpm_x10) {
  if (!s_adapter.initialized) return;
  
  s_adapter.tempo_bpm_x10 = tempo_bpm_x10;
  
  // OSC modules don't have a direct tempo API
  // Could be used to calculate LFO sync rates
}

uint32_t osc_adapter_get_tempo() {
  return s_adapter.tempo_bpm_x10;
}

void osc_adapter_render(float *output, uint32_t frames) {
  if (!s_adapter.initialized || !output) {
    if (output) {
      memset(output, 0, frames * sizeof(float));
    }
    return;
  }
  
  // Allocate Q31 buffer
  int32_t q31_buffer[128];
  
  if (frames > 128) {
    memset(output, 0, frames * sizeof(float));
    return;
  }
  
  // Call OSC render
  OSC_CYCLE(&s_adapter.params, q31_buffer, frames);
  
  // Convert Q31 to float
  for (uint32_t i = 0; i < frames; ++i) {
    output[i] = q31_to_float(q31_buffer[i]);
  }
}

void osc_adapter_reset() {
  if (!s_adapter.initialized) return;
  
  // Reset modulation
  s_adapter.pitch_mod = 0.0f;
  s_adapter.shape_lfo = 0.0f;
  s_adapter.params.shape_lfo = 0;
  
  // Trigger note off
  OSC_NOTEOFF(&s_adapter.params);
}

bool osc_adapter_is_initialized() {
  return s_adapter.initialized;
}

// ---- Parameter Utilities ----

uint16_t osc_adapter_scale_param(float normalized) {
  // Convert 0.0-1.0 to 10-bit (0-1023)
  if (normalized < 0.0f) normalized = 0.0f;
  if (normalized > 1.0f) normalized = 1.0f;
  return static_cast<uint16_t>(normalized * 1023.0f);
}

float osc_adapter_normalize_param(uint16_t value) {
  // Convert 10-bit (0-1023) to 0.0-1.0
  if (value > 1023) value = 1023;
  return static_cast<float>(value) / 1023.0f;
}

uint16_t osc_adapter_midi_to_param(uint8_t midi_value) {
  // Convert 7-bit MIDI (0-127) to 10-bit (0-1023)
  return static_cast<uint16_t>(midi_value) << 3;
}

// ---- Tempo Sync Utilities ----

float osc_adapter_get_tempo_hz() {
  // Convert BPM to Hz
  return (static_cast<float>(s_adapter.tempo_bpm_x10) / 10.0f) / 60.0f;
}

float osc_adapter_get_sync_rate(float multiplier) {
  // Get synchronized rate in Hz for given multiplier
  // e.g., multiplier = 1.0 for quarter notes, 0.5 for half notes
  return osc_adapter_get_tempo_hz() * multiplier;
}

uint32_t osc_adapter_get_sync_samples(float multiplier, uint32_t sample_rate) {
  // Get number of samples for synced period
  float period_sec = 1.0f / osc_adapter_get_sync_rate(multiplier);
  return static_cast<uint32_t>(period_sec * static_cast<float>(sample_rate));
}

// ---- Advanced Parameter Smoothing ----

struct ParamSmoother {
  float current;
  float target;
  float slew_rate;  // Per-sample change rate
  
  void init(float initial_value, float slew_ms, uint32_t sample_rate) {
    current = initial_value;
    target = initial_value;
    // Calculate per-sample rate for given slew time
    slew_rate = 1.0f / (slew_ms * 0.001f * static_cast<float>(sample_rate));
  }
  
  void set_target(float new_target) {
    target = new_target;
  }
  
  float process() {
    if (current < target) {
      current += slew_rate;
      if (current > target) current = target;
    } else if (current > target) {
      current -= slew_rate;
      if (current < target) current = target;
    }
    return current;
  }
};

// Example: Global smoothers for common parameters (optional)
static ParamSmoother s_shape_smoother;
static ParamSmoother s_timbre_smoother;

void osc_adapter_init_smoothing(uint32_t sample_rate) {
  s_shape_smoother.init(0.5f, 50.0f, sample_rate);   // 50ms slew
  s_timbre_smoother.init(0.5f, 50.0f, sample_rate);  // 50ms slew
}

void osc_adapter_set_shape_smoothed(float value) {
  s_shape_smoother.set_target(value);
}

float osc_adapter_get_shape_smoothed() {
  return s_shape_smoother.process();
}

// ---- LFO Routing Helpers ----

struct LFORouter {
  float lfo_value;
  float depth[8];  // Depth for 8 possible destinations
  
  void set_lfo(float value) {
    lfo_value = value;
  }
  
  void set_depth(uint8_t dest, float depth_value) {
    if (dest < 8) {
      depth[dest] = depth_value;
    }
  }
  
  float get_modulation(uint8_t dest) {
    if (dest >= 8) return 0.0f;
    return lfo_value * depth[dest];
  }
};

static LFORouter s_lfo_router;

void osc_adapter_lfo_set_value(float lfo) {
  s_lfo_router.set_lfo(lfo);
}

void osc_adapter_lfo_set_depth(uint8_t destination, float depth) {
  s_lfo_router.set_depth(destination, depth);
}

float osc_adapter_lfo_get_mod(uint8_t destination) {
  return s_lfo_router.get_modulation(destination);
}

// ---- Utility: Format Conversions ----

void osc_adapter_convert_mono_to_stereo(const float *mono, float *stereo, uint32_t frames) {
  for (uint32_t i = 0; i < frames; ++i) {
    stereo[i * 2]     = mono[i];  // Left
    stereo[i * 2 + 1] = mono[i];  // Right
  }
}

void osc_adapter_convert_q31_to_float(const int32_t *q31, float *output, uint32_t frames) {
  for (uint32_t i = 0; i < frames; ++i) {
    output[i] = q31_to_float(q31[i]);
  }
}

void osc_adapter_convert_float_to_q31(const float *input, int32_t *q31, uint32_t frames) {
  for (uint32_t i = 0; i < frames; ++i) {
    q31[i] = float_to_q31(input[i]);
  }
}

// ---- Compatibility Wrappers for Unit Wrapper ----

void osc_adapter_param(uint16_t id, const void *params) {
  if (!s_adapter.initialized || !params) return;
  
  const user_osc_param_t *p = static_cast<const user_osc_param_t*>(params);
  OSC_PARAM(id, p->value);
}

const char* osc_adapter_get_param_str(uint16_t id, int32_t value) {
  // OSC modules don't typically provide string representations
  // Return nullptr - the unit wrapper can provide defaults
  (void)id;
  (void)value;
  return nullptr;
}

void osc_adapter_tempo_tick(uint32_t counter) {
  if (!s_adapter.initialized) return;
  
  // OSC modules don't have a direct tempo tick API
  // This is provided for compatibility but does nothing
  (void)counter;
}

// ---- Debug Utilities ----

#ifdef OSC_ADAPTER_DEBUG

#include <cstdio>

void osc_adapter_debug_print_state() {
  if (!s_adapter.initialized) {
    printf("OSC Adapter: Not initialized\n");
    return;
  }
  
  uint8_t note = (s_adapter.params.pitch >> 8) & 0xFF;
  uint8_t fine = s_adapter.params.pitch & 0xFF;
  
  printf("=== OSC Adapter State ===\n");
  printf("Initialized: yes\n");
  printf("Pitch: %u + %u/255 (0x%04X)\n", note, fine, s_adapter.params.pitch);
  printf("Pitch Mod: %.3f semitones\n", s_adapter.pitch_mod);
  printf("Shape LFO: %.3f (Q31: %d)\n", s_adapter.shape_lfo, s_adapter.params.shape_lfo);
  printf("Tempo: %u (%.1f BPM)\n", s_adapter.tempo_bpm_x10, s_adapter.tempo_bpm_x10 / 10.0f);
  printf("Param Value: %u\n", s_adapter.params.value);
  printf("========================\n");
}

void osc_adapter_test_render(uint32_t frames) {
  if (!s_adapter.initialized) {
    printf("Error: Adapter not initialized\n");
    return;
  }
  
  float buffer[128];
  if (frames > 128) frames = 128;
  
  osc_adapter_render(buffer, frames);
  
  printf("Rendered %u frames\n", frames);
  printf("First 3 samples: %.6f, %.6f, %.6f\n", buffer[0], buffer[1], buffer[2]);
  printf("Last 3 samples: %.6f, %.6f, %.6f\n", 
         buffer[frames-3], buffer[frames-2], buffer[frames-1]);
  
  // Calculate RMS
  float sum_squares = 0.0f;
  for (uint32_t i = 0; i < frames; ++i) {
    sum_squares += buffer[i] * buffer[i];
  }
  float rms = sqrtf(sum_squares / static_cast<float>(frames));
  printf("RMS level: %.6f\n", rms);
}

#endif // OSC_ADAPTER_DEBUG
