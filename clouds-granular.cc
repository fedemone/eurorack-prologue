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
 * Clouds is a 32 kHz machine, so the engine runs at 32 kHz here and the
 * conversion to and from the drumlogue's 48 kHz happens at the edges of
 * this file (see clouds_src.h).  That keeps SIZE, the delay times, the
 * buffer capacity and the feedback filter's corner at the values Clouds
 * was voiced with, and it drops the engine's cost by a third: 1000 blocks
 * per second instead of 1500.  The audible price is a rolloff above
 * 13 kHz -- about what Clouds' own codec does.
 *
 * Pitch is unaffected either way: the sawtooth is synthesised straight at
 * 32 kHz, and sample playback still reads at 48 kHz and is decimated, so a
 * sample plays back at its recorded pitch.
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
#include "clouds_src.h"
#include "stmlib/dsp/dsp.h"

#include <cstring>
#include <cmath>

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

#include "clouds/dsp/granular_processor.h"

using namespace clouds;

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
alignas(16) static uint8_t large_buffer_[kLargeBufferSize];
alignas(16) static uint8_t small_buffer_[kSmallBufferSize];

static GranularProcessor processor_;

/* Built-in sawtooth oscillator state */
static float osc_phase_ = 0.0f;
static float osc_frequency_ = 440.0f;
static bool osc_active_ = false;

/* User-facing parameter storage */
static uint16_t p_values[k_num_user_osc_param_id] = {0};
static float shape = 0, shiftshape = 0;
static float shape_lfo = 0;

/* Custom param storage */
static int32_t pitch_semitones_ = 0;

/* Sample playback state
 *
 * load_sample() runs on the drumlogue's param/UI thread while
 * generate_sample_input() reads the sample on the audio thread, so pointer
 * and length have to reach the reader as one unit.  Publishing them as
 * separate variables — however carefully ordered, and even with the pointer
 * written last — does not achieve that: the audio thread can read the OLD
 * pointer just before load_sample() runs and the NEW length just after, then
 * index a short sample with a long sample's frame count and read clean off
 * the end of the buffer.
 *
 * Instead each load fills the slot that is NOT currently published and then
 * publishes its address with a release store.  A record is therefore never
 * mutated while it is reachable, and the acquire load below yields a pointer,
 * length and channel count that were always written together.
 */
struct SampleState {
  const float *ptr;
  size_t frames;
  uint8_t channels;
};

static uint8_t sample_bank_ = 0;
static uint8_t sample_number_ = 0;  /* 0 = use sawtooth, 1+ = sample */
static SampleState sample_slots_[2];
static uint8_t sample_slot_ = 0;                    /* UI thread only */
static SampleState *sample_state_ = nullptr;        /* published record */
static size_t sample_read_pos_ = 0;
static volatile uint16_t sample_start_permil_ = 0;     /* 0-1000 (0.0%-100.0%) */
static volatile uint16_t sample_end_permil_ = 1000;    /* 0-1000 (0.0%-100.0%) */

/* Publish a sample record (UI thread). Pass a null ptr to select the
 * internal sawtooth. */
static void publish_sample(const float *ptr, size_t frames, uint8_t channels) {
  if (!ptr || frames == 0) {
    __atomic_store_n(&sample_state_, (SampleState *)nullptr, __ATOMIC_RELEASE);
    return;
  }
  sample_slot_ ^= 1;
  SampleState *s = &sample_slots_[sample_slot_];
  s->ptr = ptr;
  s->frames = frames;
  s->channels = channels;
  __atomic_store_n(&sample_state_, s, __ATOMIC_RELEASE);
}

struct SampleSnapshot {
  const float *ptr;
  size_t frames;
  uint8_t channels;
  uint16_t start_permil;
  uint16_t end_permil;
};

/* Take a consistent snapshot of the sample state (audio thread). */
static inline void snapshot_sample(SampleSnapshot *snap) {
  const SampleState *s = __atomic_load_n(&sample_state_, __ATOMIC_ACQUIRE);
  snap->ptr = s ? s->ptr : nullptr;
  snap->frames = s ? s->frames : 0;
  snap->channels = s ? s->channels : 0;
  snap->start_permil = sample_start_permil_;
  snap->end_permil = sample_end_permil_;
}

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
    publish_sample(nullptr, 0, 0);
    return;
  }
  /* sample_number_ is 1-based; API is 0-based */
  const sample_wrapper_t *sw =
      osc_adapter_get_sample(sample_bank_, sample_number_ - 1);
  if (sw && sw->sample_ptr && sw->frames > 0) {
    /* Move the read position before publishing, so the audio thread never
     * sees the new sample with the old sample's position. */
    size_t start = (size_t)((uint64_t)sample_start_permil_ * sw->frames / 1000);
    if (start >= sw->frames) start = sw->frames - 1;
    sample_read_pos_ = start;
    publish_sample(sw->sample_ptr, sw->frames, sw->channels);
  } else {
    sample_read_pos_ = 0;
    publish_sample(nullptr, 0, 0);
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

/* Clamp a 0-1 engine parameter to just below 1.0.
 *
 * Several Clouds parameters (dry_wet, size, texture, ...) are used as LUT
 * indices via stmlib::Interpolate(table, value, size), which reads
 * table[value*size] AND table[value*size + 1].  At exactly 1.0 that second
 * read is one element PAST the end of the table.  On the original Clouds
 * (bare-metal STM32) the stray flash read was harmless, and the pots/CV
 * never delivered exactly 1.0 anyway.  On drumlogue, units run in a
 * memory-protected process, so the out-of-bounds read can fault — which is
 * exactly what happened at the first trigger with the default Dry/Wet=100%:
 * the internal dry/wet ramp reached 1.0 on the first sounding block and the
 * Interpolate(lut_xfade_in, 1.0, 16) read past the 17-entry table.
 * Clamping to 0.9995 (inaudible, ~0.05% off full) keeps every LUT access
 * in bounds without touching the eurorack submodule. */
static inline float clip_param(float x) {
  return (x < 0.f) ? 0.f : ((x > 0.9995f) ? 0.9995f : x);
}

/* Density: map the knob onto Clouds' grain scheduler, skipping its dead zone.
 *
 * Clouds' DENSITY is bipolar around 0.5.  ProcessGranular() derives the
 * scheduler's `overlap` as (density - 0.53) * 2.12 above the centre and
 * (0.47 - density) * 2.12 below it, so 0.47..0.53 schedules no grains at all.
 * GranularSamplePlayer::Play() then takes target_num_grains =
 * max_num_grains * overlap^3, so the useful range runs from overlap ~0.32
 * (about one grain) to 1.0 (all 32 in stereo hi-fi).
 *
 * As a self-contained synth voice we want the knob to read sparse-to-dense
 * end to end, so 0..100 % maps monotonically onto the upper (probabilistic)
 * branch.  The lower branch's periodic grain clock is not reachable; the
 * trade is a knob with no silent middle.
 *
 * This replaces forcing parameters.trigger on every block, which is what the
 * port used to do to work around the dead zone.  One trigger per 32-sample
 * block is 1500 grains/second: it pinned the grain pool at maximum wherever
 * DENSITY was set, put a 1500 Hz (block-rate) buzz on everything, and left
 * the engine at worst-case CPU permanently.  A trigger is now seeded once at
 * note-on, for an immediate attack, and the scheduler runs the cloud.
 *
 * The knob is linear in *grain count*, not in `density`.  Mapping the knob
 * straight onto density 0.68..1.0 inherits the engine's cubic law, which puts
 * almost all of the range — and almost all of the CPU — in the last fifth of
 * the travel:
 *
 *      knob      10   30   50   70   80   90  100 %
 *      grains     1    4    8   15   20   25   32
 *
 * The top fifth of that knob adds more grains than the bottom three fifths
 * together, so the control is unusably bunched at the top and the last part
 * of it costs more than the drumlogue can pay: measured on ARM, knob 100 %
 * is 3.1x the block cost of knob 0 %, which puts Clouds above Rings — the
 * most expensive stock unit — with a whole kit still to render.
 *
 * Inverting the cube spreads the grains evenly over the travel, and capping
 * the top at kGrainsMax keeps the whole range payable.  kGrainsMax is
 * expressed as a fraction of the pool the engine has allocated, so it tracks
 * the Quality setting (32 grains stereo hi-fi, 40 mono, more when low-fi). */
static const float kGrainsMin = 0.032f; /* ~1 grain: sparse but never silent */
static const float kGrainsMax = 0.57f;  /* ~18 of 32: dense, and affordable  */

static inline float density_to_clouds(float knob01) {
  const float k = clip_param(knob01);
  /* target_num_grains = max_num_grains * overlap^3, so a knob linear in
   * grains needs the cube root. */
  const float grains = kGrainsMin + k * (kGrainsMax - kGrainsMin);
  const float overlap = cbrtf(grains);
  return 0.53f + overlap * (1.0f / 2.12f);
}

/* Mapped DENSITY.  Recomputed by OSC_PARAM on the control thread so the cube
 * root above never runs under the audio deadline; OSC_CYCLE only loads it.
 * A plain aligned float is fine for that hand-off — it is one word, and a
 * block rendered with the previous value is inaudible. */
static volatile float density_mapped_ = 0.0f;

/* Set by OSC_NOTEON (control thread), consumed by the next OSC_CYCLE. */
static volatile bool note_trigger_ = false;

/* Mode / Quality are latched here and applied on the audio thread.
 *
 * set_playback_mode() and set_quality() do more than store a setting: they
 * change num_channels_ and low_fidelity_, which decide whether Process()
 * reads the 16-bit or the 8-bit audio buffers.  The matching buffers are only
 * (re)initialized by the next Prepare().  Calling those setters straight from
 * the parameter callback — which the drumlogue runs on the UI thread — leaves
 * a window in which the audio thread is inside Process() with a resolution
 * whose buffers have never been set up, reading through an uninitialized
 * pointer.  Latching the request and applying it in OSC_CYCLE, immediately
 * before Prepare(), keeps every engine reconfiguration on the audio thread. */
static volatile int32_t pending_mode_ = 0;
static volatile int32_t pending_quality_ = 0;
static volatile int32_t pending_freeze_ = 0;

static inline float midi_to_hz(float note) {
  /* Fast MIDI-to-Hz: 440 * 2^((note - 69) / 12) */
  return 440.0f * powf(2.0f, (note - 69.0f) / 12.0f);
}

/**
 * Generate sawtooth samples into ShortFrame input buffer (mono: L=R).
 * Amplitude is 50% (-6 dB) to avoid clipping in the feedback path.
 *
 * `rate` is the rate the samples are generated at.  The sawtooth is
 * synthesised straight at the engine's 32 kHz — it is a naive ramp with no
 * band limiting either way, so putting it through the input decimator would
 * cost two 60-tap filters to clean up aliasing that the generator itself
 * puts back.  Sample playback is the case that genuinely needs the
 * decimator, and it asks for 48 kHz.
 */
static void generate_input(ShortFrame *input, size_t size, float frequency,
                           float rate) {
  const float phase_inc = frequency / rate;
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
 * 48 kHz <-> 32 kHz plumbing
 *
 * OSC_CYCLE owes the adapter exactly kMaxBlockSize samples at 48 kHz every
 * call.  The engine runs at 32 kHz, so one engine block of 32 frames covers
 * 48 output samples -- an engine block is needed on two calls out of three,
 * and which two depends on the resampler's phase.  Rather than track that,
 * the render is pull-driven: ask the upsampler how many 32 kHz samples the
 * next 32 output samples need, and run engine blocks until the staging FIFO
 * holds that many.
 *
 * The input side is asymmetric, deliberately.  Sample playback is decimated
 * from 48 kHz -- an engine block asks the decimator for 32 samples, which
 * turns into a request for 48 at 48 kHz, and generate_sample_input() is
 * called for exactly that many, unchanged.  Reading the bank at 1.5 frames
 * per step instead would skip the anti-aliasing, which is audible on any
 * bright sample.  The sawtooth takes the other route and is generated at
 * 32 kHz directly: it is a naive ramp with no band limiting either way, so
 * putting it through two 60-tap filters would only clean up aliasing the
 * generator immediately puts back.  Measured, that one asymmetry is most of
 * the difference between the first 32 kHz build and this one.
 * ==================================================================== */

static clouds_src::SrcDown src_down_l_;
static clouds_src::SrcDown src_down_r_;
static clouds_src::SrcUp src_up_;

/* Engine output waiting to be resampled up: mono, 32 kHz, int16-scaled.
 * At most 21 samples survive a call and a block adds 32, so 64 is plenty. */
static float stage_[64];
static int stage_avail_ = 0;

/* Note frequency, captured by OSC_CYCLE for the engine blocks it drives. */
static float note_hz_ = 440.0f;

static inline int16_t clamp_s16(float x) {
  return (x > 32767.0f) ? 32767 : ((x < -32768.0f) ? -32768 : (int16_t)x);
}

/* Which source last filled an engine block, so the decimators can be cleared
 * when the sawtooth/sample switch leaves stale history in them. */
static bool src_down_active_ = false;

/* Render one 32-frame engine block at 32 kHz and append it to stage_. */
static void engine_block(void) {
  ShortFrame input[kMaxBlockSize];
  ShortFrame output[kMaxBlockSize];

  SampleSnapshot snap;
  snap.ptr = nullptr;
  snap.frames = 0;
  if (osc_active_)
    snapshot_sample(&snap);

  if (snap.ptr && snap.frames > 0) {
    /* --- Sample input: read at 48 kHz, decimate to 32 kHz ---
     * Stepping the sample pointer at 1.5 frames instead would be cheaper but
     * has no anti-aliasing, which is audible on anything bright. */
    if (!src_down_active_) {
      src_down_l_.Init();
      src_down_r_.Init();
      src_down_active_ = true;
    }
    const int need48 = src_down_l_.InputNeeded((int)kMaxBlockSize);
    ShortFrame gen[clouds_src::kMaxIn];
    generate_sample_input(gen, (size_t)need48, snap);

    float *dl = src_down_l_.Input();
    float *dr = src_down_r_.Input();
    for (int i = 0; i < need48; ++i) {
      dl[i] = (float)gen[i].l;
      dr[i] = (float)gen[i].r;
    }

    float il[kMaxBlockSize], ir[kMaxBlockSize];
    src_down_l_.Process(il, (int)kMaxBlockSize, 1);
    src_down_r_.Process(ir, (int)kMaxBlockSize, 1);
    for (size_t i = 0; i < kMaxBlockSize; ++i) {
      input[i].l = clamp_s16(il[i]);
      input[i].r = clamp_s16(ir[i]);
    }
  } else {
    src_down_active_ = false;
    if (osc_active_)
      generate_input(input, kMaxBlockSize, note_hz_, 32000.0f);
    else
      memset(input, 0, sizeof(input));
  }

  /* --- Engine --- */
  Parameters *p = processor_.mutable_parameters();
  p->position = clip_param(shape);                                 /* id 1 */
  p->size = clip_param(shiftshape);                                /* id 2 */
  p->density = density_mapped_;                                    /* id 3 */
  p->texture = clip_param(p_values[k_user_osc_param_id2] * 0.01f);  /* id 4 */
  p->pitch = (float)pitch_semitones_;                              /* id 5 */
  p->feedback = clip_param(p_values[k_user_osc_param_id4] * 0.01f); /* id 6 */
  p->dry_wet = clip_param(p_values[k_user_osc_param_id5] * 0.01f);  /* id 7 */
  p->reverb = clip_param(p_values[k_user_osc_param_id6] * 0.01f);   /* id 8 */
  p->gate = osc_active_;

  /* Seed exactly one grain per note-on, so the attack is immediate instead of
   * waiting on the probabilistic scheduler.  The flag is cleared here rather
   * than in OSC_CYCLE because a call that runs no engine block would
   * otherwise swallow the note's only trigger. */
  p->trigger = note_trigger_;
  note_trigger_ = false;

  /* Apply latched engine reconfiguration on this (audio) thread, so the
   * buffer layout can never change underneath Process(). */
  if ((int32_t)processor_.playback_mode() != pending_mode_)
    processor_.set_playback_mode(static_cast<PlaybackMode>(pending_mode_));
  if (processor_.quality() != pending_quality_)
    processor_.set_quality(pending_quality_);
  processor_.set_freeze(pending_freeze_ != 0);

  /* Prepare handles mode/quality switches and buffer resets */
  processor_.Prepare();
  processor_.Process(input, output, kMaxBlockSize);

  /* --- Output: mix to mono and stage for the upsampler --- */
  float *dst = &stage_[stage_avail_];
  for (size_t i = 0; i < kMaxBlockSize; ++i)
    dst[i] = (output[i].l + output[i].r) * 0.5f;
  stage_avail_ += (int)kMaxBlockSize;
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
  density_mapped_ = density_to_clouds(0.5f);
  params->density = density_mapped_;
  params->texture = 0.5f;
  params->dry_wet = 1.0f; /* fully wet by default */
  params->stereo_spread = 0.5f;
  params->feedback = 0.0f;
  params->reverb = 0.0f;
  params->freeze = false;
  params->trigger = false;
  params->gate = false;

  pending_mode_ = PLAYBACK_MODE_GRANULAR;
  pending_quality_ = 0;
  pending_freeze_ = 0;

  /* Run the first Prepare() here rather than letting it land on the first
   * audio block: it is the call that allocates and zeroes the ~190 KB of
   * sample/FX buffers, which has no business happening under an audio
   * deadline.  Afterwards Prepare() is cheap in Granular mode. */
  processor_.Prepare();

  src_down_l_.Init();
  src_down_r_.Init();
  src_down_active_ = false;
  src_up_.Init();
  stage_avail_ = 0;

  osc_phase_ = 0.0f;
  osc_frequency_ = midi_to_hz(60.0f);
  note_hz_ = osc_frequency_;
  osc_active_ = false;
  pitch_semitones_ = 0;

  sample_bank_ = 0;
  sample_number_ = 0;
  sample_read_pos_ = 0;
  publish_sample(nullptr, 0, 0);
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
  note_hz_ = osc_frequency_;

  /* Run only as many engine blocks as this output block actually consumes.
   * Two calls in three need one; the third needs none. */
  const int need = src_up_.InputNeeded((int)kMaxBlockSize);
  while (stage_avail_ < need)
    engine_block();

  float *up_in = src_up_.Input();
  memcpy(up_in, stage_, (size_t)need * sizeof(float));
  stage_avail_ -= need;
  memmove(stage_, stage_ + need, (size_t)stage_avail_ * sizeof(float));

  float mono[kMaxBlockSize];
  src_up_.Process(mono, (int)kMaxBlockSize, 1);

  /* Mono 32 kHz engine output, now at 48 kHz, converted to Q31.
   * The adapter expects exactly kMaxBlockSize mono Q31 samples.
   *
   * A dense granular cloud sums many overlapping grains and, after Clouds'
   * internal post-gain, rides right up against 0 dBFS.  At that level the
   * peaks convert to near-full-scale Q31 and the sharp grain edges read as
   * clicks/harshness on the output.  Applying a little headroom here
   * (kOutGain, ~-3 dB) keeps the loudest textures clear of the ceiling
   * without making the module noticeably quiet.  Each mono sample is in the
   * int16 range; scaling by kOutGain * 65536 lifts it to Q31. */
  const float kOutGain = 0.7f;
#ifdef __ARM_NEON
  {
    const float32x4_t vscale = vdupq_n_f32(kOutGain * 65536.0f);
    for (size_t i = 0; i < kMaxBlockSize; i += 4) {
      /* vcvtq_s32_f32 saturates, so the resampler's overshoot on a
       * full-scale transient cannot wrap the Q31 output. */
      vst1q_s32(yn + i, vcvtq_s32_f32(vmulq_f32(vld1q_f32(mono + i), vscale)));
    }
  }
#else
  for (size_t i = 0; i < kMaxBlockSize; ++i) {
    float v = mono[i] * (kOutGain * 65536.0f);
    if (v > 2147483520.0f) v = 2147483520.0f;
    if (v < -2147483520.0f) v = -2147483520.0f;
    yn[i] = (int32_t)v;
  }
#endif
}

void OSC_NOTEON(const user_osc_param_t *const params)
{
  (void)params;
  osc_active_ = true;
  note_trigger_ = true;
  /* Reset sample playback to start point */
  {
    SampleSnapshot snap;
    snapshot_sample(&snap);
    if (snap.ptr && snap.frames > 0) {
      size_t start = (size_t)((uint64_t)snap.start_permil * snap.frames / 1000);
      if (start >= snap.frames) start = snap.frames - 1;
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

/* Neutral state, on the audio thread between blocks.  See OSC_RESET in
 * drumlogue/userosc.h for why it is not done where unit_reset() is called.
 *
 * The audio buffer is the whole point here: Clouds holds seconds of recorded
 * material, and without this a unit swap carries the previous patch's
 * recording into the next one. RequestBufferReset() hands the clearing to the
 * Prepare() that already runs at the top of every block -- the same route
 * Mode and Quality changes take. It is the expensive Prepare(), so it costs
 * one over-budget block, which is what the SDK means by delay lines being
 * "set to be cleared" rather than cleared at once. */
void OSC_RESET(void)
{
  osc_active_ = false;
  processor_.mutable_parameters()->gate    = false;
  processor_.mutable_parameters()->trigger = false;
  processor_.mutable_parameters()->freeze  = false;
  pending_freeze_ = 0;
  processor_.RequestBufferReset();
}

void OSC_PARAM(uint16_t index, uint16_t value)
{
  switch (index) {
    case k_user_osc_param_id1: /* Density */
      p_values[index] = value;
      density_mapped_ = density_to_clouds(value * 0.01f);
      break;

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

    /* Custom params passed by the wrapper.
     * These reconfigure the engine, so they are only latched here and
     * applied by OSC_CYCLE on the audio thread (see pending_* above). */
    case 8: /* Freeze (0 = off, 1 = on) */
      pending_freeze_ = (value != 0) ? 1 : 0;
      break;

    case 9: /* Mode (0-3: Granular/Stretch/Delay/Spectral) */
      if (value < PLAYBACK_MODE_LAST)
        pending_mode_ = value;
      break;

    case 10: /* Quality (0-3: StHi/MoHi/StLo/MoLo) */
      pending_quality_ = value & 0x3;
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
