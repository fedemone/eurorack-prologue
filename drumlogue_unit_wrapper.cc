/*
 * File: drumlogue_unit_wrapper.cc
 * 
 * Drumlogue Unit Wrapper Implementation
 * 
 * This file provides the complete synth module API wrapper for Drumlogue,
 * bridging the unit API calls to the OSC API adapter. It handles all
 * required unit lifecycle, rendering, note events, and parameter management.
 * 
 * Copyright (c) 2026
 */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <algorithm>

#include "userosc.h"
#include "drumlogue_osc_adapter.h"

// ---- Module State ----
static struct {
  bool initialized;
  std::atomic<uint32_t> flags;
  user_osc_param_t params[24];  // Storage for parameter values
  uint8_t note;
  uint8_t velocity;
  uint32_t sample_count;
} s_unit_state = {
  .initialized = false,
  .flags = 0,
  .params = {},
  .note = 60,
  .velocity = 100,
  .sample_count = 0
};

// ---- Forward Declarations ----
extern "C" {

// Required Unit API Functions
__attribute__((used))
void unit_init(uint32_t platform, uint32_t api_version);

__attribute__((used))
void unit_teardown();

__attribute__((used))
void unit_reset();

__attribute__((used))
void unit_resume();

__attribute__((used))
void unit_suspend();

__attribute__((used))
void unit_render(const float *in, float *out, uint32_t frames);

__attribute__((used))
void unit_note_on(uint8_t note, uint8_t velocity);

__attribute__((used))
void unit_note_off(uint8_t note);

__attribute__((used))
void unit_all_note_off();

__attribute__((used))
void unit_pitch_bend(uint16_t bend);

__attribute__((used))
void unit_channel_pressure(uint8_t pressure);

__attribute__((used))
void unit_aftertouch(uint8_t note, uint8_t aftertouch);

__attribute__((used))
void unit_set_param_value(uint8_t id, int32_t value);

__attribute__((used))
int32_t unit_get_param_value(uint8_t id);

__attribute__((used))
const char * unit_get_param_str_value(uint8_t id, int32_t value);

__attribute__((used))
void unit_set_tempo(uint32_t tempo);

__attribute__((used))
void unit_tempo_4ppqn_tick(uint32_t counter);

__attribute__((used))
void unit_gate_on(uint8_t velocity);

__attribute__((used))
void unit_gate_off();

} // extern "C"

// ---- Helper Functions ----

static inline void clear_buffers(float *out, uint32_t frames) {
  for (uint32_t i = 0; i < frames * 2; ++i) {
    out[i] = 0.f;
  }
}

static inline void convert_mono_to_stereo(const float *mono, float *stereo, uint32_t frames) {
  for (uint32_t i = 0; i < frames; ++i) {
    stereo[i * 2] = mono[i];
    stereo[i * 2 + 1] = mono[i];
  }
}

// ---- Unit API Implementation ----

void unit_init(uint32_t platform, uint32_t api_version) {
  // Initialize the OSC adapter
  osc_adapter_init(platform, api_version);
  
  // Reset state
  s_unit_state.initialized = true;
  s_unit_state.flags = 0;
  s_unit_state.note = 60;
  s_unit_state.velocity = 100;
  s_unit_state.sample_count = 0;
  
  // Clear parameter storage
  for (int i = 0; i < 24; ++i) {
    s_unit_state.params[i].value = 0;
  }
}

void unit_teardown() {
  if (!s_unit_state.initialized) {
    return;
  }
  
  s_unit_state.initialized = false;
  s_unit_state.flags = 0;
}

void unit_reset() {
  if (!s_unit_state.initialized) {
    return;
  }
  
  // Reset sample counter and state
  s_unit_state.sample_count = 0;
  s_unit_state.note = 60;
  s_unit_state.velocity = 100;
  
  // Forward to OSC adapter
  // OSC modules typically handle reset through note off events
  unit_all_note_off();
}

void unit_resume() {
  if (!s_unit_state.initialized) {
    return;
  }
  
  // Resume operation - clear any suspend flags
  s_unit_state.flags &= ~(1U << 0);
}

void unit_suspend() {
  if (!s_unit_state.initialized) {
    return;
  }
  
  // Suspend operation - set suspend flag
  s_unit_state.flags |= (1U << 0);
  
  // Turn off all notes when suspending
  unit_all_note_off();
}

void unit_render(const float *in, float *out, uint32_t frames) {
  if (!s_unit_state.initialized || (s_unit_state.flags & (1U << 0))) {
    // Not initialized or suspended - output silence
    clear_buffers(out, frames);
    return;
  }
  
  // Drumlogue expects interleaved stereo output (L, R, L, R, ...)
  // OSC API typically generates mono output
  
  // Allocate temporary mono buffer on stack for smaller frame counts
  if (frames <= 64) {
    float mono_buffer[64];
    
    // Call OSC adapter to render audio
    osc_adapter_render(mono_buffer, frames);
    
    // Convert mono to stereo
    convert_mono_to_stereo(mono_buffer, out, frames);
  } else {
    // For larger frames, process in chunks to avoid stack overflow
    const uint32_t chunk_size = 64;
    float mono_buffer[64];
    uint32_t remaining = frames;
    uint32_t offset = 0;
    
    while (remaining > 0) {
      uint32_t to_process = (remaining < chunk_size) ? remaining : chunk_size;
      
      osc_adapter_render(mono_buffer, to_process);
      convert_mono_to_stereo(mono_buffer, out + (offset * 2), to_process);
      
      offset += to_process;
      remaining -= to_process;
    }
  }
  
  s_unit_state.sample_count += frames;
}

void unit_note_on(uint8_t note, uint8_t velocity) {
  if (!s_unit_state.initialized) {
    return;
  }
  
  s_unit_state.note = note;
  s_unit_state.velocity = velocity;
  
  // Forward to OSC adapter
  osc_adapter_note_on(note, velocity);
}

void unit_note_off(uint8_t note) {
  if (!s_unit_state.initialized) {
    return;
  }
  
  // Forward to OSC adapter
  osc_adapter_note_off(note);
}

void unit_all_note_off() {
  if (!s_unit_state.initialized) {
    return;
  }
  
  // Forward to OSC adapter
  osc_adapter_note_off(s_unit_state.note);
  
  s_unit_state.velocity = 0;
}

void unit_pitch_bend(uint16_t bend) {
  if (!s_unit_state.initialized) {
    return;
  }
  
  // Convert 14-bit pitch bend (0-16383, center at 8192) to signed value
  // OSC API expects pitch bend in semitones or normalized format
  int16_t signed_bend = static_cast<int16_t>(bend) - 8192;
  
  // Forward to OSC adapter
  osc_adapter_pitch_bend(signed_bend);
}

void unit_channel_pressure(uint8_t pressure) {
  if (!s_unit_state.initialized) {
    return;
  }
  
  // Forward channel pressure as a parameter change
  // Many OSC modules map this to filter or amplitude modulation
  user_osc_param_t param;
  param.value = pressure;
  osc_adapter_param(k_user_osc_param_id1, &param);
}

void unit_aftertouch(uint8_t note, uint8_t aftertouch) {
  if (!s_unit_state.initialized) {
    return;
  }
  
  // Polyphonic aftertouch - forward as parameter if note matches current note
  if (note == s_unit_state.note) {
    user_osc_param_t param;
    param.value = aftertouch;
    osc_adapter_param(k_user_osc_param_id2, &param);
  }
}

void unit_set_param_value(uint8_t id, int32_t value) {
  if (!s_unit_state.initialized || id >= 24) {
    return;
  }
  
  // Store parameter value
  s_unit_state.params[id].value = value;
  
  // Forward to OSC adapter with appropriate parameter ID mapping
  osc_adapter_param(static_cast<user_osc_param_id_t>(id), &s_unit_state.params[id]);
}

int32_t unit_get_param_value(uint8_t id) {
  if (!s_unit_state.initialized || id >= 24) {
    return 0;
  }
  
  return s_unit_state.params[id].value;
}

const char * unit_get_param_str_value(uint8_t id, int32_t value) {
  if (!s_unit_state.initialized || id >= 24) {
    return nullptr;
  }
  
  // Forward to OSC adapter
  return osc_adapter_get_param_str(static_cast<user_osc_param_id_t>(id), value);
}

void unit_set_tempo(uint32_t tempo) {
  if (!s_unit_state.initialized) {
    return;
  }
  
  // Tempo is typically in BPM * 10 (e.g., 1200 = 120.0 BPM)
  // Forward to OSC adapter for tempo-synced effects
  osc_adapter_set_tempo(tempo);
}

void unit_tempo_4ppqn_tick(uint32_t counter) {
  if (!s_unit_state.initialized) {
    return;
  }
  
  // Forward tempo clock ticks to OSC adapter
  // 4ppqn = 4 pulses per quarter note
  osc_adapter_tempo_tick(counter);
}

void unit_gate_on(uint8_t velocity) {
  if (!s_unit_state.initialized) {
    return;
  }
  
  // Gate on with velocity - trigger envelope
  s_unit_state.velocity = velocity;
  unit_note_on(s_unit_state.note, velocity);
}

void unit_gate_off() {
  if (!s_unit_state.initialized) {
    return;
  }
  
  // Gate off - release envelope
  unit_note_off(s_unit_state.note);
}

// ---- Unit Header Structure ----

// The unit header contains metadata about the synth module
// This structure must be exported with specific alignment and naming

struct unit_header {
  uint32_t magic;           // Magic number for validation
  uint32_t api_version;     // API version
  uint32_t platform;        // Platform identifier
  uint32_t reserved0;       // Reserved for future use
  
  const char *name;         // Module name
  const char *category;     // Module category
  const char *author;       // Author name
  const char *description;  // Module description
  
  uint32_t num_params;      // Number of parameters
  uint32_t reserved1[3];    // Reserved for future use
};

// Export unit header with proper section placement
__attribute__((section(".unit_header")))
__attribute__((used))
const unit_header unit_header_data = {
  .magic = 0x54494E55,  // 'UNIT' in hex
  .api_version = 0x0100,
  .platform = 0x44524D4C,  // 'DRML' for Drumlogue
  .reserved0 = 0,
  
  .name = "Eurorack OSC",
  .category = "Oscillator",
  .author = "Eurorack Port",
  .description = "Ported Eurorack oscillator module for Drumlogue",
  
  .num_params = 6,
  .reserved1 = {0, 0, 0}
};

// ---- Parameter Descriptors ----

// Optional: Export parameter descriptors for UI integration
struct param_descriptor {
  uint8_t id;
  uint8_t type;
  uint16_t flags;
  const char *name;
  int32_t min;
  int32_t max;
  int32_t def;
};

__attribute__((section(".unit_params")))
__attribute__((used))
const param_descriptor unit_params[] = {
  {0,  0, 0, "Shape",      0, 100, 0},
  {1,  0, 0, "Alt Shape",  0, 100, 0},
  {2,  0, 0, "Parameter 1", 0, 100, 50},
  {3,  0, 0, "Parameter 2", 0, 100, 50},
  {4,  0, 0, "Parameter 3", 0, 100, 50},
  {5,  0, 0, "Parameter 4", 0, 100, 50},
};

// ---- Module Info Functions ----

extern "C" {

__attribute__((used))
const char * unit_get_module_name() {
  return unit_header_data.name;
}

__attribute__((used))
const char * unit_get_module_category() {
  return unit_header_data.category;
}

__attribute__((used))
const char * unit_get_module_author() {
  return unit_header_data.author;
}

__attribute__((used))
const char * unit_get_module_description() {
  return unit_header_data.description;
}

__attribute__((used))
uint32_t unit_get_num_params() {
  return unit_header_data.num_params;
}

} // extern "C"
