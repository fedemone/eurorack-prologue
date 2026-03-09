/*
 * File: drumlogue_unit_wrapper.cc
 *
 * Drumlogue Synth Module Unit Wrapper
 *
 * Bridges the logue-sdk v2.0 Synth Module API (unit_*) to the v1.x
 * User Oscillator API (OSC_*). This allows the same oscillator source
 * code (macro-oscillator2.cc, modal-strike.cc, rings-resonator.cc,
 * clouds-granular.cc) to run on both prologue-class platforms and
 * drumlogue without modification.
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

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

#include "drumlogue_osc_adapter.h"

/* SDK headers for drumlogue types */
#include "runtime.h"
#include "unit.h"
#include "attributes.h"

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
  uint8_t  note;       /* last MIDI note from unit_note_on */
  uint8_t  velocity;
  uint8_t  base_note;  /* user param: note for gate trigger (default 60) */

  /* Stored parameter values (drumlogue int32 range) */
  int32_t  param_values[UNIT_MAX_PARAM_COUNT];

  /* Sample access function pointers from runtime descriptor */
  unit_runtime_get_num_sample_banks_ptr     get_num_sample_banks;
  unit_runtime_get_num_samples_for_bank_ptr get_num_samples_for_bank;
  unit_runtime_get_sample_ptr               get_sample;
} s_state;

/* ===========================================================================
 * Lifecycle Callbacks
 * ======================================================================== */

extern "C" {

__unit_callback
int8_t unit_init(const unit_runtime_desc_t *desc) {
  if (!desc)
    return k_unit_err_undef;

  /* Validate target platform (must match our header exactly) */
  if (desc->target != unit_header.target)
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
  s_state.base_note         = 60;  /* default base note for gate trigger */

  memset(s_state.param_values, 0, sizeof(s_state.param_values));

  /* Store sample access function pointers */
  s_state.get_num_sample_banks     = desc->get_num_sample_banks;
  s_state.get_num_samples_for_bank = desc->get_num_samples_for_bank;
  s_state.get_sample               = desc->get_sample;

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

  s_state.note      = 60;
  s_state.velocity  = 0;
  s_state.base_note = 60;
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

/**
 * Copy mono buffer to interleaved stereo (L=R).
 * NEON path uses vst2q_f32 to interleave 4 samples at a time.
 */
static void mono_to_stereo(const float *mono, float *stereo, uint32_t count) {
#ifdef __ARM_NEON
  uint32_t i = 0;
  for (; i + 4 <= count; i += 4) {
    float32x4_t m = vld1q_f32(mono + i);
    float32x4x2_t s;
    s.val[0] = m;  /* L channels */
    s.val[1] = m;  /* R channels */
    vst2q_f32(stereo + i * 2, s);
  }
  for (; i < count; ++i) {
    stereo[i * 2]     = mono[i];
    stereo[i * 2 + 1] = mono[i];
  }
#else
  for (uint32_t i = 0; i < count; ++i) {
    stereo[i * 2]     = mono[i];
    stereo[i * 2 + 1] = mono[i];
  }
#endif
}

__unit_callback
void unit_render(const float *in, float *out, uint32_t frames) {
  (void)in;  /* synth units ignore input */

  if (!s_state.initialized || (s_state.flags & k_flag_suspended)) {
    clear_output(out, frames);
    return;
  }

  /*
   * Render audio and write to stereo interleaved output.
   * Process in chunks to stay within stack limits.
   */
  const uint32_t chunk_size = 64;
  uint32_t offset = 0;
  uint32_t remaining = frames;

#if defined(MUSSOLA_VOCAL)
  /* Mussola: use stereo render path for true stereo spread */
  float left[64], right[64];
  while (remaining > 0) {
    uint32_t n = (remaining < chunk_size) ? remaining : chunk_size;

    osc_adapter_render_stereo(left, right, n);
    float *dst = out + (offset * 2);
    for (uint32_t i = 0; i < n; ++i) {
      dst[i * 2]     = left[i];
      dst[i * 2 + 1] = right[i];
    }

    offset    += n;
    remaining -= n;
  }
#else
  /* Other modules: mono render duplicated to stereo */
  float mono[64];
  while (remaining > 0) {
    uint32_t n = (remaining < chunk_size) ? remaining : chunk_size;

    osc_adapter_render(mono, n);
    mono_to_stereo(mono, out + (offset * 2), n);

    offset    += n;
    remaining -= n;
  }
#endif
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
  osc_adapter_note_on(s_state.base_note, velocity);
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
 * Per-oscillator param mapping via compile-time #ifdef.
 * See header.c for the full param layout per oscillator type.
 *
 * Clouds (clouds-granular.cc):
 *   id 0  -> base_note  (MIDI 0-127, stored locally)
 *   id 1  -> shape      (10-bit: 0-1023)  [Position]
 *   id 2  -> shiftshape (10-bit: 0-1023)  [Size]
 *   id 3  -> id1        (0-100 percent)   [Density]
 *   id 4  -> id2        (0-100 percent)   [Texture]
 *   id 5  -> id3        (0-48, centered 24)[Pitch semitones]
 *   id 6  -> id4        (0-100 percent)   [Feedback]
 *   id 7  -> id5        (0-100 percent)   [Dry/Wet]
 *   id 8  -> id6        (0-100 percent)   [Reverb]
 *   id 9  -> custom 8   (0-1 on/off)     [Freeze]
 *   id 10 -> custom 9   (0-3 mode)       [Mode]
 *   id 11 -> custom 10  (0-3 quality)    [Quality]
 *   id 12 -> custom 11  (0-15 bank)      [SampleBank]
 *   id 13 -> custom 12  (0-64 number)    [SampleNum]
 *   id 14 -> custom 13  (0-1000 permil)  [SmplStart]
 *   id 15 -> custom 14  (0-1000 permil)  [SmplEnd]
 *
 * Rings (rings-resonator.cc):
 *   id 0  -> base_note  (MIDI 0-127, stored locally)
 *   id 1  -> shape      (10-bit: 0-1023)  [Position]
 *   id 2  -> shiftshape (10-bit: 0-1023)  [Structure]
 *   id 3  -> id1        (0-100 percent)   [Brightness]
 *   id 4  -> id2        (0-100 percent)   [Damping]
 *   id 5  -> id3        (0-10 chord)      [Chord]
 *   id 6  -> custom 8   (0-5 model)       [Model]
 *   id 7  -> custom 9   (1-4 polyphony)   [Polyphony]
 *
 * Plaits (macro-oscillator2.cc):
 *   id 0  -> base_note  (MIDI 0-127, stored locally)
 *   id 1  -> shape      (10-bit: 0-1023)
 *   id 2  -> shiftshape (10-bit: 0-1023)
 *   id 3  -> id1        (0-200 bipolar, centered at 100)
 *   id 4  -> id2        (0-100 percent)
 *   id 5  -> id3        (LFO target strings)
 *   id 6  -> custom 11  (LFO1 shape strings)
 *   id 7  -> id4        (LFO2 rate 0-100)
 *   id 8  -> id5        (LFO2 depth 0-100)
 *   id 9  -> id6        (LFO2 target strings)
 *   id 10 -> custom 12  (LFO2 shape strings)
 *   id 11 -> custom 13  (Gate mode strings)
 *
 * Elements (modal-strike.cc):
 *   id 0  -> base_note  (MIDI 0-127, stored locally)
 *   id 1  -> shape      (10-bit: 0-1023)  [Position]
 *   id 2  -> shiftshape (10-bit: 0-1023)  [Geometry]
 *   id 3  -> id1        (0-100 percent)   [Strength]
 *   id 4  -> id2        (0-100 percent)   [Mallet]
 *   id 5  -> id3        (0-100 percent)   [Timbre]
 *   id 6  -> id4        (0-100 percent)   [Damping]
 *   id 7  -> id5        (0-100 percent)   [Brightness]
 *   id 8  -> id6        (LFO target strings 0-8)
 *   id 9  -> custom 11  (LFO1 shape strings)
 *   id 10 -> custom 8   (LFO2 rate 0-100)
 *   id 11 -> custom 9   (LFO2 depth 0-100)
 *   id 12 -> custom 10  (LFO2 target strings 0-6)
 *   id 13 -> custom 12  (LFO2 shape strings)
 * ======================================================================== */

__unit_callback
void unit_set_param_value(uint8_t id, int32_t value) {
  if (!s_state.initialized || id >= UNIT_MAX_PARAM_COUNT) return;

  s_state.param_values[id] = value;

  /* Map drumlogue param id to OSC param id and scale */
  uint16_t osc_value;
  user_osc_param_id_t osc_id;

#if defined(MUSSOLA_VOCAL)
  /* Mussola custom OSC_PARAM indices (must match mussola.cc enum) */
  enum {
    k_mussola_param_speed     = 8,
    k_mussola_param_prosody   = 9,
    k_mussola_param_decay     = 10,
    k_mussola_param_mix       = 11,
    k_mussola_param_model     = 12,
    k_mussola_param_gate_mode = 13,
    k_mussola_param_voices    = 14,
    k_mussola_param_detune    = 15,
    k_mussola_param_spread    = 16,
    k_mussola_param_gender    = 17,
    k_mussola_param_attack    = 18,
  };
  /* ---- Mussola param mapping ----
   * id 0:  Base Note   -> stored in wrapper
   * id 1:  Phoneme     -> k_user_osc_param_shape (10-bit)
   * id 2:  Timbre      -> k_user_osc_param_shiftshape (10-bit)
   * id 3:  Harmonics   -> k_user_osc_param_id1 (0-100)
   * id 4:  Morph       -> k_user_osc_param_id2 (0-100)
   * id 5:  Speed       -> k_mussola_param_speed (0-100)
   * id 6:  Prosody     -> k_mussola_param_prosody (0-100)
   * id 7:  Decay       -> k_mussola_param_decay (0-100)
   * id 8:  Mix         -> k_mussola_param_mix (0-100)
   * id 9:  Model       -> k_mussola_param_model (0-3)
   * id 10: Gate Mode   -> k_mussola_param_gate_mode (0-2)
   * id 11: Voices      -> k_mussola_param_voices (1-4)
   * id 12: Detune      -> k_mussola_param_detune (0-100)
   * id 13: Spread      -> k_mussola_param_spread (0-100)
   * id 14: Gender      -> k_mussola_param_gender (0-100)
   * id 15: Attack      -> k_mussola_param_attack (0-100)
   */
  switch (id) {
    case 0: /* Base Note: MIDI note 0-127 */
      s_state.base_note = (uint8_t)(value & 0x7F);
      return;
    case 1: /* Phoneme: 0-100 -> 10-bit (0-1023) */
      osc_id    = k_user_osc_param_shape;
      osc_value = (uint16_t)((value * 1023 + 50) / 100);
      break;
    case 2: /* Timbre: 0-100 -> 10-bit (0-1023) */
      osc_id    = k_user_osc_param_shiftshape;
      osc_value = (uint16_t)((value * 1023 + 50) / 100);
      break;
    case 3: /* Harmonics: 0-100 percent */
      osc_id    = k_user_osc_param_id1;
      osc_value = (uint16_t)value;
      break;
    case 4: /* Morph: 0-100 percent */
      osc_id    = k_user_osc_param_id2;
      osc_value = (uint16_t)value;
      break;
    case 5: /* Speed: 0-100 (custom OSC_PARAM index) */
      osc_id    = (user_osc_param_id_t)k_mussola_param_speed;
      osc_value = (uint16_t)value;
      break;
    case 6: /* Prosody: 0-100 (custom OSC_PARAM index) */
      osc_id    = (user_osc_param_id_t)k_mussola_param_prosody;
      osc_value = (uint16_t)value;
      break;
    case 7: /* Decay: 0-100 (custom OSC_PARAM index) */
      osc_id    = (user_osc_param_id_t)k_mussola_param_decay;
      osc_value = (uint16_t)value;
      break;
    case 8: /* Mix: 0-100 (custom OSC_PARAM index) */
      osc_id    = (user_osc_param_id_t)k_mussola_param_mix;
      osc_value = (uint16_t)value;
      break;
    case 9: /* Model: 0-3 (custom OSC_PARAM index) */
      osc_id    = (user_osc_param_id_t)k_mussola_param_model;
      osc_value = (uint16_t)value;
      break;
    case 10: /* Gate Mode: 0-2 (custom OSC_PARAM index) */
      osc_id    = (user_osc_param_id_t)k_mussola_param_gate_mode;
      osc_value = (uint16_t)value;
      break;
    case 11: /* Voices: 1-4 */
      osc_id    = (user_osc_param_id_t)k_mussola_param_voices;
      osc_value = (uint16_t)value;
      break;
    case 12: /* Detune: 0-100 */
      osc_id    = (user_osc_param_id_t)k_mussola_param_detune;
      osc_value = (uint16_t)value;
      break;
    case 13: /* Spread: 0-100 */
      osc_id    = (user_osc_param_id_t)k_mussola_param_spread;
      osc_value = (uint16_t)value;
      break;
    case 14: /* Gender: 0-100 */
      osc_id    = (user_osc_param_id_t)k_mussola_param_gender;
      osc_value = (uint16_t)value;
      break;
    case 15: /* Attack: 0-100 */
      osc_id    = (user_osc_param_id_t)k_mussola_param_attack;
      osc_value = (uint16_t)value;
      break;
    default:
      return;
  }
#elif defined(CLOUDS_GRANULAR)
  /* ---- Clouds param mapping ---- */
  switch (id) {
    case 0: /* Base Note: MIDI note 0-127 */
      s_state.base_note = (uint8_t)(value & 0x7F);
      return;
    case 1: /* Position: 0-100 -> 10-bit (0-1023) */
      osc_id    = k_user_osc_param_shape;
      osc_value = (uint16_t)((value * 1023 + 50) / 100);
      break;
    case 2: /* Size: 0-100 -> 10-bit (0-1023) */
      osc_id    = k_user_osc_param_shiftshape;
      osc_value = (uint16_t)((value * 1023 + 50) / 100);
      break;
    case 3: /* Density: 0-100 percent */
      osc_id    = k_user_osc_param_id1;
      osc_value = (uint16_t)value;
      break;
    case 4: /* Texture: 0-100 percent */
      osc_id    = k_user_osc_param_id2;
      osc_value = (uint16_t)value;
      break;
    case 5: /* Pitch: 0-48 (centered at 24 = 0 semitones) */
      osc_id    = k_user_osc_param_id3;
      osc_value = (uint16_t)(int16_t)(value - 24);
      break;
    case 6: /* Feedback: 0-100 percent */
      osc_id    = k_user_osc_param_id4;
      osc_value = (uint16_t)value;
      break;
    case 7: /* Dry/Wet: 0-100 percent */
      osc_id    = k_user_osc_param_id5;
      osc_value = (uint16_t)value;
      break;
    case 8: /* Reverb: 0-100 percent */
      osc_id    = k_user_osc_param_id6;
      osc_value = (uint16_t)value;
      break;
    case 9: /* Freeze: 0-1 (custom OSC_PARAM index 8) */
      osc_id    = (user_osc_param_id_t)8;
      osc_value = (uint16_t)value;
      break;
    case 10: /* Mode: 0-3 (custom OSC_PARAM index 9) */
      osc_id    = (user_osc_param_id_t)9;
      osc_value = (uint16_t)value;
      break;
    case 11: /* Quality: 0-3 (custom OSC_PARAM index 10) */
      osc_id    = (user_osc_param_id_t)10;
      osc_value = (uint16_t)value;
      break;
    case 12: /* SampleBank: 0-15 (custom OSC_PARAM index 11) */
      osc_id    = (user_osc_param_id_t)11;
      osc_value = (uint16_t)value;
      break;
    case 13: /* SampleNum: 0-64 (custom OSC_PARAM index 12) */
      osc_id    = (user_osc_param_id_t)12;
      osc_value = (uint16_t)value;
      break;
    case 14: /* SmplStart: 0-1000 (custom OSC_PARAM index 13) */
      osc_id    = (user_osc_param_id_t)13;
      osc_value = (uint16_t)value;
      break;
    case 15: /* SmplEnd: 0-1000 (custom OSC_PARAM index 14) */
      osc_id    = (user_osc_param_id_t)14;
      osc_value = (uint16_t)value;
      break;
    default:
      return;
  }
#elif defined(RINGS_RESONATOR)
  /* ---- Rings param mapping ---- */
  switch (id) {
    case 0: /* Base Note: MIDI note 0-127 */
      s_state.base_note = (uint8_t)(value & 0x7F);
      return;
    case 1: /* Position: 0-100 -> 10-bit (0-1023) */
      osc_id    = k_user_osc_param_shape;
      osc_value = (uint16_t)((value * 1023 + 50) / 100);
      break;
    case 2: /* Structure: 0-100 -> 10-bit (0-1023) */
      osc_id    = k_user_osc_param_shiftshape;
      osc_value = (uint16_t)((value * 1023 + 50) / 100);
      break;
    case 3: /* Brightness: 0-100 percent */
      osc_id    = k_user_osc_param_id1;
      osc_value = (uint16_t)value;
      break;
    case 4: /* Damping: 0-100 percent */
      osc_id    = k_user_osc_param_id2;
      osc_value = (uint16_t)value;
      break;
    case 5: /* Chord: 0-10 enum */
      osc_id    = k_user_osc_param_id3;
      osc_value = (uint16_t)value;
      break;
    case 6: /* Model: 0-5 enum (custom OSC_PARAM index 8) */
      osc_id    = (user_osc_param_id_t)8;
      osc_value = (uint16_t)value;
      break;
    case 7: /* Polyphony: 1-4 (custom OSC_PARAM index 9) */
      osc_id    = (user_osc_param_id_t)9;
      osc_value = (uint16_t)value;
      break;
    default:
      return;
  }
#elif defined(ELEMENTS_RESONATOR_MODES)
  /* ---- Elements param mapping ---- */
  switch (id) {
    case 0: /* Base Note: MIDI note 0-127 */
      s_state.base_note = (uint8_t)(value & 0x7F);
      return;
    case 1: /* Position: 0-100 -> 10-bit (0-1023) */
      osc_id    = k_user_osc_param_shape;
      osc_value = (uint16_t)((value * 1023 + 50) / 100);
      break;
    case 2: /* Geometry: 0-100 -> 10-bit (0-1023) */
      osc_id    = k_user_osc_param_shiftshape;
      osc_value = (uint16_t)((value * 1023 + 50) / 100);
      break;
    case 3: /* Strength: 0-100 percent */
      osc_id    = k_user_osc_param_id1;
      osc_value = (uint16_t)value;
      break;
    case 4: /* Mallet: 0-100 percent */
      osc_id    = k_user_osc_param_id2;
      osc_value = (uint16_t)value;
      break;
    case 5: /* Timbre: 0-100 percent */
      osc_id    = k_user_osc_param_id3;
      osc_value = (uint16_t)value;
      break;
    case 6: /* Damping: 0-100 percent */
      osc_id    = k_user_osc_param_id4;
      osc_value = (uint16_t)value;
      break;
    case 7: /* Brightness: 0-100 percent */
      osc_id    = k_user_osc_param_id5;
      osc_value = (uint16_t)value;
      break;
    case 8: /* LFO Target: strings enum 0-8 */
      osc_id    = k_user_osc_param_id6;
      osc_value = (uint16_t)value;
      break;
    case 9: /* LFO1 Shape: strings enum (custom OSC_PARAM index 11) */
      osc_id    = (user_osc_param_id_t)11;
      osc_value = (uint16_t)value;
      break;
    case 10: /* LFO2 Rate: 0-100 percent (custom OSC_PARAM index 8) */
      osc_id    = (user_osc_param_id_t)8;
      osc_value = (uint16_t)value;
      break;
    case 11: /* LFO2 Depth: 0-100 percent (custom OSC_PARAM index 9) */
      osc_id    = (user_osc_param_id_t)9;
      osc_value = (uint16_t)value;
      break;
    case 12: /* LFO2 Target: strings enum 0-6 (custom OSC_PARAM index 10) */
      osc_id    = (user_osc_param_id_t)10;
      osc_value = (uint16_t)value;
      break;
    case 13: /* LFO2 Shape: strings enum (custom OSC_PARAM index 12) */
      osc_id    = (user_osc_param_id_t)12;
      osc_value = (uint16_t)value;
      break;
    default:
      return;
  }
#else
  /* ---- Plaits param mapping ---- */
  switch (id) {
    case 0: /* Base Note: MIDI note 0-127 */
      s_state.base_note = (uint8_t)(value & 0x7F);
      return;
    case 1: /* Shape: 0-100 -> 10-bit (0-1023) */
      osc_id    = k_user_osc_param_shape;
      osc_value = (uint16_t)((value * 1023 + 50) / 100);
      break;
    case 2: /* Shift-Shape: 0-100 -> 10-bit (0-1023) */
      osc_id    = k_user_osc_param_shiftshape;
      osc_value = (uint16_t)((value * 1023 + 50) / 100);
      break;
    case 3: /* Param 1: 0-100 -> 0-200 (bipolar centered at 100) */
      osc_id    = k_user_osc_param_id1;
      osc_value = (uint16_t)(value * 2);
      break;
    case 4: /* Param 2: 0-100 -> 0-100 (percent) */
      osc_id    = k_user_osc_param_id2;
      osc_value = (uint16_t)value;
      break;
    case 5: /* LFO Target: strings enum value */
      osc_id    = k_user_osc_param_id3;
      osc_value = (uint16_t)value;
      break;
    case 6: /* LFO1 Shape: strings enum (custom OSC_PARAM index 11) */
      osc_id    = (user_osc_param_id_t)11;
      osc_value = (uint16_t)value;
      break;
    case 7: /* LFO2 Rate: 0-100 percent */
      osc_id    = k_user_osc_param_id4;
      osc_value = (uint16_t)value;
      break;
    case 8: /* LFO2 Depth: 0-100 percent */
      osc_id    = k_user_osc_param_id5;
      osc_value = (uint16_t)value;
      break;
    case 9: /* LFO2 Target: strings enum value */
      osc_id    = k_user_osc_param_id6;
      osc_value = (uint16_t)value;
      break;
    case 10: /* LFO2 Shape: strings enum (custom OSC_PARAM index 12) */
      osc_id    = (user_osc_param_id_t)12;
      osc_value = (uint16_t)value;
      break;
    default:
      return;
  }
#endif

  osc_adapter_set_param(osc_id, osc_value);
}

__unit_callback
int32_t unit_get_param_value(uint8_t id) {
  if (!s_state.initialized || id >= UNIT_MAX_PARAM_COUNT) return 0;
  return s_state.param_values[id];
}

/* ---- LFO shape names (shared by LFO1 Shape and LFO2 Shape params) ---- */
static const char * const s_lfo_shape_names[] = {
  "Cosine", "Triangle", "Ramp Up", "Ramp Down", "Fat Sine"
};
#define NUM_LFO_SHAPES 5

#if defined(MUSSOLA_VOCAL)
/* ---- Mussola model and gate mode names ---- */
static const char * const s_mussola_model_names[] = {
  "Naive", "SAM", "LPC", "Blend"
};
#define NUM_MUSSOLA_MODELS 4

static const char * const s_mussola_gate_names[] = {
  "Trigger", "Sustain", "Contin."
};
#define NUM_MUSSOLA_GATES 3

static const char * const s_mussola_voices_names[] = {
  "1", "2", "3", "4"
};
#define NUM_MUSSOLA_VOICES 4

#elif defined(CLOUDS_GRANULAR)
/* ---- Clouds mode and quality names ---- */
static const char * const s_clouds_mode_names[] = {
  "Granular", "Stretch", "Delay", "Spectral"
};
#define NUM_CLOUDS_MODES 4

static const char * const s_clouds_quality_names[] = {
  "StHi", "MoHi", "StLo", "MoLo"
};
#define NUM_CLOUDS_QUALITIES 4

#elif defined(RINGS_RESONATOR)
/* ---- Rings model and chord names ---- */
static const char * const s_rings_model_names[] = {
  "Modal", "SympStr", "String", "FM", "SympStrQ", "Str+Verb"
};
#define NUM_RINGS_MODELS 6

static const char * const s_rings_chord_names[] = {
  "Oct", "5th", "sus4", "min", "min7",
  "min9", "min11", "69", "Maj9", "Maj7", "Maj"
};
#define NUM_RINGS_CHORDS 11

static const char * const s_rings_poly_names[] = {
  "1", "2", "3", "4"
};
#define NUM_RINGS_POLY 4

#elif defined(ELEMENTS_RESONATOR_MODES)
/* ---- Elements LFO target names ---- */
static const char * const s_elements_lfo_target_names[] = {
  "Position", "Geometry", "Strength", "Mallet",
  "Timbre", "Damping", "Bright.", "LFO2 Frq", "LFO2 Dep"
};
#define NUM_ELEMENTS_LFO_TARGETS 9

static const char * const s_elements_lfo2_target_names[] = {
  "Position", "Geometry", "Strength", "Mallet",
  "Timbre", "Damping", "Bright."
};
#define NUM_ELEMENTS_LFO2_TARGETS 7

#else
/* ---- Plaits LFO target names ---- */
static const char * const s_plaits_lfo_target_names[] = {
  "Shape", "ShftShp", "Param 1", "Param 2",
  "Pitch", "Amplit.", "LFO2 Frq", "LFO2 Dep"
};
#define NUM_PLAITS_LFO_TARGETS 8
#endif

__unit_callback
const char * unit_get_param_str_value(uint8_t id, int32_t value) {
#if defined(MUSSOLA_VOCAL)
  switch (id) {
    case 9: /* Model */
      if (value >= 0 && value < NUM_MUSSOLA_MODELS)
        return s_mussola_model_names[value];
      break;
    case 10: /* Gate Mode */
      if (value >= 0 && value < NUM_MUSSOLA_GATES)
        return s_mussola_gate_names[value];
      break;
    case 11: /* Voices */
      if (value >= 1 && value <= NUM_MUSSOLA_VOICES)
        return s_mussola_voices_names[value - 1];
      break;
  }
#elif defined(CLOUDS_GRANULAR)
  switch (id) {
    case 10: /* Mode */
      if (value >= 0 && value < NUM_CLOUDS_MODES)
        return s_clouds_mode_names[value];
      break;
    case 11: /* Quality */
      if (value >= 0 && value < NUM_CLOUDS_QUALITIES)
        return s_clouds_quality_names[value];
      break;
  }
#elif defined(RINGS_RESONATOR)
  switch (id) {
    case 5: /* Chord */
      if (value >= 0 && value < NUM_RINGS_CHORDS)
        return s_rings_chord_names[value];
      break;
    case 6: /* Model */
      if (value >= 0 && value < NUM_RINGS_MODELS)
        return s_rings_model_names[value];
      break;
    case 7: /* Polyphony */
      if (value >= 1 && value <= NUM_RINGS_POLY)
        return s_rings_poly_names[value - 1];
      break;
  }
#elif defined(ELEMENTS_RESONATOR_MODES)
  switch (id) {
    case 8: /* LFO Target */
      if (value >= 0 && value < NUM_ELEMENTS_LFO_TARGETS)
        return s_elements_lfo_target_names[value];
      break;
    case 9: /* LFO1 Shape */
    case 13: /* LFO2 Shape */
      if (value >= 0 && value < NUM_LFO_SHAPES)
        return s_lfo_shape_names[value];
      break;
    case 12: /* LFO2 Target */
      if (value >= 0 && value < NUM_ELEMENTS_LFO2_TARGETS)
        return s_elements_lfo2_target_names[value];
      break;
  }
#else
  switch (id) {
    case 5: /* LFO Target */
    case 9: /* LFO2 Target (same target list) */
      if (value >= 0 && value < NUM_PLAITS_LFO_TARGETS)
        return s_plaits_lfo_target_names[value];
      break;
    case 6: /* LFO1 Shape */
    case 10: /* LFO2 Shape */
      if (value >= 0 && value < NUM_LFO_SHAPES)
        return s_lfo_shape_names[value];
      break;
  }
#endif
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

/* ===========================================================================
 * Sample Access (exposed to oscillator code via drumlogue_osc_adapter.h)
 * ======================================================================== */

uint8_t osc_adapter_get_num_sample_banks(void) {
  if (!s_state.get_num_sample_banks) return 0;
  return s_state.get_num_sample_banks();
}

uint8_t osc_adapter_get_num_samples_for_bank(uint8_t bank) {
  if (!s_state.get_num_samples_for_bank) return 0;
  return s_state.get_num_samples_for_bank(bank);
}

const sample_wrapper_t* osc_adapter_get_sample(uint8_t bank, uint8_t number) {
  if (!s_state.get_sample) return nullptr;
  if (!s_state.get_num_sample_banks) return nullptr;
  if (bank >= s_state.get_num_sample_banks()) return nullptr;
  if (!s_state.get_num_samples_for_bank) return nullptr;
  if (number >= s_state.get_num_samples_for_bank(bank)) return nullptr;
  return s_state.get_sample(bank, number);
}

} /* extern "C" */
