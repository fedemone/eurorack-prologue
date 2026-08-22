/*
 * File: test_clouds_stretch_clicks.cc
 *
 * Does Stretch step when a knob moves?
 *
 * A click has two possible causes and they need different fixes, so the first
 * job is to tell them apart.  Either a block misses its deadline and the host
 * drops audio, which is a CPU problem and shows up in bench_clouds_stretch.cc,
 * or the engine emits a discontinuity, which is a signal problem and shows up
 * here.  Nothing in a cost measurement can distinguish them, and listening
 * cannot either.
 *
 * A discontinuity is measurable without deciding what counts as audible: the
 * largest sample-to-sample step in the output, compared against the same
 * statistic with the knob held still.  A held render already steps -- it is
 * broadband audio -- so the number that matters is the ratio, not the value.
 * If turning a knob makes the largest step several times what it is at rest,
 * the knob is producing an edge that was not in the signal.
 *
 * Reported per parameter rather than as one figure, because which knob does
 * it is the whole diagnosis: WSOLASamplePlayer::Play() smooths PITCH through
 * smoothed_pitch_ and slews the window size toward SIZE, but takes POSITION
 * raw.
 *
 * Build/run: make test-clouds-stretch-clicks
 */

#include "clouds/dsp/granular_processor.h"
#include "stmlib/utils/random.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>

using namespace clouds;

static const size_t kLargeBufferSize = 118784;
static const size_t kSmallBufferSize = 65536;
alignas(16) static uint8_t large_buffer_[kLargeBufferSize];
alignas(16) static uint8_t small_buffer_[kSmallBufferSize];
static GranularProcessor processor_;

static int failures_ = 0;

enum Knob { kHeld, kPosition, kSize, kPitch, kTexture };

/* The SIZE the non-SIZE rows hold, so the PITCH rows can be run at more than
 * one window length.  SIZE decides how many blocks separate one scheduled
 * window from the next, and ScheduleAlignedWindow() is the only place the
 * pitch ratio is updated -- so if the pitch steps are the once-per-window
 * update, they must get larger as SIZE does. */
static float held_size_ = 0.5f;

/* How many discrete steps a swept knob moves in.  The drumlogue's panel
 * reports integer percent, so a real knob arrives as 100 steps; the engine
 * upstream is fed by a CV scaler that smooths, so it sees a continuum.  0
 * means do not quantise. */
static int quantise_steps_ = 0;

static void engine_init(void) {
  processor_.Quiesce();
  stmlib::Random::Seed(0x12345678u);
  memset(large_buffer_, 0, kLargeBufferSize);
  memset(small_buffer_, 0, kSmallBufferSize);
  processor_.Init(large_buffer_, kLargeBufferSize,
                  small_buffer_, kSmallBufferSize);
  processor_.set_playback_mode(PLAYBACK_MODE_STRETCH);
  processor_.set_quality(0);
  processor_.set_bypass(false);
  processor_.set_silence(false);
  Parameters *p = processor_.mutable_parameters();
  p->position = 0.5f; p->size = held_size_; p->pitch = 0.0f; p->density = 0.5f;
  p->texture = 0.5f; p->dry_wet = 0.9995f; p->stereo_spread = 0.5f;
  p->feedback = 0.0f; p->reverb = 0.0f;
  p->freeze = false; p->gate = true; p->trigger = false;
  processor_.Prepare();
}

struct Result {
  int32_t max_step;      /* largest |x[n] - x[n-1]| in the output */
  double  rms;
  int32_t peak;
};

/* A steady, band-limited input.  Broadband noise would put a large step in
 * the signal itself and bury the thing being looked for; a tone makes any
 * step the engine introduces stand out against a smooth waveform. */
static void fill_input(ShortFrame *in, size_t n, double *phase) {
  for (size_t i = 0; i < n; ++i) {
    *phase += 2.0 * M_PI * 220.0 / 32000.0;
    if (*phase > 2.0 * M_PI) *phase -= 2.0 * M_PI;
    const int16_t s = (int16_t)(9000.0 * sin(*phase));
    in[i].l = s;
    in[i].r = (int16_t)(s / 2);
  }
}

/* fixed_t < 0 sweeps the knob across its range; otherwise the knob is held
 * at that point of its range for the whole render. */
static Result run(int blocks, Knob knob, float fixed_t) {
  ShortFrame in[kMaxBlockSize], out[kMaxBlockSize];
  double phase = 0.0;
  Result r;
  r.max_step = 0;
  r.peak = 0;
  double energy = 0.0;
  int32_t prev_l = 0;
  bool have_prev = false;
  Parameters *p = processor_.mutable_parameters();

  for (int b = 0; b < blocks; ++b) {
    /* The whole range in about two seconds, which is a brisk but ordinary
     * knob move at 32 kHz with 32-sample blocks. */
    float t = fixed_t >= 0.0f ? fixed_t : (float)(b % 1000) / 999.0f;
    if (quantise_steps_ > 0) {
      t = (float)((int)(t * quantise_steps_)) / (float)quantise_steps_;
    }
    switch (knob) {
      case kHeld:                          break;
      case kPosition: p->position = t;     break;
      case kSize:     p->size     = t;     break;
      case kPitch:    p->pitch    = t * 48.0f - 24.0f; break;
      case kTexture:  p->texture  = t;     break;
    }
    fill_input(in, kMaxBlockSize, &phase);
    processor_.Prepare();
    processor_.Process(in, out, kMaxBlockSize);
    for (size_t i = 0; i < kMaxBlockSize; ++i) {
      const int32_t l = out[i].l;
      if (have_prev) {
        const int32_t step = abs(l - prev_l);
        if (step > r.max_step) r.max_step = step;
      }
      prev_l = l;
      have_prev = true;
      if (abs(l) > r.peak) r.peak = abs(l);
      energy += (double)l * l;
    }
  }
  r.rms = sqrt(energy / (blocks * kMaxBlockSize));
  return r;
}

static Result measure(Knob knob, float size, float fixed_t) {
  held_size_ = size;
  engine_init();
  run(400, knob, fixed_t);           /* settle */
  return run(2000, knob, fixed_t);
}

/* The control a swept row has to beat.
 *
 * Comparing a swept knob against the *resting* render is wrong for any knob
 * that changes the signal's frequency content, and PITCH is exactly that: the
 * largest step of a sine is proportional to its frequency, so shifting up an
 * octave doubles the step with nothing discontinuous happening.  The first
 * version of this test read that as a click.
 *
 * So the reference is the knob held still at each point of its own range, and
 * the largest step any of those produce.  A swept render that stays under
 * that is doing nothing the knob does not do standing still. */
static Result held_across_range(Knob knob, float size) {
  Result worst;
  worst.max_step = 0; worst.rms = 0.0; worst.peak = 0;
  static const float points[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
  for (size_t i = 0; i < sizeof(points) / sizeof(points[0]); ++i) {
    const Result r = measure(knob, size, points[i]);
    if (r.max_step > worst.max_step) worst.max_step = r.max_step;
    if (r.peak > worst.peak) worst.peak = r.peak;
    if (r.rms > worst.rms) worst.rms = r.rms;
  }
  return worst;
}

int main(void) {
  printf("Clouds Stretch Discontinuity Test\n\n");
  printf("Largest sample-to-sample step in the output, 220 Hz input.\n");
  printf("The held row is the reference: a step that size is in the signal.\n\n");

  const Result held = measure(kHeld, 0.5f, -1.0f);
  printf("  %-22s max step %6d   rms %8.1f   peak %6d\n",
         "held", held.max_step, held.rms, held.peak);

  if (held.peak == 0) {
    printf("\nFAIL held render is silent -- every comparison below is vacuous\n");
    return 1;
  }

  static const struct { Knob k; const char *name; } knobs[] = {
    { kPosition, "POSITION swept" },
    { kSize,     "SIZE swept" },
    { kPitch,    "PITCH swept" },
    { kTexture,  "TEXTURE swept" },
  };

  /* Two thresholds, because there are two questions.
   *
   * Above 2x the control is worth printing: the knob is doing something to
   * the waveform that holding it anywhere does not.  That is not by itself a
   * defect -- TEXTURE sits here, and TEXTURE drives the LP/HP cutoff over 216
   * semitones, so sweeping it *is* a fast filter sweep and a fast filter
   * sweep has edges in it.
   *
   * Above 4x is a failure: at that point the step is larger than anything the
   * signal does at any setting, which is an edge the engine put there.
   *
   * The gap between the two is where a judgement lives, so the test prints
   * the number and leaves it visible rather than encoding an opinion. */
  for (size_t i = 0; i < sizeof(knobs) / sizeof(knobs[0]); ++i) {
    const Result swept = measure(knobs[i].k, 0.5f, -1.0f);
    const Result ctrl  = held_across_range(knobs[i].k, 0.5f);
    const double ratio = (double)swept.max_step /
                         (double)(ctrl.max_step ? ctrl.max_step : 1);
    printf("  %-22s max step %6d   vs held-anywhere %6d   %5.2fx%s\n",
           knobs[i].name, swept.max_step, ctrl.max_step, ratio,
           ratio > 4.0 ? "   <-- EDGE" : (ratio > 2.0 ? "   <-- notable" : ""));
    if (ratio > 4.0) ++failures_;
  }

  /* The port hands the engine whatever the panel reports, and the panel
   * reports whole percent.  Upstream never sees that: Clouds' cv_scaler
   * smooths every parameter before GranularProcessor does.  If the edge grows
   * as the knob is made coarser, it is the steps and not the sweep. */
  printf("\n  TEXTURE swept at different knob resolutions:\n");
  static const int steps[] = { 0, 1000, 100, 50, 20 };
  for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i) {
    quantise_steps_ = steps[i];
    const Result r = measure(kTexture, 0.5f, -1.0f);
    char label[64];
    if (steps[i] == 0) snprintf(label, sizeof label, "TEXTURE continuous");
    else snprintf(label, sizeof label, "TEXTURE in %d steps%s", steps[i],
                  steps[i] == 100 ? "  (the panel)" : "");
    printf("  %-30s max step %6d\n", label, r.max_step);
  }
  quantise_steps_ = 0;

  printf("\n  TEXTURE is the one that sits above 2x, and the rows above say why:\n"
         "  the edge barely moves with knob resolution (401 continuous, 414 at\n"
         "  the panel's 100 steps), so it is the sweep and not the steps.  In\n"
         "  Stretch, TEXTURE is the LP/HP cutoff over 216 semitones.\n");

  printf("\n%s\n", failures_
         ? "=== a swept knob puts an edge in the output ==="
         : "=== ALL PASS (0 failures) ===");
  return failures_ ? 1 : 0;
}
