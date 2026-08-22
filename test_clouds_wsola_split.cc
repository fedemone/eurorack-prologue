/*
 * File: test_clouds_wsola_split.cc
 *
 * Differential test for splitting the WSOLA correlator load across two blocks.
 *
 * The split in LoadCorrelator() is the largest remaining burst in the engine
 * now that Spectral's transform has moved off the audio thread, and it is the
 * kind of change that cannot be argued into correctness: it defers half of a
 * correlation window's read by one audio block, so whether the deferred half
 * reads the same samples depends on how far the write head has advanced in
 * the meantime, which depends on SIZE, POSITION and PITCH together.
 *
 * The fork notes in wsola_sample_player.h quote a sweep -- "12 of 90 points"
 * for one stage order against "3 of 90" for the other, and "18 of 90" for a
 * rejected guard. That sweep was run once, by hand, and never committed, so
 * every number in those notes rested on something nobody could re-run. This
 * is that sweep, made reproducible.
 *
 * The comparison is the split against no split at all, both from the fork, so
 * the only variable is the scheduling. A point that differs is not
 * automatically a fault: the deferred read legitimately sees audio recorded
 * one block later, and WSOLA's answer is a splice position, so a flipped best
 * match moves a splice by a few samples in a signal that is being spliced
 * anyway. What the test reports is how many points move and how far, so that
 * the trade is visible rather than assumed.
 *
 * A note on settling, because it changes what the grid means.  window_size_
 * is not set from SIZE, it is slewed toward it -- by (target - current) >> 5,
 * and only inside ScheduleAlignedWindow(), which runs once per window.  At
 * large SIZE that is hundreds of blocks apart, so the window length converges
 * over tens of thousands of blocks, not hundreds.  The settle below is long
 * enough to be past the initial transient but not long enough to converge at
 * every point, so a row's SIZE is where the engine was pointed, not
 * necessarily the window length it ran at.  That does not weaken the
 * comparison -- both builds settle identically, and the only variable is the
 * split -- but it is why a row labelled 0.50 can show a difference that only
 * a window above kCorrelatorSplitWindow could produce.
 *
 * Build/run: make test-clouds-wsola-split
 */

#include "clouds/dsp/granular_processor.h"
#include "stmlib/utils/random.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>

using namespace clouds;

namespace clouds {
extern uint32_t g_wsola_split_taken;
extern uint32_t g_wsola_split_refused;
}

static const size_t kLargeBufferSize = 118784;
static const size_t kSmallBufferSize = 65536;
alignas(16) static uint8_t large_buffer_[kLargeBufferSize];
alignas(16) static uint8_t small_buffer_[kSmallBufferSize];
static GranularProcessor processor_;

static uint64_t hash_init(void) { return 1469598103934665603ULL; }
static void hash_frames(uint64_t *h, const ShortFrame *f, size_t n) {
  const uint8_t *p = reinterpret_cast<const uint8_t *>(f);
  for (size_t i = 0; i < n * sizeof(ShortFrame); ++i) {
    *h ^= p[i];
    *h *= 1099511628211ULL;
  }
}

static void fill_input(ShortFrame *in, size_t n, uint32_t *state) {
  for (size_t i = 0; i < n; ++i) {
    *state = *state * 1664525u + 1013904223u;
    const int16_t s = (int16_t)((int32_t)(*state >> 16) - 32768) / 3;
    in[i].l = s;
    in[i].r = (int16_t)(-s / 2);
  }
}

static void engine_init(float size, float position, float pitch, int quality) {
  processor_.Quiesce();
  stmlib::Random::Seed(0x12345678u);
  memset(large_buffer_, 0, kLargeBufferSize);
  memset(small_buffer_, 0, kSmallBufferSize);
  processor_.Init(large_buffer_, kLargeBufferSize,
                  small_buffer_, kSmallBufferSize);
  processor_.set_playback_mode(PLAYBACK_MODE_STRETCH);
  processor_.set_quality(quality);
  processor_.set_bypass(false);
  processor_.set_silence(false);
  Parameters *p = processor_.mutable_parameters();
  p->position = position; p->size = size; p->pitch = pitch;
  p->density = 0.5f; p->texture = 0.5f; p->dry_wet = 0.9995f;
  p->stereo_spread = 0.5f; p->feedback = 0.0f; p->reverb = 0.0f;
  p->freeze = false; p->gate = true; p->trigger = false;
  processor_.Prepare();
}

int main(void) {
  printf("# CLOUDS_WSOLA_SPLIT_WINDOW=%d CLOUDS_WSOLA_HEAD_MARGIN=%d\n",
         (int)CLOUDS_WSOLA_SPLIT_WINDOW, (int)(CLOUDS_WSOLA_HEAD_MARGIN));

  /* The grid is chosen around what decides whether a deferred read is safe:
   * SIZE sets the window length and so both the burst and the gap between
   * windows, POSITION sets how close the correlation windows sit to the write
   * head, and PITCH sets how fast a window is consumed. Quality 0 and 1 are
   * the stereo and mono paths, which read the buffer differently. */
  static const float sizes[]     = { 0.30f, 0.50f, 0.65f, 0.80f, 1.00f };
  static const float positions[] = { 0.00f, 0.10f, 0.25f, 0.50f, 1.00f };
  static const float pitches[]   = { -24.0f, 0.0f, 7.0f, 24.0f };
  static const int qualities[]   = { 0, 1 };

  for (size_t q = 0; q < sizeof(qualities) / sizeof(qualities[0]); ++q) {
    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
      for (size_t p = 0; p < sizeof(positions) / sizeof(positions[0]); ++p) {
        for (size_t t = 0; t < sizeof(pitches) / sizeof(pitches[0]); ++t) {
          engine_init(sizes[s], positions[p], pitches[t], qualities[q]);
          g_wsola_split_taken = 0;
          g_wsola_split_refused = 0;
          ShortFrame in[kMaxBlockSize], out[kMaxBlockSize];
          uint32_t st = 999u;
          uint64_t h = hash_init();
          double energy = 0.0;
          int peak = 0;
          for (int b = 0; b < 6000; ++b) {
            fill_input(in, kMaxBlockSize, &st);
            processor_.Prepare();
            processor_.Process(in, out, kMaxBlockSize);
            if (b >= 3000) {                      /* settle */
              hash_frames(&h, out, kMaxBlockSize);
              for (size_t i = 0; i < kMaxBlockSize; ++i) {
                const int l = abs((int)out[i].l);
                if (l > peak) peak = l;
                energy += (double)out[i].l * out[i].l;
              }
            }
          }
          printf("P q%d size %.2f pos %.2f pitch %+6.1f  %016llx  "
                 "rms %9.3f  peak %6d\n",
                 qualities[q], sizes[s], positions[p], pitches[t],
                 (unsigned long long)h,
                 sqrt(energy / (3000.0 * kMaxBlockSize)), peak);
          printf("S q%d size %.2f pos %.2f pitch %+6.1f  split taken %5u  "
                 "refused %5u\n",
                 qualities[q], sizes[s], positions[p], pitches[t],
                 g_wsola_split_taken, g_wsola_split_refused);
        }
      }
    }
  }
  return 0;
}
