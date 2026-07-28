/*
 * File: drumlogue_delfx_wrapper.cc
 *
 * Drumlogue Delay-FX (delfx) Unit Wrapper for CloudsFX
 *
 * Bridges the logue-sdk v2.0 delfx Module API (unit_*) to the CloudsFX
 * engine (clouds-fx.cc), which granulates the incoming FX-bus audio.
 *
 * This is deliberately separate from drumlogue_unit_wrapper.cc (the synth
 * wrapper): a delfx unit receives a stereo audio input in unit_render(),
 * which synth units ignore.  It also has no MIDI-note pitch, no internal
 * LFO, and no sample-bank access, so this wrapper is much thinner.
 *
 * Architecture:
 *   Drumlogue Runtime (FX bus)
 *        |  unit_render(in, out, frames)   <-- stereo in, stereo out
 *        v
 *   drumlogue_delfx_wrapper.cc   <-- this file
 *        |  clouds_fx_process(in, out, frames)
 *        v
 *   clouds-fx.cc  ->  clouds/dsp/granular_processor  (unchanged engine)
 */

#include <cstdint>
#include <cstring>

#include "runtime.h"
#include "unit.h"
#include "attributes.h"
#include "drumlogue_fpu.h"

/* CloudsFX engine entry points (clouds-fx.cc) */
extern "C" {
void clouds_fx_init(void);
void clouds_fx_request_reset(void);
void clouds_fx_set_param(uint8_t id, int32_t value);
void clouds_fx_process(const float *in, float *out, uint32_t frames);
}

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

  /* Validate target platform (must match our header exactly: delfx) */
  if (desc->target != unit_header.target)
    return k_unit_err_target;

  if (desc->api < k_unit_api_2_0_0)
    return k_unit_err_api_version;

  if (desc->samplerate != 48000)
    return k_unit_err_samplerate;

  s_state.samplerate        = desc->samplerate;
  s_state.frames_per_buffer = desc->frames_per_buffer;
  s_state.flags             = 0;
  memset(s_state.param_values, 0, sizeof(s_state.param_values));

  clouds_fx_init();

  s_state.initialized = true;
  return k_unit_err_none;
}

__unit_callback
void unit_teardown() {
  s_state.initialized = false;
  s_state.flags = 0;
}

__unit_callback
void unit_reset() {
  if (!s_state.initialized) return;
  /* Only latch the request: re-initializing the engine rewrites the buffers
   * unit_render() is reading, so it has to happen on the audio thread. */
  clouds_fx_request_reset();
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
}

/* ===========================================================================
 * Audio Rendering
 *
 * Drumlogue provides interleaved stereo float buffers.  For a delfx unit,
 * `in` is the FX-send bus (the audio to process) and `out` is the returned
 * wet/dry mix.  CloudsFX processes stereo in-place through the engine.
 * ======================================================================== */

static inline void copy_through(const float *in, float *out, uint32_t frames) {
  if (in)
    memcpy(out, in, frames * 2 * sizeof(float));
  else
    memset(out, 0, frames * 2 * sizeof(float));
}

__unit_callback
void unit_render(const float *in, float *out, uint32_t frames) {
  if (!s_state.initialized || (s_state.flags & k_flag_suspended) || !in) {
    /* When suspended or without input, pass the dry signal through. */
    copy_through(in, out, frames);
    return;
  }
  /* Flush denormals for the whole render — the reverb/diffuser tails and the
   * parameter smoothers decay straight into them.  See drumlogue_fpu.h. */
  const uint32_t fpscr = fpu_begin_audio();
  clouds_fx_process(in, out, frames);
  fpu_end_audio(fpscr);
}

/* ===========================================================================
 * Note / MIDI Callbacks (delfx units are not pitched — all no-ops)
 * ======================================================================== */

__unit_callback void unit_note_on(uint8_t note, uint8_t velocity) {
  (void)note; (void)velocity;
}
__unit_callback void unit_note_off(uint8_t note) { (void)note; }
__unit_callback void unit_all_note_off(void) {}
__unit_callback void unit_gate_on(uint8_t velocity) { (void)velocity; }
__unit_callback void unit_gate_off(void) {}
__unit_callback void unit_pitch_bend(uint16_t bend) { (void)bend; }
__unit_callback void unit_channel_pressure(uint8_t pressure) { (void)pressure; }
__unit_callback void unit_aftertouch(uint8_t note, uint8_t aftertouch) {
  (void)note; (void)aftertouch;
}

/* ===========================================================================
 * Parameter Callbacks
 *
 * CloudsFX param layout (see header.c CLOUDS_FX block):
 *   id 0  Position   (0-100 %)
 *   id 1  Size       (0-100 %)
 *   id 2  Density    (0-100 %)
 *   id 3  Texture    (0-100 %)
 *   id 4  Pitch      (0-48, centered 24)
 *   id 5  Feedback   (0-100 %)
 *   id 6  Dry/Wet    (0-100 %)
 *   id 7  Reverb     (0-100 %)
 *   id 8  Freeze     (0/1)
 *   id 9  Mode       (0-3 strings)
 *   id 10 Quality    (0-3 strings)
 *
 * The ids map 1:1 to clouds_fx_set_param(), which does the scaling.
 * ======================================================================== */

__unit_callback
void unit_set_param_value(uint8_t id, int32_t value) {
  if (!s_state.initialized || id >= UNIT_MAX_PARAM_COUNT) return;
  s_state.param_values[id] = value;
  clouds_fx_set_param(id, value);
}

__unit_callback
int32_t unit_get_param_value(uint8_t id) {
  if (!s_state.initialized || id >= UNIT_MAX_PARAM_COUNT) return 0;
  return s_state.param_values[id];
}

static const char * const s_clouds_mode_names[] = {
  "Granular", "Stretch", "Delay", "Spectral"
};
#define NUM_CLOUDS_MODES 4

static const char * const s_clouds_quality_names[] = {
  "StHi", "MoHi", "StLo", "MoLo"
};
#define NUM_CLOUDS_QUALITIES 4

__unit_callback
const char * unit_get_param_str_value(uint8_t id, int32_t value) {
  switch (id) {
    case 9: /* Mode */
      if (value >= 0 && value < NUM_CLOUDS_MODES)
        return s_clouds_mode_names[value];
      break;
    case 10: /* Quality */
      if (value >= 0 && value < NUM_CLOUDS_QUALITIES)
        return s_clouds_quality_names[value];
      break;
  }
  return nullptr;
}

__unit_callback
const uint8_t * unit_get_param_bmp_value(uint8_t id, int32_t value) {
  (void)id; (void)value;
  return nullptr;
}

/* ===========================================================================
 * Preset Callbacks (stubs - no presets)
 * ======================================================================== */

__unit_callback uint8_t unit_get_preset_index(void) { return 0; }
__unit_callback const char * unit_get_preset_name(uint8_t idx) {
  (void)idx; return nullptr;
}
__unit_callback void unit_load_preset(uint8_t idx) { (void)idx; }

/* ===========================================================================
 * Tempo Callback (CloudsFX does not tempo-sync)
 * ======================================================================== */

__unit_callback void unit_set_tempo(uint32_t tempo) { (void)tempo; }

} /* extern "C" */
