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
 * The engine runs at its native 32 kHz; the drumlogue's 48 kHz is converted
 * at the boundary (clouds_src.h), in both directions.  See the SRC section
 * below for why the dry signal goes through the conversion too rather than
 * bypassing it.
 *
 * I/O: stereo float in -> stereo float out (interleaved L/R), matching the
 * drumlogue delfx render contract.
 */

#include "clouds/dsp/granular_processor.h"
#include "clouds_src.h"
#include "stmlib/dsp/dsp.h"

/* The optimized engine fork must actually be on the include path.
 *
 * eurorack-opt/ shadows two submodule headers, and both change the size of
 * GranularProcessor.  A build that compiles the forked granular_processor.cc
 * but lets some translation unit see the submodule's headers -- or the other
 * way round -- links without complaint and then corrupts memory at run time,
 * which is not a failure anyone should have to debug.  So the build declares
 * its intent with CLOUDS_OPT_ENGINE and this checks the headers agree. */
#if defined(CLOUDS_OPT_ENGINE) && !defined(CLOUDS_OPT_ACTIVE)
#error "CLOUDS_OPT_ENGINE is set but eurorack-opt/ is not ahead of eurorack/ on the include path -- see eurorack-opt/README.md"
#endif


#include <cstdint>
#include <cstring>
#include <cmath>
#include <time.h>   /* nanosleep, for the reconfiguration handshake */

using namespace clouds;

/* --- Static memory allocations (see clouds-granular.cc for sizing notes) --- */
static const size_t kLargeBufferSize = 118784;
static const size_t kSmallBufferSize = 65536;
alignas(16) static uint8_t large_buffer_[kLargeBufferSize];
alignas(16) static uint8_t small_buffer_[kSmallBufferSize];

static GranularProcessor processor_;

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

/* Density: map the knob onto Clouds' grain scheduler, linearly in grains.
 *
 * Clouds' DENSITY is bipolar around 0.5 and schedules no grains at all
 * between 0.47 and 0.53; above that, target_num_grains is max_num_grains *
 * ((density - 0.53) * 2.12)^3.  Mapping the knob straight onto density
 * inherits that cube, which puts almost all of the grains — and almost all
 * of the CPU — in the top fifth of the travel.  Inverting the cube spreads
 * them evenly, and capping at kGrainsMax keeps the whole range payable; the
 * cap is a fraction of the allocated pool, so it follows Quality.  There is
 * a longer note on the measurements behind this in clouds-granular.cc.
 *
 * This replaces setting parameters.trigger on every block, which seeded a
 * grain every 32 samples — 1500 grains/second, from the moment the unit
 * loaded.  That pinned the grain pool at maximum whatever DENSITY said, put a
 * block-rate buzz on the wet signal, and held the engine at worst-case CPU
 * for as long as the effect was inserted. */
static const float kGrainsMin = 0.032f; /* ~1 grain: sparse but never silent */
static const float kGrainsMax = 0.57f;  /* ~18 of 32: dense, and affordable  */

static inline float density_to_clouds(float knob01) {
  const float k = clamp01(knob01);
  /* target_num_grains = max_num_grains * overlap^3, so a knob linear in
   * grains needs the cube root.  Control thread only. */
  const float grains = kGrainsMin + k * (kGrainsMax - kGrainsMin);
  return 0.53f + cbrtf(grains) * (1.0f / 2.12f);
}

/* ======================================================================
 * Parameter storage
 *
 * Knob values live here: written by the control thread, read by the audio
 * thread when it assembles an engine block.  Nothing outside this file's
 * audio path writes into processor_'s Parameters, so a knob move can never
 * land in the middle of a Process() call.  Each is a single aligned word,
 * and a block rendered with the previous value is inaudible.
 * ==================================================================== */

static volatile float knob_position_ = 0.5f;
static volatile float knob_size_ = 0.5f;
static volatile float knob_density_ = 0.0f;   /* already mapped */
static volatile float knob_texture_ = 0.5f;
static volatile float knob_pitch_ = 0.0f;     /* semitones */
static volatile float knob_feedback_ = 0.0f;
static volatile float knob_dry_wet_ = 0.5f;   /* 50 % wet: sensible for an insert */
static volatile float knob_reverb_ = 0.0f;
static volatile int32_t knob_freeze_ = 0;

/* ======================================================================
 * Engine reconfiguration handshake
 *
 * Mode, Quality and unit_reset() all force GranularProcessor::Prepare() down
 * its reallocating path: FxEngine::Init() clears the 32 KB reverb, the 8 KB
 * diffuser and the 8 KB pitch shifter, and AudioBuffer::Init() std::fill()s
 * the sample buffers — around 180 KB of stores, plus the pointer re-seating
 * that Process() reads through.
 *
 * Neither thread can do that alone.  Running it straight from the control
 * callback rewrites the buffers unit_render() is reading, which was a
 * verified crash.  Deferring it to the audio thread — which is what this
 * file used to do — keeps the pointers consistent but drops 180 KB of memory
 * traffic into a single audio callback, on a bus that is already rendering a
 * full kit.
 *
 * So: the control thread asks the audio thread to stand down, waits for it
 * to acknowledge, does the work itself, and hands the engine back.  One word
 * carries the whole protocol.
 *
 *   kRunning  the audio thread owns the engine
 *   kParkReq  the control thread wants it
 *   kParked   the audio thread has stood down; it will not touch the engine
 *             again until the state is kRunning
 *
 * The audio thread only ever writes kParked, and only after reading a
 * non-kRunning state — and having written it, that call returns without
 * going near the engine.  So once the control thread observes kParked, every
 * render from that point on is on the silent path and the engine is
 * exclusively its own.  A render that read kRunning just before the request
 * landed completes normally and parks on its next call, which costs the
 * control thread one extra block of waiting.
 *
 * The wait is bounded, because the audio thread may not be running at all:
 * the unit can be suspended, or not yet rendering, or a host could call
 * unit_reset() from the render thread itself.  render_count_ separates those
 * cases — if it has not moved for the whole timeout then no callback
 * happened, and reconfiguring is safe whatever the park state says.
 * ==================================================================== */

enum {
  kRunning = 0,
  kParkReq = 1,
  kParked = 2,
};

static volatile int32_t engine_state_ = kRunning;
static volatile uint32_t render_count_ = 0;

/* Latched engine configuration, applied by the control thread under the park. */
static volatile int32_t pending_mode_ = 0;
static volatile int32_t pending_quality_ = 0;

/* CONTROL THREAD ONLY.  While the unit is rendering, the acknowledgement
 * arrives within one audio block — well under a millisecond at any drumlogue
 * buffer size — so this normally blocks for one poll interval.
 *
 * Two ceilings bound the pathological cases.  kIdleTimeoutNs decides "the
 * audio thread is not running": if render_count_ has not moved at all in that
 * time, no callback happened, and the engine is ours whatever the park state
 * says.  kParkTimeoutNs is the hard ceiling for a renderer that is running
 * but somehow never parks; reconfiguring under that would be exactly the race
 * this exists to prevent, so the reconfiguration is abandoned instead.
 * pending_mode_ / pending_quality_ stay latched, so the next successful park
 * picks up whatever was missed. */
static const long kParkPollNs = 200000L;      /* 0.2 ms between polls */
static const long kIdleTimeoutNs = 10000000L; /* 10 ms with no render at all */
static const long kParkTimeoutNs = 50000000L; /* 50 ms hard ceiling */

static void with_engine_parked(void (*fn)(void)) {
  const uint32_t seen = __atomic_load_n(&render_count_, __ATOMIC_ACQUIRE);
  __atomic_store_n(&engine_state_, kParkReq, __ATOMIC_RELEASE);

  long waited = 0;
  bool rendered = false;
  while (__atomic_load_n(&engine_state_, __ATOMIC_ACQUIRE) != kParked) {
    if (__atomic_load_n(&render_count_, __ATOMIC_ACQUIRE) != seen)
      rendered = true;
    if (!rendered && waited >= kIdleTimeoutNs)
      break;                    /* nothing is rendering; take the engine */
    if (waited >= kParkTimeoutNs) {
      /* Renders are happening but the park never landed.  Leave the engine
       * alone.  Store kRunning with a compare-exchange from kParkReq so a
       * park that lands right now is not silently overwritten — if it wins,
       * the audio thread stays on the silent path for one more block and
       * then finds kRunning. */
      int32_t expect = kParkReq;
      __atomic_compare_exchange_n(&engine_state_, &expect, kRunning, false,
                                  __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
      if (expect == kParked)
        break;                  /* it parked after all — carry on */
      return;
    }
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = kParkPollNs;
    nanosleep(&ts, nullptr);
    waited += kParkPollNs;
  }

  fn();

  __atomic_store_n(&engine_state_, kRunning, __ATOMIC_RELEASE);
}

/* ======================================================================
 * 48 kHz <-> 32 kHz plumbing
 *
 * Both directions are converted, dry included.  Keeping the dry signal at
 * 48 kHz and mixing it outside the engine would preserve its top octave, but
 * it would also put it ~1.2 ms ahead of the wet path, and a delayed copy
 * summed with an undelayed one is a comb filter — far more audible than the
 * rolloff above 13 kHz that the conversion costs.  Clouds' own codec band
 * limits its dry path the same way, so this is also the more faithful of the
 * two options.
 *
 * The render is pull-driven, like the synth's: ask the upsamplers how many
 * 32 kHz samples the next output chunk needs, and run engine blocks until the
 * FIFO holds that many.  Unlike the synth, the input is not something we can
 * generate on demand — it arrives in whatever chunk size the host uses — so
 * incoming audio is buffered, and the pipeline is primed with silence to
 * cover the mismatch between "48 kHz samples that have arrived" and "48 kHz
 * samples this engine block wants".
 * ==================================================================== */

static clouds_src::SrcDown src_down_l_, src_down_r_;
static clouds_src::SrcUp src_up_l_, src_up_r_;

/* Incoming 48 kHz audio waiting to be decimated. */
static const int kInFifoSize = 256;
static float in_l_[kInFifoSize], in_r_[kInFifoSize];
static int in_avail_ = 0;

/* Priming: an engine block asks for 48 samples at 48 kHz, and a host buffer
 * may deliver fewer than that before one falls due.  The rates are exactly
 * rational so the deficit is deterministic: simulating the counters over
 * buffer sizes from 16 to 128 frames, 32 samples of cushion is the exact
 * minimum and anything less underruns at every size.  48 keeps 16 samples of
 * margin for 1 ms of latency; the total added by this file is that plus the
 * two conversions' group delay, about 2.2 ms. */
static const int kInPrime = 48;

/* Engine output at 32 kHz waiting to be interpolated up. */
static const int kEngFifoSize = 128;
static float eng_l_[kEngFifoSize], eng_r_[kEngFifoSize];
static int eng_avail_ = 0;

/* Largest output chunk handled in one pass; bounds every buffer above. */
static const int kChunk = 64;

static inline int16_t f32_to_s16(float x) {
  float s = x * kInScale;
  if (s >  32767.0f) s =  32767.0f;
  if (s < -32768.0f) s = -32768.0f;
  return (int16_t)s;
}

static inline float s16_to_f32(float x) {
  float y = x * (kOutGain / 32768.0f);
  if (y >  1.0f) y =  1.0f;
  if (y < -1.0f) y = -1.0f;
  return y;
}

static void src_reset(void) {
  src_down_l_.Init();
  src_down_r_.Init();
  src_up_l_.Init();
  src_up_r_.Init();
  memset(in_l_, 0, sizeof(in_l_));
  memset(in_r_, 0, sizeof(in_r_));
  memset(eng_l_, 0, sizeof(eng_l_));
  memset(eng_r_, 0, sizeof(eng_r_));
  in_avail_ = kInPrime;
  eng_avail_ = 0;
}

/* Render one 32-frame engine block at 32 kHz into the output FIFO.
 * AUDIO THREAD ONLY, and only while the engine state is kRunning. */
static void engine_block(void) {
  const int need48 = src_down_l_.InputNeeded((int)kMaxBlockSize);

  /* Underrun is not expected after priming, but pad rather than read stale
   * samples if a host uses a buffer size that outruns the cushion. */
  if (in_avail_ < need48) {
    const int pad = need48 - in_avail_;
    memset(&in_l_[in_avail_], 0, (size_t)pad * sizeof(float));
    memset(&in_r_[in_avail_], 0, (size_t)pad * sizeof(float));
    in_avail_ = need48;
  }

  memcpy(src_down_l_.Input(), in_l_, (size_t)need48 * sizeof(float));
  memcpy(src_down_r_.Input(), in_r_, (size_t)need48 * sizeof(float));
  in_avail_ -= need48;
  memmove(in_l_, in_l_ + need48, (size_t)in_avail_ * sizeof(float));
  memmove(in_r_, in_r_ + need48, (size_t)in_avail_ * sizeof(float));

  float dl[kMaxBlockSize], dr[kMaxBlockSize];
  src_down_l_.Process(dl, (int)kMaxBlockSize, 1);
  src_down_r_.Process(dr, (int)kMaxBlockSize, 1);

  ShortFrame input[kMaxBlockSize];
  ShortFrame output[kMaxBlockSize];
  for (size_t i = 0; i < kMaxBlockSize; ++i) {
    input[i].l = f32_to_s16(dl[i]);
    input[i].r = f32_to_s16(dr[i]);
  }

  Parameters *p = processor_.mutable_parameters();
  p->position = knob_position_;
  p->size = knob_size_;
  p->density = knob_density_;
  p->texture = knob_texture_;
  p->pitch = knob_pitch_;
  p->feedback = knob_feedback_;
  p->dry_wet = knob_dry_wet_;
  p->reverb = knob_reverb_;
  p->stereo_spread = 0.5f;
  /* As an insert FX the engine is always "sounding", and there is no
   * external trigger on the FX bus: the grain clock comes from DENSITY. */
  p->gate = true;
  p->trigger = false;
  processor_.set_freeze(knob_freeze_ != 0);

  processor_.Prepare();
  processor_.Process(input, output, kMaxBlockSize);

  for (size_t i = 0; i < kMaxBlockSize; ++i) {
    eng_l_[eng_avail_ + (int)i] = (float)output[i].l;
    eng_r_[eng_avail_ + (int)i] = (float)output[i].r;
  }
  eng_avail_ += (int)kMaxBlockSize;
}

/* ======================================================================
 * FX entry points (called by drumlogue_delfx_wrapper.cc)
 * ==================================================================== */

/* Bring the engine up from scratch, keeping the current parameter values.
 *
 * GranularProcessor::Init() re-seats the buffer pointers and sets
 * reset_buffers_, and the Prepare() below then re-allocates the FX workspace
 * and re-Init()s the audio buffers — AudioBuffer::Init() zero-fills them, so
 * no explicit memset is needed here.
 *
 * This is the ~180 KB call.  Every caller must reach it either through
 * with_engine_parked() or before the unit is rendering at all. */
static void engine_reconfigure(void) {
  processor_.Init(large_buffer_, kLargeBufferSize,
                  small_buffer_, kSmallBufferSize);
  processor_.set_playback_mode(static_cast<PlaybackMode>(pending_mode_));
  processor_.set_quality(pending_quality_);
  processor_.set_bypass(false);
  processor_.set_silence(false);
  processor_.set_freeze(knob_freeze_ != 0);

  Parameters *p = processor_.mutable_parameters();
  p->trigger = false;
  p->gate = false;

  /* Run Prepare() here rather than letting it land on the first audio block:
   * it is the call that allocates and zeroes the sample/FX buffers.
   * Afterwards Prepare() is cheap in Granular mode. */
  processor_.Prepare();

  src_reset();
}

extern "C" void clouds_fx_init(void) {
  memset(large_buffer_, 0, kLargeBufferSize);
  memset(small_buffer_, 0, kSmallBufferSize);

  knob_position_ = 0.5f;
  knob_size_ = 0.5f;
  knob_density_ = density_to_clouds(0.5f);
  knob_texture_ = 0.5f;
  knob_pitch_ = 0.0f;
  knob_feedback_ = 0.0f;
  knob_dry_wet_ = 0.5f;
  knob_reverb_ = 0.0f;
  knob_freeze_ = 0;

  pending_mode_ = PLAYBACK_MODE_GRANULAR;
  pending_quality_ = 0;   /* Stereo, high fidelity */

  engine_state_ = kRunning;
  render_count_ = 0;

  /* Nothing is rendering yet, so this one needs no handshake. */
  engine_reconfigure();
}

/* Called from unit_reset() on the drumlogue's control thread. */
extern "C" void clouds_fx_request_reset(void) {
  with_engine_parked(engine_reconfigure);
}

/* id is the drumlogue CloudsFX param id (see header.c CLOUDS_FX block). */
extern "C" void clouds_fx_set_param(uint8_t id, int32_t value) {
  switch (id) {
    case 0: /* Position: 0-100 % */
      knob_position_ = clamp01(value * 0.01f);
      break;
    case 1: /* Size: 0-100 % */
      knob_size_ = clamp01(value * 0.01f);
      break;
    case 2: /* Density: 0-100 % */
      knob_density_ = density_to_clouds(value * 0.01f);
      break;
    case 3: /* Texture: 0-100 % */
      knob_texture_ = clamp01(value * 0.01f);
      break;
    case 4: /* Pitch: 0-48, centered at 24 = 0 semitones */
      knob_pitch_ = (float)(value - 24);
      break;
    case 5: /* Feedback: 0-100 % */
      knob_feedback_ = clamp01(value * 0.01f);
      break;
    case 6: /* Dry/Wet: 0-100 % */
      knob_dry_wet_ = clamp01(value * 0.01f);
      break;
    case 7: /* Reverb: 0-100 % */
      knob_reverb_ = clamp01(value * 0.01f);
      break;
    case 8: /* Freeze: 0/1 — cheap, no reallocation */
      knob_freeze_ = (value != 0) ? 1 : 0;
      break;
    /* Mode and Quality both push Prepare() down its reallocating path, so
     * they are applied here, on the control thread, with the renderer
     * parked — never from inside an audio callback. */
    case 9: /* Mode: 0-3 (Granular/Stretch/Delay/Spectral) */
      if (value >= 0 && value < PLAYBACK_MODE_LAST && value != pending_mode_) {
        pending_mode_ = value;
        with_engine_parked(engine_reconfigure);
      }
      break;
    case 10: /* Quality: 0-3 (StHi/MoHi/StLo/MoLo) */
      if ((value & 0x3) != pending_quality_) {
        pending_quality_ = value & 0x3;
        with_engine_parked(engine_reconfigure);
      }
      break;
    default:
      break;
  }
}

/* Process interleaved stereo float in -> interleaved stereo float out. */
extern "C" void clouds_fx_process(const float *in, float *out,
                                  uint32_t frames) {
  __atomic_fetch_add(&render_count_, 1u, __ATOMIC_ACQ_REL);

  if (__atomic_load_n(&engine_state_, __ATOMIC_ACQUIRE) != kRunning) {
    /* Stand down and say so.  The engine belongs to the control thread until
     * it hands it back; output silence rather than dry, because the level a
     * dry passthrough would need depends on engine state we must not read.
     * A reconfiguration lasts a block or two.
     *
     * kParkReq -> kParked only, so that a control thread which has already
     * given up and restored kRunning cannot be pushed back into a park this
     * call would never leave. */
    int32_t expect = kParkReq;
    __atomic_compare_exchange_n(&engine_state_, &expect, kParked, false,
                                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    memset(out, 0, (size_t)frames * 2 * sizeof(float));
    return;
  }

  uint32_t done = 0;
  while (done < frames) {
    int n = (int)(frames - done);
    if (n > kChunk) n = kChunk;

    /* Buffer this chunk of incoming audio.  in_avail_ tracks the priming
     * cushion and cannot drift far from it — the engine consumes 48 kHz
     * samples at exactly the rate they arrive — but drop the oldest rather
     * than run off the end if a host ever breaks that assumption. */
    if (in_avail_ > kInFifoSize - kChunk) {
      const int drop = in_avail_ - (kInFifoSize - kChunk);
      memmove(in_l_, in_l_ + drop, (size_t)(in_avail_ - drop) * sizeof(float));
      memmove(in_r_, in_r_ + drop, (size_t)(in_avail_ - drop) * sizeof(float));
      in_avail_ -= drop;
    }

    const float *src = in + (size_t)done * 2;
    for (int i = 0; i < n; ++i) {
      in_l_[in_avail_ + i] = src[i * 2];
      in_r_[in_avail_ + i] = src[i * 2 + 1];
    }
    in_avail_ += n;

    /* Run only the engine blocks this chunk actually consumes. */
    const int need32 = src_up_l_.InputNeeded(n);
    while (eng_avail_ < need32)
      engine_block();

    memcpy(src_up_l_.Input(), eng_l_, (size_t)need32 * sizeof(float));
    memcpy(src_up_r_.Input(), eng_r_, (size_t)need32 * sizeof(float));
    eng_avail_ -= need32;
    memmove(eng_l_, eng_l_ + need32, (size_t)eng_avail_ * sizeof(float));
    memmove(eng_r_, eng_r_ + need32, (size_t)eng_avail_ * sizeof(float));

    float ol[kChunk], orr[kChunk];
    src_up_l_.Process(ol, n, 1);
    src_up_r_.Process(orr, n, 1);

    float *dst = out + (size_t)done * 2;
    for (int i = 0; i < n; ++i) {
      dst[i * 2]     = s16_to_f32(ol[i]);
      dst[i * 2 + 1] = s16_to_f32(orr[i]);
    }

    done += (uint32_t)n;
  }
}

/* Test/inspection hooks (host builds only). */
#ifdef CLOUDS_FX_TEST
extern "C" {
int clouds_fx_test_mode(void) {
  return (int)processor_.playback_mode();
}
float clouds_fx_test_pitch(void) { return knob_pitch_; }
}
#endif
