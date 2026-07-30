/*
 * File: bench_clouds_spike.cc
 *
 * Where does Spectral's cost sit: spread out, or concentrated in one block?
 *
 * This is the harness behind the freeze diagnosis in
 * docs/CLOUDS_DRUMLOGUE_AUDIO_NOTES.md. Spectral's *average* cost is the
 * second-lowest of the four modes, which is why looking at means led nowhere
 * for a long time. Nearly all of it lands in one block out of 32 -- the phase
 * vocoder's hop -- where a 4096-point FFT, the spectral modifier and a full
 * inverse run back to back, twice over in stereo, inside a single audio
 * block. So this reports the worst single block and the fraction of blocks
 * that miss the deadline alongside the mean; the mean on its own says
 * nothing about whether the unit crackles.
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

  double mean = 0, worst = 0, prep_worst = 0, prep_mean = 0, spike_sum = 0;
  int over = 0, spikes = 0;
  for (int b = 0; b < blocks; ++b) {
    const double t0 = now_s();
    pr.Prepare();
    const double t1 = now_s();
    pr.Process(in, out, kMaxBlockSize);
    const double t2 = now_s();
    const double prep = t1 - t0, tot = t2 - t0;
    prep_mean += prep; mean += tot;
    prep_worst = std::max(prep_worst, prep);
    worst = std::max(worst, tot);
    if (tot > kDeadline) ++over;
    /* A "spike" is a Prepare() costing more than a whole ordinary block. */
    if (prep > kDeadline) { ++spikes; spike_sum += prep; }
  }
  mean /= blocks; prep_mean /= blocks;
  printf("  %-22s mean %6.2f%%  worst block %8.2f%%  over deadline %5.2f%% of blocks\n",
         name, PCT(mean), PCT(worst), 100.0 * over / blocks);
  printf("  %-22s   of which Prepare(): mean %6.2f%%  worst %8.2f%%",
         "", PCT(prep_mean), PCT(prep_worst));
  if (spikes)
    printf("   (%d spikes in %d blocks = 1 per %.0f, avg %.2f ms)",
           spikes, blocks, (double)blocks / spikes, 1e3 * spike_sum / spikes);
  printf("\n\n");
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
