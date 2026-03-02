/*
 * File: bench_render.cc
 *
 * Benchmark: measures host-side render throughput through the full
 * drumlogue wrapper chain (unit_init -> unit_note_on -> unit_render).
 *
 * Reports:
 *   - Microseconds per 64-frame block
 *   - Real-time ratio (how much faster than real-time)
 *   - Estimated CPU% on Cortex-A7 @ 1 GHz (rough scaling factor)
 *
 * Build:
 *   g++ -std=c++11 -O2 -DTEST -DBLOCKSIZE=24 -DOSC_VA \
 *       -DOSC_NATIVE_BLOCK_SIZE=24 -Idrumlogue -I. -Ieurorack \
 *       bench_render.cc drumlogue_osc_adapter.cc \
 *       drumlogue_unit_wrapper.cc header.c macro-oscillator2.cc \
 *       eurorack/plaits/dsp/engine/virtual_analog_engine.cc \
 *       eurorack/stmlib/dsp/units.cc \
 *       -o bench_render -lm
 *
 * Run:
 *   ./bench_render
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>

#include "runtime.h"
#include "unit.h"

static unit_runtime_desc_t make_valid_desc(void) {
  unit_runtime_desc_t desc;
  memset(&desc, 0, sizeof(desc));
  desc.target = k_unit_target_drumlogue_synth;
  desc.api = k_unit_api_2_0_0;
  desc.samplerate = 48000;
  desc.frames_per_buffer = 64;
  desc.input_channels = 0;
  desc.output_channels = 2;
  return desc;
}

/* Portable high-resolution clock (POSIX) */
static double get_time_sec(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

int main(void) {
  printf("\n=== Render Benchmark (Plaits VirtualAnalogEngine) ===\n\n");

  /* Init */
  unit_runtime_desc_t desc = make_valid_desc();
  int8_t err = unit_init(&desc);
  if (err != k_unit_err_none) {
    printf("unit_init failed: %d\n", err);
    return 1;
  }

  /* Set params for active sound */
  unit_set_param_value(0, 50); /* Shape */
  unit_set_param_value(1, 50); /* ShiftShape */
  unit_set_param_value(2, 50); /* Param 1 */
  unit_set_param_value(3, 50); /* Param 2 */

  /* Note on */
  unit_note_on(69, 127);

  /* Warm up (discard first 100 blocks) */
  const uint32_t frames_per_block = 64;
  float stereo[frames_per_block * 2];
  for (int i = 0; i < 100; ++i) {
    unit_render(nullptr, stereo, frames_per_block);
  }

  /* Benchmark: render N blocks, measure wall time */
  const int num_blocks = 10000;
  const double total_seconds_of_audio =
    (double)(num_blocks * frames_per_block) / 48000.0;

  double t_start = get_time_sec();
  for (int i = 0; i < num_blocks; ++i) {
    unit_render(nullptr, stereo, frames_per_block);
  }
  double t_end = get_time_sec();
  double elapsed = t_end - t_start;

  /* Results */
  double us_per_block = (elapsed / num_blocks) * 1e6;
  double realtime_ratio = total_seconds_of_audio / elapsed;

  /* At 48 kHz, a 64-frame block = 1333.3 us of real-time audio.
   * CPU% = (render_time / realtime_duration) * 100 */
  double realtime_block_us = (double)frames_per_block / 48000.0 * 1e6;
  double cpu_pct_host = (us_per_block / realtime_block_us) * 100.0;

  printf("  Blocks rendered:           %d\n", num_blocks);
  printf("  Frames per block:          %u\n", frames_per_block);
  printf("  Total audio duration:      %.2f sec\n", total_seconds_of_audio);
  printf("  Wall-clock time:           %.4f sec\n", elapsed);
  printf("  Time per block:            %.1f us\n", us_per_block);
  printf("  Real-time ratio:           %.1fx\n", realtime_ratio);
  printf("  CPU%% (host):               %.1f%%\n", cpu_pct_host);
  printf("\n");

  /*
   * Cortex-A7 @ 1 GHz scaling estimate.
   * A rough scaling factor: x86-64 at -O2 is typically 3-5x faster than
   * Cortex-A7 for floating-point DSP (due to wider pipelines, out-of-order
   * execution, and higher clock speeds). NEON SIMD partially compensates.
   * We use a conservative 4x factor.
   */
  const double cortex_a7_factor = 4.0;
  double est_cpu_pct_a7 = cpu_pct_host * cortex_a7_factor;
  printf("  Estimated CPU%% (A7 @1GHz): ~%.0f%% (%.0fx scaling factor)\n",
         est_cpu_pct_a7, cortex_a7_factor);
  printf("  Note: Actual A7 performance depends on cache, NEON usage,\n");
  printf("        and compiler optimization. Measure on hardware for accuracy.\n");

  printf("\n=== Benchmark complete ===\n\n");

  unit_teardown();
  return 0;
}
