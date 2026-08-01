/*
 * bench_units.c — per-render cost distribution for the shipped .drmlgunit
 * binaries, measured end to end through the drumlogue ABI.
 *
 * `make bench-clouds-spike` benches the Clouds engine directly.  This one is
 * a level up: it dlopens whole units and times `unit_render()` for exactly
 * the buffer the firmware asks for, so the number includes the wrapper, the
 * adapter's block buffering and every engine reconfiguration the unit does on
 * the audio thread.  That is the deadline that actually exists, and the only
 * scale on which two different units can be compared.
 *
 * Reports mean / p99 / p99.9 as a percentage of the render deadline
 * (frames / 48000), plus the fraction of renders over it.  Percentages are
 * relative, not absolute: QEMU is roughly an order of magnitude slower than
 * the SoC and does not model memory bandwidth.  See
 * docs/CLOUDS_DRUMLOGUE_AUDIO_NOTES.md for why the tail matters more than the
 * mean and why the maximum is not reported.
 *
 * Build for ARMv7-A and run under qemu-arm; see `make bench-units`.
 *
 * Usage: bench_units [-f frames] [-n renders] <unit.drmlgunit> [more units...]
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "runtime.h"

#define MAX_FRAMES 256

typedef int8_t (*init_f)(const unit_runtime_desc_t *);
typedef void (*void_f)(void);
typedef void (*render_f)(const float *, float *, uint32_t);
typedef void (*setp_f)(uint8_t, int32_t);
typedef void (*noteon_f)(uint8_t, uint8_t);

static float s_sample_data[4800 * 2];
static sample_wrapper_t s_sample;
static uint8_t hook_num_banks(void) { return 1; }
static uint8_t hook_num_samples(uint8_t b) { (void)b; return 1; }
static const sample_wrapper_t *hook_get_sample(uint8_t b, uint8_t i) {
  (void)b;
  (void)i;
  return &s_sample;
}

static int cmp_double(const void *a, const void *b) {
  double x = *(const double *)a, y = *(const double *)b;
  return (x > y) - (x < y);
}

static double now_us(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

int main(int argc, char **argv) {
  unsigned frames = 64;
  unsigned renders = 4000;
  int arg = 1;

  while (arg < argc && argv[arg][0] == '-') {
    if (!strcmp(argv[arg], "-f") && arg + 1 < argc) frames = atoi(argv[++arg]);
    else if (!strcmp(argv[arg], "-n") && arg + 1 < argc) renders = atoi(argv[++arg]);
    else { fprintf(stderr, "unknown option %s\n", argv[arg]); return 2; }
    ++arg;
  }
  if (arg >= argc) {
    fprintf(stderr, "usage: %s [-f frames] [-n renders] <unit.drmlgunit>...\n",
            argv[0]);
    return 2;
  }
  if (frames < 1 || frames > MAX_FRAMES) {
    fprintf(stderr, "frames must be 1..%d\n", MAX_FRAMES);
    return 2;
  }

  for (int i = 0; i < 4800; ++i) {
    float v = 0.5f * sinf(2.0f * 3.14159265f * 220.0f * i / 48000.0f);
    s_sample_data[i * 2] = v;
    s_sample_data[i * 2 + 1] = v;
  }
  memset(&s_sample, 0, sizeof(s_sample));
  s_sample.channels = 2;
  s_sample.frames = 4800;
  s_sample.sample_ptr = s_sample_data;
  snprintf(s_sample.name, sizeof(s_sample.name), "sine");

  const double deadline_us = frames * 1e6 / 48000.0;
  double *us = malloc(renders * sizeof(*us));
  static float in[MAX_FRAMES * 2], out[MAX_FRAMES * 2];

  printf("Per-render cost, %u frames (%.2f ms deadline), %u renders each\n",
         frames, deadline_us / 1000.0, renders);
  printf("Percentages are of the deadline.  Relative only -- QEMU.\n\n");
  printf("%-14s %8s %8s %8s %10s\n", "unit", "mean", "p99", "p99.9", "over");

  int rc = 0;
  for (; arg < argc; ++arg) {
    void *h = dlopen(argv[arg], RTLD_NOW | RTLD_LOCAL);
    if (!h) {
      fprintf(stderr, "dlopen %s: %s\n", argv[arg], dlerror());
      rc = 1;
      continue;
    }
    const unit_header_t *hdr = dlsym(h, "unit_header");
    init_f f_init = (init_f)dlsym(h, "unit_init");
    render_f f_render = (render_f)dlsym(h, "unit_render");
    setp_f f_setp = (setp_f)dlsym(h, "unit_set_param_value");
    noteon_f f_noteon = (noteon_f)dlsym(h, "unit_note_on");
    void_f f_reset = (void_f)dlsym(h, "unit_reset");
    void_f f_resume = (void_f)dlsym(h, "unit_resume");
    if (!hdr || !f_init || !f_render || !f_setp) {
      fprintf(stderr, "%s: missing unit ABI\n", argv[arg]);
      rc = 1;
      continue;
    }
    const int is_synth = (hdr->target & 0xFF) == k_unit_module_synth;

    unit_runtime_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.target = hdr->target;
    desc.api = UNIT_API_VERSION;
    desc.samplerate = 48000;
    desc.frames_per_buffer = frames;
    desc.input_channels = is_synth ? 0 : 2;
    desc.output_channels = 2;
    desc.get_num_sample_banks = hook_num_banks;
    desc.get_num_samples_for_bank = hook_num_samples;
    desc.get_sample = hook_get_sample;

    int8_t err = f_init(&desc);
    if (err != k_unit_err_none) {
      fprintf(stderr, "%s: unit_init -> %d\n", hdr->name, err);
      rc = 1;
      continue;
    }
    if (f_reset) f_reset();
    for (uint32_t i = 0; i < UNIT_MAX_PARAM_COUNT; ++i)
      f_setp((uint8_t)i, hdr->params[i].init);
    if (f_resume) f_resume();

    for (unsigned i = 0; i < frames; ++i) {
      float v = 0.6f * sinf(2.0f * 3.14159265f * 110.0f * i / 48000.0f);
      in[i * 2] = v;
      in[i * 2 + 1] = v;
    }

    /* Warm up: the first renders pay for whatever the unit defers to its
     * first audio block, which is a separate question from steady state. */
    if (is_synth && f_noteon) f_noteon(60, 100);
    for (unsigned b = 0; b < 200; ++b) f_render(in, out, frames);

    for (unsigned b = 0; b < renders; ++b) {
      /* Retrigger periodically so the unit is sounding, not decayed away. */
      if (is_synth && f_noteon && (b % 500) == 0) f_noteon(60, 100);
      double t0 = now_us();
      f_render(in, out, frames);
      us[b] = now_us() - t0;
    }

    double sum = 0.0;
    unsigned over = 0;
    for (unsigned b = 0; b < renders; ++b) {
      sum += us[b];
      if (us[b] > deadline_us) ++over;
    }
    qsort(us, renders, sizeof(*us), cmp_double);
    const double p99 = us[(unsigned)(renders * 0.99)];
    const double p999 = us[(unsigned)(renders * 0.999)];

    printf("%-14s %7.1f%% %7.1f%% %7.1f%% %9.2f%%\n", hdr->name,
           100.0 * (sum / renders) / deadline_us, 100.0 * p99 / deadline_us,
           100.0 * p999 / deadline_us, 100.0 * over / renders);
    fflush(stdout);
  }

  free(us);
  return rc;
}
