/*
 * File: drumlogue_unit_wrapper.cc
 *
 * Drumlogue Synth Module Unit Wrapper
 *
 * Bridges the logue-sdk v2.0 Synth Module API (unit_*) to the v1.x
 * User Oscillator API (OSC_*). This allows the same oscillator source
 * code (macro-oscillator2.cc, modal-strike.cc) to run on both
 * prologue-class platforms and drumlogue without modification.
 *
 * Architecture:
 *   Drumlogue Runtime
 *        |  (Synth Module API: unit_init, unit_render, ...)
 *        v
 *   drumlogue_unit_wrapper.cc   <-- this file
 *        |  (calls adapter functions)
 *        v
 *   drumlogue_osc_adapter.cc
 *        |  (User OSC API: OSC_INIT, OSC_CYCLE, ...)
 *        v
 *   macro-oscillator2.cc / modal-strike.cc  (unchanged source)
 */

#include <cstdint>
#include <cstring>

#include "drumlogue_osc_adapter.h"

/* SDK headers for drumlogue types */
#include "runtime.h"
#include "unit.h"
#include "attributes.h"

/* ===========================================================================
 * Unit Header
 *
 * Exported in the .unit_header ELF section. The drumlogue runtime reads
 * this to identify the unit, its API version, and parameter descriptors.
 * ======================================================================== */

#define UNIT_PARAM_PERCENT(pname, pmin, pmax, pcenter, pinit) \
  { (pmin), (pmax), (pcenter), (pinit), k_unit_param_type_percent, 0, 0, 0, {pname} }

#define UNIT_PARAM_ENUM(pname, pmin, pmax, pinit) \
  { (pmin), (pmax), 0, (pinit), k_unit_param_type_enum, 0, 0, 0, {pname} }

#define UNIT_PARAM_NONE() \
  { 0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""} }

const __unit_header unit_header_t unit_header = {
  .header_size  = sizeof(unit_header_t),
  .target       = UNIT_TARGET_PLATFORM | k_unit_module_synth,
  .api          = UNIT_API_VERSION,
  .dev_id       = 0x0U,
  .unit_id      = 0x0U,
  .version      = 0x00010600U,   /* v1.6.0 matching project version */
  .name         = "EurorkOSC",   /* max 13 chars */
  .num_presets  = 0,
  .num_params   = 6,
  .params       = {
    /* id 0 */ UNIT_PARAM_PERCENT("Shape",      0, 100, 0,  0),
    /* id 1 */ UNIT_PARAM_PERCENT("ShiftShape",  0, 100, 0,  0),
    /* id 2 */ UNIT_PARAM_PERCENT("Param 1",    0, 100, 50, 50),
    /* id 3 */ UNIT_PARAM_PERCENT("Param 2",    0, 100, 50, 50),
    /* id 4 */ UNIT_PARAM_ENUM("LFO Target",    0, 7, 0),
    /* id 5 */ UNIT_PARAM_PERCENT("LFO2 Rate",  0, 100, 0,  0),
    /* remaining slots unused */
    UNIT_PARAM_NONE(), UNIT_PARAM_NONE(), UNIT_PARAM_NONE(),
    UNIT_PARAM_NONE(), UNIT_PARAM_NONE(), UNIT_PARAM_NONE(),
    UNIT_PARAM_NONE(), UNIT_PARAM_NONE(), UNIT_PARAM_NONE(),
    UNIT_PARAM_NONE(), UNIT_PARAM_NONE(), UNIT_PARAM_NONE(),
    UNIT_PARAM_NONE(), UNIT_PARAM_NONE(), UNIT_PARAM_NONE(),
    UNIT_PARAM_NONE(), UNIT_PARAM_NONE(), UNIT_PARAM_NONE(),
  }
};

/* ===========================================================================
 * Module State
 * ======================================================================== */

enum {
  k_flag_suspended = (1U << 0),
};

static struct {
  bool     initialized;
  uint32_t flags;
  uint32_t samplerate;
  uint16_t frames_per_buffer;

  /* Current note state */
  uint8_t  note;
  uint8_t  velocity;

  /* Stored parameter values (drumlogue int32 range) */
  int32_t  param_values[UNIT_MAX_PARAM_COUNT];
} s_state;

/* ===========================================================================
 * Lifecycle Callbacks
 * ======================================================================== */

extern "C" {

__unit_callback
int8_t unit_init(const unit_runtime_desc_t *desc) {
  if (!desc)
    return k_unit_err_undef;

  /* Validate target platform */
  if ((desc->target & 0xFF00) != k_unit_target_drumlogue)
    return k_unit_err_target;

  /* Validate API version (require 2.0.0+) */
  if (desc->api < k_unit_api_2_0_0)
    return k_unit_err_api_version;

  /* Validate sample rate */
  if (desc->samplerate != 48000)
    return k_unit_err_samplerate;

  /* Store runtime info */
  s_state.samplerate        = desc->samplerate;
  s_state.frames_per_buffer = desc->frames_per_buffer;
  s_state.flags             = 0;
  s_state.note              = 60;  /* middle C */
  s_state.velocity          = 0;

  memset(s_state.param_values, 0, sizeof(s_state.param_values));

  /* Initialize the OSC adapter, which calls OSC_INIT */
  osc_adapter_init(desc->target, desc->api);

  s_state.initialized = true;
  return k_unit_err_none;
}

__unit_callback
void unit_teardown() {
  osc_adapter_teardown();
  s_state.initialized = false;
  s_state.flags = 0;
}

__unit_callback
void unit_reset() {
  if (!s_state.initialized) return;

  s_state.note     = 60;
  s_state.velocity = 0;
  osc_adapter_reset();
}

__unit_callback
void unit_resume() {
  if (!s_state.initialized) return;
  s_state.flags &= ~k_flag_suspended;
}

__unit_callback
void unit_suspend() {
  if (!s_state.initialized) return;
  s_state.flags |= k_flag_suspended;
  osc_adapter_note_off(s_state.note);
}

/* ===========================================================================
 * Audio Rendering
 *
 * Drumlogue provides interleaved stereo float buffers.
 * The OSC API produces Q31 output via OSC_CYCLE:
 *   - macro-oscillator2: mono Q31, fixed plaits::kMaxBlockSize (24) frames
 *   - modal-strike: interleaved stereo Q31, 2 * elements::kMaxBlockSize frames
 *
 * We call osc_adapter_render() which handles Q31->float conversion,
 * then copy/interleave into the drumlogue stereo output.
 * ======================================================================== */

static inline void clear_output(float *out, uint32_t frames) {
  memset(out, 0, frames * 2 * sizeof(float));
}

__unit_callback
void unit_render(const float *in, float *out, uint32_t frames) {
  (void)in;  /* synth units ignore input */

  if (!s_state.initialized || (s_state.flags & k_flag_suspended)) {
    clear_output(out, frames);
    return;
  }

  /*
   * osc_adapter_render fills a mono float buffer.
   * We then duplicate to stereo interleaved.
   * Process in chunks to stay within stack limits.
   */
  const uint32_t chunk_size = 64;
  float mono[64];
  uint32_t offset = 0;
  uint32_t remaining = frames;

  while (remaining > 0) {
    uint32_t n = (remaining < chunk_size) ? remaining : chunk_size;

    osc_adapter_render(mono, n);

    /* Mono to interleaved stereo */
    float *dst = out + (offset * 2);
    for (uint32_t i = 0; i < n; ++i) {
      dst[i * 2]     = mono[i];
      dst[i * 2 + 1] = mono[i];
    }

    offset    += n;
    remaining -= n;
  }
}

/* ===========================================================================
 * Note / MIDI Callbacks
 * ======================================================================== */

__unit_callback
void unit_note_on(uint8_t note, uint8_t velocity) {
  if (!s_state.initialized) return;
  s_state.note     = note;
  s_state.velocity = velocity;
  osc_adapter_note_on(note, velocity);
}

__unit_callback
void unit_note_off(uint8_t note) {
  if (!s_state.initialized) return;
  osc_adapter_note_off(note);
}

__unit_callback
void unit_all_note_off(void) {
  if (!s_state.initialized) return;
  osc_adapter_note_off(s_state.note);
  s_state.velocity = 0;
}

__unit_callback
void unit_gate_on(uint8_t velocity) {
  if (!s_state.initialized) return;
  s_state.velocity = velocity;
  osc_adapter_note_on(s_state.note, velocity);
}

__unit_callback
void unit_gate_off(void) {
  if (!s_state.initialized) return;
  osc_adapter_note_off(s_state.note);
}

__unit_callback
void unit_pitch_bend(uint16_t bend) {
  if (!s_state.initialized) return;
  /* 14-bit: 0x0000..0x3FFF, neutral at 0x2000 */
  int16_t signed_bend = (int16_t)bend - 0x2000;
  osc_adapter_pitch_bend(signed_bend);
}

__unit_callback
void unit_channel_pressure(uint8_t pressure) {
  if (!s_state.initialized) return;
  /* Map channel pressure to shape LFO modulation depth */
  osc_adapter_set_shape_lfo((float)pressure / 127.f);
}

__unit_callback
void unit_aftertouch(uint8_t note, uint8_t aftertouch) {
  if (!s_state.initialized) return;
  (void)note;
  (void)aftertouch;
  /* Polyphonic aftertouch: no direct mapping in OSC API */
}

/* ===========================================================================
 * Parameter Callbacks
 *
 * Drumlogue params (int32_t, range defined in header) are mapped to
 * the OSC API's parameter system:
 *   id 0 -> k_user_osc_param_shape      (10-bit: 0-1023)
 *   id 1 -> k_user_osc_param_shiftshape (10-bit: 0-1023)
 *   id 2 -> k_user_osc_param_id1        (0-200 i.e -100% - +100%)
 *   id 3 -> k_user_osc_param_id2        (0-100 percentage)
 *   id 4 -> k_user_osc_param_id3        (LFO target select)
 *   id 5 -> k_user_osc_param_id4        (LFO2 rate)
 *
 * The remaining OSC params (id5=LFO2 Int, id6=LFO2 Target) could be
 * exposed if needed by adding entries to the unit_header.params table.
 * ======================================================================== */

__unit_callback
void unit_set_param_value(uint8_t id, int32_t value) {
  if (!s_state.initialized || id >= UNIT_MAX_PARAM_COUNT) return;

  s_state.param_values[id] = value;

  /* Map drumlogue param id to OSC param id and scale */
  uint16_t osc_value;
  user_osc_param_id_t osc_id;

  switch (id) {
    case 0: /* Shape: 0-100 -> 10-bit (0-1023) */
      osc_id    = k_user_osc_param_shape;
      osc_value = (uint16_t)((value * 1023 + 50) / 100);
      break;
    case 1: /* Shift-Shape: 0-100 -> 10-bit (0-1023) */
      osc_id    = k_user_osc_param_shiftshape;
      osc_value = (uint16_t)((value * 1023 + 50) / 100);
      break;
    case 2: /* Param 1: 0-100 -> 0-200 (bipolar centered at 100) */
      osc_id    = k_user_osc_param_id1;
      osc_value = (uint16_t)(value * 2);
      break;
    case 3: /* Param 2: 0-100 -> 0-100 (percent) */
      osc_id    = k_user_osc_param_id2;
      osc_value = (uint16_t)value;
      break;
    case 4: /* LFO Target: direct enum value */
      osc_id    = k_user_osc_param_id3;
      osc_value = (uint16_t)value;
      break;
    case 5: /* LFO2 Rate: 0-100 percent */
      osc_id    = k_user_osc_param_id4;
      osc_value = (uint16_t)value;
      break;
    default:
      return;
  }

  osc_adapter_set_param(osc_id, osc_value);
}

__unit_callback
int32_t unit_get_param_value(uint8_t id) {
  if (!s_state.initialized || id >= UNIT_MAX_PARAM_COUNT) return 0;
  return s_state.param_values[id];
}

__unit_callback
const char * unit_get_param_str_value(uint8_t id, int32_t value) {
  (void)id;
  (void)value;
  /* Let the runtime use the default numeric display */
  return nullptr;
}

__unit_callback
const uint8_t * unit_get_param_bmp_value(uint8_t id, int32_t value) {
  (void)id;
  (void)value;
  return nullptr;
}

/* ===========================================================================
 * Preset Callbacks (stubs - no presets)
 * ======================================================================== */

__unit_callback
uint8_t unit_get_preset_index(void) {
  return 0;
}

__unit_callback
const char * unit_get_preset_name(uint8_t idx) {
  (void)idx;
  return nullptr;
}

__unit_callback
void unit_load_preset(uint8_t idx) {
  (void)idx;
}

/* ===========================================================================
 * Tempo Callback
 * ======================================================================== */

__unit_callback
void unit_set_tempo(uint32_t tempo) {
  if (!s_state.initialized) return;
  osc_adapter_set_tempo(tempo);
}

} /* extern "C" */
