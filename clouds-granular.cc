/*
 * Clouds granular processor port for drumlogue
 *
 * Port of Mutable Instruments Clouds (granular audio processor)
 * to the drumlogue User Oscillator API.
 *
 * Since Clouds is an audio processor (not a generator), this port
 * supports two input sources:
 *   1. A built-in sawtooth oscillator that follows MIDI pitch (default)
 *   2. Sample playback from the drumlogue sample bank
 * Select via SampleNum parameter: 0 = sawtooth, 1+ = sample from bank.
 *
 * Clouds was designed for 32 kHz; we run it at drumlogue's native
 * 48 kHz. The only audible effect is slightly different feedback
 * filter tuning (the phase vocoder's sample_rate parameter is unused).
 *
 * Output: Clouds produces stereo (L/R via ShortFrame). We interleave
 * the int16 output into the Q31 buffer as L/R pairs (same pattern
 * as Rings/Elements modal-strike).
 *
 * Modes:
 *   0 = Granular (granular playback with random/periodic grains)
 *   1 = Stretch  (WSOLA time stretching)
 *   2 = Looping Delay (pitch-shifted delay)
 *   3 = Spectral (phase vocoder)
 *
 * Quality:
 *   0 = Stereo Hi-fi (16-bit, stereo)
 *   1 = Mono Hi-fi   (16-bit, mono, more buffer time)
 *   2 = Stereo Lo-fi (8-bit mu-law, stereo, 2x downsampled)
 *   3 = Mono Lo-fi   (8-bit mu-law, mono, max buffer time)
 */

#include "userosc.h"
#include "drumlogue_osc_adapter.h"
#include "stmlib/dsp/dsp.h"

#include <cstring>
#include <cmath>

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

#include "clouds/dsp/granular_processor.h"

using namespace clouds;

/* --- Static memory allocations ---
 *
 * GranularProcessor requires two memory regions:
 *   large_buffer: 118784 bytes (sample storage + workspace in stereo mode)
 *   small_buffer: 65536 bytes  (second channel / FX workspace)
 *
 * Original Clouds on STM32F4 uses CCMRAM + SRAM for these.
 * On drumlogue's Cortex-A7 we have plenty of static RAM.
 */
static const size_t kLargeBufferSize = 118784;
static const size_t kSmallBufferSize = 65536;
static uint8_t large_buffer_[kLargeBufferSize];
static uint8_t small_buffer_[kSmallBufferSize];

static GranularProcessor processor_;

/* Built-in sawtooth oscillator state */
static float osc_phase_ = 0.0f;
static float osc_frequency_ = 440.0f;
static bool osc_active_ = false;

/* User-facing parameter storage */
uint16_t p_values[6] = {0};
float shape = 0, shiftshape = 0;
float shape_lfo = 0;

/* Custom param storage */
static int32_t pitch_semitones_ = 0;

/* Sample playback state
 *
 * Thread safety: load_sample() is called from the param/UI thread,
 * while generate_sample_input() runs on the audio thread. To avoid
 * torn reads, OSC_CYCLE snapshots the sample state into a local
 * SampleSnapshot before calling generate_sample_input(). load_sample()
 * writes sample_ptr_ last (after frames/channels) so the audio thread
 * sees either the old complete state or the new complete state.
 */
static uint8_t sample_bank_ = 0;
static uint8_t sample_number_ = 0;  /* 0 = use sawtooth, 1+ = sample */
static const float * volatile sample_ptr_ = nullptr;
static volatile size_t sample_frames_ = 0;
static volatile uint8_t sample_channels_ = 0;
static size_t sample_read_pos_ = 0;
static volatile uint16_t sample_start_permil_ = 0;     /* 0-1000 (0.0%-100.0%) */
static volatile uint16_t sample_end_permil_ = 1000;    /* 0-1000 (0.0%-100.0%) */

struct SampleSnapshot {
  const float *ptr;
  size_t frames;
  uint8_t channels;
  uint16_t start_permil;
  uint16_t end_permil;
};

/* ======================================================================
 * Audio Input Sources
 *
 * Two sources feed Clouds:
 *   1. Built-in sawtooth oscillator (follows MIDI pitch)
 *   2. Sample from drumlogue sample bank (looped playback)
 * Selected via sample_number_: 0 = sawtooth, 1+ = sample.
 * ==================================================================== */

static void load_sample(void) {
  if (sample_number_ == 0) {
    /* Nullify ptr first to signal audio thread to stop reading */
    sample_ptr_ = nullptr;
    sample_frames_ = 0;
    sample_channels_ = 0;
    return;
  }
  /* sample_number_ is 1-based; API is 0-based */
  const sample_wrapper_t *sw =
      osc_adapter_get_sample(sample_bank_, sample_number_ - 1);
  if (sw && sw->sample_ptr && sw->frames > 0) {
    /* Nullify ptr first so audio thread won't use stale frames/channels
     * with a new pointer. Then set size/channels, then ptr last. */
    sample_ptr_ = nullptr;
    sample_frames_ = sw->frames;
    sample_channels_ = sw->channels;
    /* Reset read position before exposing the new pointer */
    size_t start = (size_t)((uint64_t)sample_start_permil_ * sw->frames / 1000);
    if (start >= sw->frames) start = sw->frames - 1;
    sample_read_pos_ = start;
    /* Publish pointer last — audio thread checks ptr before reading */
    sample_ptr_ = sw->sample_ptr;
  } else {
    sample_ptr_ = nullptr;
    sample_frames_ = 0;
    sample_channels_ = 0;
    sample_read_pos_ = 0;
  }
}

/**
 * Generate sample-based input into ShortFrame buffer.
 * Uses a snapshot of sample state taken at the start of OSC_CYCLE
 * to avoid torn reads if load_sample() runs concurrently.
 * If end < start, plays backwards. Loops at region boundary.
 * Converts float [-1,+1] to int16 at 50% amplitude.
 */
static void generate_sample_input(ShortFrame *input, size_t size,
                                  const SampleSnapshot &snap) {
  size_t start_frame = (size_t)((uint64_t)snap.start_permil * snap.frames / 1000);
  size_t end_frame   = (size_t)((uint64_t)snap.end_permil * snap.frames / 1000);

  /* Clamp to valid range */
  if (start_frame >= snap.frames) start_frame = snap.frames - 1;
  if (end_frame > snap.frames) end_frame = snap.frames;

  bool reverse = (end_frame <= start_frame);

  /* Ensure read position is within the snapshot's valid range */
  if (sample_read_pos_ >= snap.frames)
    sample_read_pos_ = start_frame;

  for (size_t i = 0; i < size; ++i) {
    float left, right;
    if (snap.channels == 2) {
      left = snap.ptr[sample_read_pos_ * 2];
      right = snap.ptr[sample_read_pos_ * 2 + 1];
    } else {
      left = right = snap.ptr[sample_read_pos_];
    }
    input[i].l = (int16_t)(left * 16384.0f);
    input[i].r = (int16_t)(right * 16384.0f);

    if (reverse) {
      /* Play from start_frame down to end_frame */
      if (sample_read_pos_ == 0 || sample_read_pos_ <= end_frame)
        sample_read_pos_ = start_frame;
      else
        sample_read_pos_--;
    } else {
      /* Play from start_frame up to end_frame */
      sample_read_pos_++;
      if (sample_read_pos_ >= end_frame)
        sample_read_pos_ = start_frame;
    }
  }
}

static inline float midi_to_hz(float note) {
  /* Fast MIDI-to-Hz: 440 * 2^((note - 69) / 12) */
  return 440.0f * powf(2.0f, (note - 69.0f) / 12.0f);
}

/**
 * Generate sawtooth samples into ShortFrame input buffer (mono: L=R).
 * Amplitude is 50% (-6 dB) to avoid clipping in the feedback path.
 */
static void generate_input(ShortFrame *input, size_t size, float frequency) {
  const float phase_inc = frequency / 48000.0f;
  for (size_t i = 0; i < size; ++i) {
    float saw = osc_phase_ * 2.0f - 1.0f; /* bipolar -1..+1 */
    int16_t sample = (int16_t)(saw * 16384.0f); /* 50% amplitude */
    input[i].l = sample;
    input[i].r = sample;
    osc_phase_ += phase_inc;
    if (osc_phase_ >= 1.0f)
      osc_phase_ -= 1.0f;
  }
}

/* ======================================================================
 * OSC API Implementation
 * ==================================================================== */

void OSC_INIT(uint32_t platform, uint32_t api)
{
  (void)platform;
  (void)api;

  memset(large_buffer_, 0, kLargeBufferSize);
  memset(small_buffer_, 0, kSmallBufferSize);

  processor_.Init(large_buffer_, kLargeBufferSize,
                  small_buffer_, kSmallBufferSize);
  processor_.set_playback_mode(PLAYBACK_MODE_GRANULAR);
  processor_.set_quality(0); /* Stereo, high fidelity */
  processor_.set_bypass(false);
  processor_.set_silence(false);

  /* Set initial parameters */
  Parameters *params = processor_.mutable_parameters();
  params->position = 0.5f;
  params->size = 0.5f;
  params->pitch = 0.0f;
  params->density = 0.5f;
  params->texture = 0.5f;
  params->dry_wet = 1.0f; /* fully wet by default */
  params->stereo_spread = 0.5f;
  params->feedback = 0.0f;
  params->reverb = 0.0f;
  params->freeze = false;
  params->trigger = false;
  params->gate = false;

  osc_phase_ = 0.0f;
  osc_frequency_ = midi_to_hz(60.0f);
  osc_active_ = false;
  pitch_semitones_ = 0;

  sample_bank_ = 0;
  sample_number_ = 0;
  sample_ptr_ = nullptr;
  sample_frames_ = 0;
  sample_channels_ = 0;
  sample_read_pos_ = 0;
  sample_start_permil_ = 0;
  sample_end_permil_ = 1000;
}

void OSC_CYCLE(const user_osc_param_t *const params,
               int32_t *yn, const uint32_t frames)
{
  (void)frames;

  shape_lfo = q31_to_f32(params->shape_lfo);

  /* Pitch from adapter (note.fraction encoding) */
  float note =
      ((float)(params->pitch >> 8)) +
      ((params->pitch & 0xFF) * k_note_mod_fscale);
  osc_frequency_ = midi_to_hz(note);

  /* Update Clouds parameters from stored param values */
  Parameters *p = processor_.mutable_parameters();
  p->position = clip01f(shape);                              /* id 1 */
  p->size = clip01f(shiftshape);                             /* id 2 */
  p->density = clip01f(p_values[k_user_osc_param_id1] * 0.01f);  /* id 3 */
  p->texture = clip01f(p_values[k_user_osc_param_id2] * 0.01f);  /* id 4 */
  p->pitch = (float)pitch_semitones_;                        /* id 5 */
  p->feedback = clip01f(p_values[k_user_osc_param_id4] * 0.01f); /* id 6 */
  p->dry_wet = clip01f(p_values[k_user_osc_param_id5] * 0.01f);  /* id 7 */
  p->reverb = clip01f(p_values[k_user_osc_param_id6] * 0.01f);   /* id 8 */
  p->gate = osc_active_;

  /* Prepare handles mode/quality switches and buffer resets */
  processor_.Prepare();

  /* Snapshot sample state for thread-safe access during this block */
  SampleSnapshot snap;
  snap.ptr = sample_ptr_;
  snap.frames = sample_frames_;
  snap.channels = sample_channels_;
  snap.start_permil = sample_start_permil_;
  snap.end_permil = sample_end_permil_;

  /* Generate input audio */
  ShortFrame input[kMaxBlockSize];
  ShortFrame output[kMaxBlockSize];

  if (osc_active_) {
    if (snap.ptr && snap.frames > 0) {
      generate_sample_input(input, kMaxBlockSize, snap);
    } else {
      generate_input(input, kMaxBlockSize, osc_frequency_);
    }
  } else {
    memset(input, 0, sizeof(ShortFrame) * kMaxBlockSize);
  }

  /* Process through Clouds granular engine */
  processor_.Process(input, output, kMaxBlockSize);

  /* Convert stereo int16 output to interleaved Q31 */
#ifdef __ARM_NEON
  {
    size_t i = 0;
    for (; i + 4 <= kMaxBlockSize; i += 4) {
      /* Load 4 pairs of int16 L/R values */
      int16_t lvals[4] = {output[i].l, output[i + 1].l,
                          output[i + 2].l, output[i + 3].l};
      int16_t rvals[4] = {output[i].r, output[i + 1].r,
                          output[i + 2].r, output[i + 3].r};
      /* int16 -> int32 (sign-extend) then shift left 16 bits for Q31 */
      int32x4_t ql = vshlq_n_s32(vmovl_s16(vld1_s16(lvals)), 16);
      int32x4_t qr = vshlq_n_s32(vmovl_s16(vld1_s16(rvals)), 16);
      /* Interleave [L0,R0,L1,R1,L2,R2,L3,R3] */
      int32x4x2_t stereo;
      stereo.val[0] = ql;
      stereo.val[1] = qr;
      vst2q_s32(yn + i * 2, stereo);
    }
    for (; i < kMaxBlockSize; ++i) {
      yn[i * 2] = ((int32_t)output[i].l) << 16;
      yn[i * 2 + 1] = ((int32_t)output[i].r) << 16;
    }
  }
#else
  for (size_t i = 0; i < kMaxBlockSize; ++i) {
    yn[i * 2] = ((int32_t)output[i].l) << 16;
    yn[i * 2 + 1] = ((int32_t)output[i].r) << 16;
  }
#endif
}

void OSC_NOTEON(const user_osc_param_t *const params)
{
  (void)params;
  osc_active_ = true;
  /* Reset sample playback to start point */
  {
    const float *ptr = sample_ptr_;
    size_t frames = sample_frames_;
    if (ptr && frames > 0) {
      size_t start = (size_t)((uint64_t)sample_start_permil_ * frames / 1000);
      if (start >= frames) start = frames - 1;
      sample_read_pos_ = start;
    } else {
      sample_read_pos_ = 0;
    }
  }
  processor_.mutable_parameters()->gate = true;
}

void OSC_NOTEOFF(const user_osc_param_t *const params)
{
  (void)params;
  osc_active_ = false;
  processor_.mutable_parameters()->gate = false;
}

void OSC_PARAM(uint16_t index, uint16_t value)
{
  switch (index) {
    case k_user_osc_param_id1: /* Density */
    case k_user_osc_param_id2: /* Texture */
    case k_user_osc_param_id4: /* Feedback */
    case k_user_osc_param_id5: /* Dry/Wet */
    case k_user_osc_param_id6: /* Reverb */
      p_values[index] = value;
      break;

    case k_user_osc_param_id3: /* Pitch (semitones, signed via uint16 cast) */
      pitch_semitones_ = (int32_t)(int16_t)value;
      break;

    case k_user_osc_param_shape: /* Position */
      shape = param_val_to_f32(value);
      break;

    case k_user_osc_param_shiftshape: /* Size */
      shiftshape = param_val_to_f32(value);
      break;

    /* Custom params passed by the wrapper */
    case 8: /* Freeze (0 = off, 1 = on) */
      processor_.set_freeze(value != 0);
      break;

    case 9: /* Mode (0-3: Granular/Stretch/Delay/Spectral) */
      if (value < PLAYBACK_MODE_LAST)
        processor_.set_playback_mode(static_cast<PlaybackMode>(value));
      break;

    case 10: /* Quality (0-3: StHi/MoHi/StLo/MoLo) */
      processor_.set_quality(value & 0x3);
      break;

    case 11: /* SampleBank (0-15) */
      sample_bank_ = (uint8_t)(value & 0xF);
      load_sample();
      break;

    case 12: /* SampleNum (0=sawtooth, 1+=sample from bank) */
      sample_number_ = (uint8_t)value;
      load_sample();
      break;

    case 13: /* SmplStart (0-1000 permil) */
      sample_start_permil_ = (value > 1000) ? 1000 : (uint16_t)value;
      break;

    case 14: /* SmplEnd (0-1000 permil) */
      sample_end_permil_ = (value > 1000) ? 1000 : (uint16_t)value;
      break;

    default:
      break;
  }
}
