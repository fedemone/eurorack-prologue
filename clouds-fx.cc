/*
 * CloudsFX — Clouds granular engine as a drumlogue delay/insert effect
 *
 * This is the FX-bus counterpart to clouds-granular.cc.  Where the synth
 * port had to invent an input (an internal sawtooth or a loaded sample),
 * CloudsFX granulates the *incoming audio* delivered on the drumlogue FX
 * bus.  All the sample-source machinery (sawtooth, sample bank, start/end)
 * is therefore gone; everything else — Position, Size, Density, Texture,
 * Pitch, Feedback, Dry/Wet, Reverb, Freeze, Mode, Quality — is the same
 * Clouds engine, exposed as an effect.
 *
 * This file is compiled ONLY for the CloudsFX (delfx) build and is never
 * linked together with clouds-granular.cc, so the two translation units
 * are free to each keep their own static engine/buffers.
 *
 * I/O: stereo float in -> stereo float out (interleaved L/R), matching the
 * drumlogue delfx render contract.  The engine works in blocks of at most
 * clouds::kMaxBlockSize (32) frames, so clouds_fx_process() chunks to that.
 */

#include "clouds/dsp/granular_processor.h"
#include "stmlib/dsp/dsp.h"

#include <cstdint>
#include <cstring>

using namespace clouds;

/* --- Static memory allocations (see clouds-granular.cc for sizing notes) --- */
static const size_t kLargeBufferSize = 118784;
static const size_t kSmallBufferSize = 65536;
alignas(16) static uint8_t large_buffer_[kLargeBufferSize];
alignas(16) static uint8_t small_buffer_[kSmallBufferSize];

static GranularProcessor processor_;
static int32_t pitch_semitones_ = 0;

/* Input trim and output gain.
 *
 * The FX bus carries a full-scale-ish stereo signal.  Clouds sums many
 * overlapping grains internally, so we leave a little input headroom
 * (kInScale below 0 dBFS) to keep the wet path clear of the int16 ceiling,
 * and a matching output trim.  At Dry/Wet = 0 % the engine passes the input
 * through, so dry level ≈ kInScale/32768 * kOutGain.  These are conservative,
 * clip-safe defaults; final levels are a hardware-tuning item. */
static const float kInScale  = 29490.0f;  /* ~0.9 full scale into int16 */
static const float kOutGain  = 0.75f;

/* Clamp a 0-1 engine parameter to just below 1.0.
 *
 * Clouds uses several 0-1 parameters (dry_wet, size, texture, ...) as LUT
 * indices via stmlib::Interpolate(table, value, size), which also reads
 * table[value*size + 1] — one element PAST the table when value is exactly
 * 1.0.  Harmless on the original bare-metal hardware, but on drumlogue the
 * unit runs in a memory-protected process and the out-of-bounds read can
 * fault (an audio crash the first time the dry/wet ramp hits 1.0).
 * Clamping to 0.9995 is inaudible and keeps every LUT access in bounds
 * without touching the eurorack submodule. */
static inline float clamp01(float x) {
  return (x < 0.0f) ? 0.0f : ((x > 0.9995f) ? 0.9995f : x);
}

/* Mode / Quality / Freeze are latched here and applied on the audio thread.
 *
 * set_playback_mode() and set_quality() change num_channels_ and
 * low_fidelity_, which decide whether Process() reads the 16-bit or the
 * 8-bit audio buffers; the matching buffers are only (re)initialized by the
 * next Prepare().  clouds_fx_set_param() runs on the drumlogue's UI thread,
 * so calling those setters directly would let the buffer layout change while
 * the audio thread is inside Process(), reading through a pointer that was
 * never initialized for that resolution.  Latching the request and applying
 * it in clouds_fx_process() keeps all reconfiguration on the audio thread. */
static volatile int32_t pending_mode_ = 0;
static volatile int32_t pending_quality_ = 0;
static volatile int32_t pending_freeze_ = 0;

static inline int16_t f32_to_s16(float x) {
  float s = x * kInScale;
  if (s >  32767.0f) s =  32767.0f;
  if (s < -32768.0f) s = -32768.0f;
  return (int16_t)s;
}

static inline float s16_to_f32(int16_t x) {
  float y = (float)x * (kOutGain / 32768.0f);
  if (y >  1.0f) y =  1.0f;
  if (y < -1.0f) y = -1.0f;
  return y;
}

/* ======================================================================
 * FX entry points (called by drumlogue_delfx_wrapper.cc)
 * ==================================================================== */

extern "C" void clouds_fx_init(void) {
  memset(large_buffer_, 0, kLargeBufferSize);
  memset(small_buffer_, 0, kSmallBufferSize);

  processor_.Init(large_buffer_, kLargeBufferSize,
                  small_buffer_, kSmallBufferSize);
  processor_.set_playback_mode(PLAYBACK_MODE_GRANULAR);
  processor_.set_quality(0); /* Stereo, high fidelity */
  processor_.set_bypass(false);
  processor_.set_silence(false);

  Parameters *p = processor_.mutable_parameters();
  p->position = 0.5f;
  p->size = 0.5f;
  p->pitch = 0.0f;
  p->density = 0.5f;
  p->texture = 0.5f;
  p->dry_wet = 0.5f;        /* 50 % wet: sensible default for an insert FX */
  p->stereo_spread = 0.5f;
  p->feedback = 0.0f;
  p->reverb = 0.0f;
  p->freeze = false;
  p->trigger = false;
  p->gate = false;

  pending_mode_ = PLAYBACK_MODE_GRANULAR;
  pending_quality_ = 0;
  pending_freeze_ = 0;

  /* Run the first Prepare() here rather than letting it land on the first
   * audio block: it is the call that allocates and zeroes the ~190 KB of
   * sample/FX buffers, which has no business happening under an audio
   * deadline.  Afterwards Prepare() is cheap in Granular mode. */
  processor_.Prepare();

  pitch_semitones_ = 0;
}

/* id is the drumlogue CloudsFX param id (see header.c CLOUDS_FX block). */
extern "C" void clouds_fx_set_param(uint8_t id, int32_t value) {
  Parameters *p = processor_.mutable_parameters();
  switch (id) {
    case 0: /* Position: 0-100 % */
      p->position = clamp01(value * 0.01f);
      break;
    case 1: /* Size: 0-100 % */
      p->size = clamp01(value * 0.01f);
      break;
    case 2: /* Density: 0-100 % */
      p->density = clamp01(value * 0.01f);
      break;
    case 3: /* Texture: 0-100 % */
      p->texture = clamp01(value * 0.01f);
      break;
    case 4: /* Pitch: 0-48, centered at 24 = 0 semitones */
      pitch_semitones_ = value - 24;
      p->pitch = (float)pitch_semitones_;
      break;
    case 5: /* Feedback: 0-100 % */
      p->feedback = clamp01(value * 0.01f);
      break;
    case 6: /* Dry/Wet: 0-100 % */
      p->dry_wet = clamp01(value * 0.01f);
      break;
    case 7: /* Reverb: 0-100 % */
      p->reverb = clamp01(value * 0.01f);
      break;
    /* These three reconfigure the engine, so they are only latched here and
     * applied by clouds_fx_process() on the audio thread (see pending_*). */
    case 8: /* Freeze: 0/1 */
      pending_freeze_ = (value != 0) ? 1 : 0;
      break;
    case 9: /* Mode: 0-3 (Granular/Stretch/Delay/Spectral) */
      if (value >= 0 && value < PLAYBACK_MODE_LAST)
        pending_mode_ = value;
      break;
    case 10: /* Quality: 0-3 (StHi/MoHi/StLo/MoLo) */
      pending_quality_ = value & 0x3;
      break;
    default:
      break;
  }
}

/* Process interleaved stereo float in -> interleaved stereo float out. */
extern "C" void clouds_fx_process(const float *in, float *out,
                                  uint32_t frames) {
  Parameters *p = processor_.mutable_parameters();
  /* As an insert FX the engine is always "sounding". */
  p->gate = true;

  /* Apply latched engine reconfiguration on this (audio) thread, so the
   * buffer layout can never change underneath Process(). */
  if ((int32_t)processor_.playback_mode() != pending_mode_)
    processor_.set_playback_mode(static_cast<PlaybackMode>(pending_mode_));
  if (processor_.quality() != pending_quality_)
    processor_.set_quality(pending_quality_);
  processor_.set_freeze(pending_freeze_ != 0);

  ShortFrame input[kMaxBlockSize];
  ShortFrame output[kMaxBlockSize];

  uint32_t done = 0;
  while (done < frames) {
    uint32_t n = frames - done;
    if (n > kMaxBlockSize) n = kMaxBlockSize;

    /* Seed a grain each block in Granular mode so the texture stays smooth
     * even when the incoming audio is steady (mirrors the synth port). The
     * other modes drive themselves from the input and ignore this. */
    p->trigger = (processor_.playback_mode() == PLAYBACK_MODE_GRANULAR);

    /* Prepare handles mode/quality switches and buffer resets. */
    processor_.Prepare();

    for (uint32_t i = 0; i < n; ++i) {
      input[i].l = f32_to_s16(in[(done + i) * 2]);
      input[i].r = f32_to_s16(in[(done + i) * 2 + 1]);
    }

    processor_.Process(input, output, n);

    for (uint32_t i = 0; i < n; ++i) {
      out[(done + i) * 2]     = s16_to_f32(output[i].l);
      out[(done + i) * 2 + 1] = s16_to_f32(output[i].r);
    }

    done += n;
  }
}

/* Test/inspection hooks (host builds only). */
#ifdef CLOUDS_FX_TEST
extern "C" {
int clouds_fx_test_mode(void) {
  return (int)processor_.playback_mode();
}
float clouds_fx_test_pitch(void) { return (float)pitch_semitones_; }
}
#endif
