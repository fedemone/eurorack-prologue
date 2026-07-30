/*
 * File: bench_clouds_spike.cc
 *
 * Where does Spectral's cost sit: spread out, or concentrated in one block?
 *
 * This is the harness behind the freeze diagnosis in
 * docs/CLOUDS_DRUMLOGUE_AUDIO_NOTES.md, and the one that chose the FFT size.
 * Spectral's *average* cost is the second-lowest of the four modes, which is
 * why looking at means led nowhere for a long time. Nearly all of it lands in
 * one block per hop, where a full forward FFT, the spectral modifier and a
 * full inverse run back to back -- twice over in stereo -- inside a single
 * audio block. At upstream's 4096 that block measured around 4x its deadline
 * and hung the hardware. So this reports the tail and the fraction of blocks
 * that miss the deadline alongside the mean; the mean on its own says nothing
 * about whether the unit crackles.
 *
 * Build with -DCLOUDS_FFT_SIZE=N to compare sizes.
 *
 * A mode with no burst mechanism at all (Granular) is measured alongside as
 * a reference row: anything Spectral does that Granular also does is the
 * harness or the host, not the FFT.
 *
 * The engine runs at 32 kHz, so a 32-sample block is 1 ms of audio and that
 * is the deadline. Percentages are relative, not absolute: built for ARM and
 * run under qemu-arm, which is roughly an order of magnitude slower than the
 * SoC. Comparisons between rows are meaningful, the absolute values are not.
 *
 * Build/run: make bench-clouds-spike (ARM under QEMU)
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <algorithm>
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

/* One 32-sample engine block at 32 kHz. */
static const double kDeadline = (double)kMaxBlockSize / 32000.0;
#define PCT(x) ((x) / kDeadline * 100.0)

static void run(const char *name, PlaybackMode mode, float density,
                float position, int blocks) {
  memset(lb, 0, kL); memset(sb, 0, kS);
  pr.Init(lb, kL, sb, kS);
  pr.set_playback_mode(mode);
  pr.set_quality(0);
  pr.set_bypass(false); pr.set_silence(false);
  Parameters *p = pr.mutable_parameters();
  p->position = position; p->size = 0.5f; p->pitch = 0.f; p->density = density;
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

  std::vector<double> tot_us(blocks);
  double mean = 0, prep_mean = 0, prep_worst = 0;
  for (int b = 0; b < blocks; ++b) {
    const double t0 = now_s();
    pr.Prepare();
    const double t1 = now_s();
    pr.Process(in, out, kMaxBlockSize);
    const double t2 = now_s();
    prep_mean += t1 - t0;
    prep_worst = std::max(prep_worst, t1 - t0);
    tot_us[b] = t2 - t0;
    mean += tot_us[b];
  }
  mean /= blocks; prep_mean /= blocks;

  /* Percentiles, not the maximum. Under QEMU the single worst block is
   * dominated by host scheduling -- the reference row below is a mode with no
   * burst at all, and its maximum still lands anywhere between 30% and 180%
   * from run to run. p99.9 is stable to a few percent and still catches a
   * once-per-hop burst, since even the shortest hop here fires far more often
   * than one block in a thousand. */
  std::sort(tot_us.begin(), tot_us.end());
  const double p99 = tot_us[(size_t)(blocks * 0.99)];
  const double p999 = tot_us[(size_t)(blocks * 0.999)];
  int over = 0;
  for (int b = 0; b < blocks; ++b) if (tot_us[b] > kDeadline) ++over;

  printf("  %-22s mean %6.2f%%   p99 %7.2f%%   p99.9 %7.2f%%   max %8.2f%%   "
         "over deadline %5.2f%%\n",
         name, PCT(mean), PCT(p99), PCT(p999), PCT(tot_us[blocks - 1]),
         100.0 * over / blocks);
  printf("  %-22s   Prepare(): mean %6.2f%%  worst %8.2f%%\n\n",
         "", PCT(prep_mean), PCT(prep_worst));
}

int main(int argc, char **argv) {
  const int N = (argc > 1) ? atoi(argv[1]) : 8000;
  printf("Deadline for one 32-sample engine block at 32 kHz: %.3f ms\n"
         "(%d blocks measured per row)\n\n", 1e3 * kDeadline, N);
  run("Granular d=50%",  PLAYBACK_MODE_GRANULAR, 0.84f, 0.5f, N);
  run("Granular d=100%",  PLAYBACK_MODE_GRANULAR, 1.0f, 1.0f, N);
  run("Spectral",        PLAYBACK_MODE_SPECTRAL, 0.84f, 0.5f, N);
  run("Stretch",         PLAYBACK_MODE_STRETCH,  0.84f, 0.5f, N);
  return 0;
}
