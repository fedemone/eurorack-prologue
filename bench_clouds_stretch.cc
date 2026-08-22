/*
 * File: bench_clouds_stretch.cc
 *
 * Where Stretch's cost sits, across the knobs that decide it.
 *
 * bench_clouds_spike.cc holds every parameter still at one setting -- SIZE
 * 0.5, PITCH 0 -- and that is not a neutral choice for this mode, it is one
 * point in the middle of the two things that drive the cost.  Stretch's work
 * is a WSOLA correlator load per scheduled window, and both halves of that
 * move with the knobs:
 *
 *   How big each load is.  It is O(window_size_), and window_size_ is SIZE
 *      mapped exponentially over 60 semitones, so the knob spans a factor of
 *      32 in window length -- 128 samples at one end, 4096 at the other.
 *
 *   How often a load happens.  A window is consumed at the pitch ratio, so
 *      the gap between windows shrinks with SIZE and again with PITCH.  The
 *      table in wsola_sample_player.h measured 500 blocks between windows at
 *      size 2048 and pitch 1, and 3 blocks below size 256.
 *
 * Those two pull against each other and the product is roughly flat, which is
 * the argument for why Stretch is affordable at all.  What that argument does
 * not cover is the *burst*: the split in LoadCorrelator() only engages above
 * kCorrelatorSplitWindow (1024) and only when POSITION is far enough from the
 * write head, so there is a region of the SIZE knob -- and one end of the
 * POSITION knob -- where the whole load lands in a single block, frequently.
 *
 * SIZE 0.5, which is the only setting benched until now, maps to a window of
 * about 725 samples.  That is already below the split threshold.
 *
 * Build/run: make bench-clouds-stretch
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>
#include "clouds/dsp/granular_processor.h"
using namespace clouds;

static const size_t kL = 118784, kS = 65536;
alignas(16) static uint8_t lb[kL];
alignas(16) static uint8_t sb[kS];
static GranularProcessor pr;

static inline double now_s(void) {
  struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec * 1e-9;
}

static const double kDeadline = (double)kMaxBlockSize / 32000.0;
#define PCT(x) ((x) / kDeadline * 100.0)

/* What SIZE means in samples, so the rows can be read against
 * kCorrelatorSplitWindow rather than against a knob position. */
static int32_t window_for_size(float size_factor) {
  const float f = powf(2.0f, (size_factor - 1.0f) * 60.0f / 12.0f);
  return (int32_t)(f * 4096.0f);
}

static void run(const char *name, float size, float position, float pitch,
                bool sweep, int blocks) {
  memset(lb, 0, kL); memset(sb, 0, kS);
  pr.Init(lb, kL, sb, kS);
  pr.set_playback_mode(PLAYBACK_MODE_STRETCH);
  pr.set_quality(0);
  pr.set_bypass(false); pr.set_silence(false);
  Parameters *p = pr.mutable_parameters();
  p->position = position; p->size = size; p->pitch = pitch; p->density = 0.5f;
  p->texture = 0.5f; p->dry_wet = 0.9995f; p->stereo_spread = 0.5f;
  p->feedback = 0.f; p->reverb = 0.f; p->freeze = false;
  p->gate = true; p->trigger = false;
  pr.Prepare();

  ShortFrame in[kMaxBlockSize], out[kMaxBlockSize];
  for (size_t i = 0; i < kMaxBlockSize; ++i) {
    in[i].l = (int16_t)(8000 * ((i % 7) - 3));
    in[i].r = (int16_t)(-8000 * ((i % 5) - 2));
  }
  for (int b = 0; b < 2000; ++b) { pr.Prepare(); pr.Process(in, out, kMaxBlockSize); }

  std::vector<double> tot(blocks);
  double mean = 0;
  for (int b = 0; b < blocks; ++b) {
    if (sweep) {
      /* A knob being turned, at a plausible speed: the whole range in about
       * two seconds at 32 kHz / 32-sample blocks. */
      p->size = (float)(b % 1000) / 999.0f;
    }
    const double t0 = now_s();
    pr.Prepare();
    pr.Process(in, out, kMaxBlockSize);
    const double t1 = now_s();
    tot[b] = t1 - t0;
    mean += tot[b];
  }
  mean /= blocks;

  std::vector<double> sorted(tot);
  std::sort(sorted.begin(), sorted.end());
  const double p99 = sorted[(size_t)(blocks * 0.99)];
  const double p999 = sorted[(size_t)(blocks * 0.999)];
  int over = 0;
  for (int b = 0; b < blocks; ++b) if (tot[b] > kDeadline) ++over;

  printf("  %-26s mean %6.2f%%  p99 %7.2f%%  p99.9 %7.2f%%  max %8.2f%%  "
         "over %5.2f%%\n",
         name, PCT(mean), PCT(p99), PCT(p999), PCT(sorted[blocks - 1]),
         100.0 * over / blocks);
}

int main(int argc, char **argv) {
  const int N = (argc > 1) ? atoi(argv[1]) : 8000;
  printf("Deadline for one 32-sample engine block at 32 kHz: %.3f ms\n",
         kDeadline * 1000.0);
  printf("(%d blocks measured per row; split threshold is %d samples)\n\n", N,
         1024);

  printf("SIZE swept across its range, POSITION 0.5, PITCH 0\n");
  static const float sizes[] = { 0.0f, 0.2f, 0.35f, 0.5f, 0.65f, 0.8f, 1.0f };
  for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
    char label[64];
    const int32_t w = window_for_size(sizes[i]);
    snprintf(label, sizeof label, "SIZE %.2f  (window %5d)%s",
             sizes[i], w, w >= 1024 ? " split" : "     ");
    run(label, sizes[i], 0.5f, 0.0f, false, N);
  }

  printf("\nPOSITION at the write head, where the split is refused\n");
  static const float psizes[] = { 0.50f, 0.65f, 0.80f, 1.00f };
  for (size_t i = 0; i < sizeof(psizes) / sizeof(psizes[0]); ++i) {
    char label[64];
    snprintf(label, sizeof label, "SIZE %.2f  POSITION 0.00", psizes[i]);
    run(label, psizes[i], 0.0f, 0.0f, false, N);
    snprintf(label, sizeof label, "SIZE %.2f  POSITION 0.25", psizes[i]);
    run(label, psizes[i], 0.25f, 0.0f, false, N);
  }

  printf("\nPITCH, which decides how fast a window is consumed\n");
  run("SIZE 0.80  PITCH   0", 0.80f, 0.5f, 0.0f, false, N);
  run("SIZE 0.80  PITCH +12", 0.80f, 0.5f, 12.0f, false, N);
  run("SIZE 0.80  PITCH +24", 0.80f, 0.5f, 24.0f, false, N);

  printf("\nSIZE being turned, rather than held\n");
  run("SIZE swept 0..1", 0.5f, 0.5f, 0.0f, true, N);

  return 0;
}
