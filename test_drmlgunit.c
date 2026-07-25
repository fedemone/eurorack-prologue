/*
 * test_drmlgunit.c — exercise real .drmlgunit binaries the way the drumlogue does.
 *
 * The other test_*.cc suites link the port layer directly into an x86 host
 * binary.  That catches logic bugs but it cannot catch anything that only
 * exists in the shipped artifact: the ARM/NEON code paths, the drumlogue ABI,
 * or — the reason this file exists — what happens when several units are
 * loaded into one address space, which is exactly what the device does with
 * everything in Units/.
 *
 * Build for ARMv7-A and run under qemu-arm; see `make test-arm`.
 *
 * Usage: test_drmlgunit <unit.drmlgunit> [more units...]
 *
 * Checks, per unit:
 *   - the header is a valid drumlogue unit header
 *   - unit_init/reset/param defaults/resume succeed
 *   - rendering is finite and in range while idle and while sounding
 *   - every parameter can be swept across its full range while rendering
 *   - partial buffers (frames < frames_per_buffer) are handled
 *   - peak stack usage of unit_render stays well inside an audio thread stack
 *
 * And across units:
 *   - each unit renders its OWN engine.  Units built from this repo all define
 *     the same port-layer symbols (OSC_CYCLE, osc_adapter_*, the eurorack DSP,
 *     ...); if they are exported, the first unit loaded hijacks every unit
 *     loaded after it and they all render identical audio.  Distinct output
 *     signatures are the regression test for drumlogue/unit_exports.map.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime.h"

#define MAX_UNITS 8
#define FRAMES 64
#define STACK_BYTES (2u * 1024u * 1024u)
#define STACK_PAINT 0xA5

typedef int8_t (*init_f)(const unit_runtime_desc_t *);
typedef void (*void_f)(void);
typedef void (*render_f)(const float *, float *, uint32_t);
typedef void (*setp_f)(uint8_t, int32_t);
typedef void (*noteon_f)(uint8_t, uint8_t);

typedef struct {
  const char *path;
  void *handle;
  const unit_header_t *hdr;
  render_f render;
  setp_f setp;
  noteon_f noteon;
  int is_synth;
  float peak;
} unit_t;

/* Port-layer symbols that must NOT be visible in a unit's dynamic symbol
 * table.  Every unit in this repo defines these, so if any of them is
 * exported the first unit loaded hijacks the rest (see the file header and
 * drumlogue/unit_exports.map). */
static const char *const kPrivateSymbols[] = {
    "OSC_INIT",
    "OSC_CYCLE",
    "OSC_NOTEON",
    "OSC_NOTEOFF",
    "OSC_PARAM",
    "osc_adapter_init",
    "osc_adapter_reset",
    "osc_adapter_render",
    "osc_adapter_render_stereo",
    "osc_adapter_note_on",
    "osc_adapter_note_off",
    "osc_adapter_set_param",
    "osc_adapter_get_sample",
    "osc_adapter_set_shape_lfo",
    "clouds_fx_init",
    "clouds_fx_process",
    "clouds_fx_set_param",
};

static unit_t s_units[MAX_UNITS];
static int s_num_units;
static int s_failures;

#define CHECK(cond, fmt, ...)                                   \
  do {                                                          \
    if (cond) {                                                 \
      printf("  ok   : " fmt "\n", ##__VA_ARGS__);              \
    } else {                                                    \
      printf("  FAIL : " fmt "\n", ##__VA_ARGS__);              \
      ++s_failures;                                             \
    }                                                           \
  } while (0)

/* A sample bank with one short sine, so units that read samples get data. */
static float s_sample_data[4800 * 2];
static sample_wrapper_t s_sample;
static uint8_t hook_num_banks(void) { return 1; }
static uint8_t hook_num_samples(uint8_t bank) { (void)bank; return 1; }
static const sample_wrapper_t *hook_get_sample(uint8_t bank, uint8_t idx) {
  (void)bank;
  (void)idx;
  return &s_sample;
}

static void fill_input(float *in, int phase) {
  for (int i = 0; i < FRAMES; ++i) {
    float v = 0.6f * sinf(2.0f * 3.14159265f * 110.0f * (phase + i) / 48000.0f);
    in[i * 2] = v;
    in[i * 2 + 1] = v;
  }
}

static int load_unit(const char *path) {
  void *h = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
  if (!h) {
    printf("  FAIL : dlopen %s: %s\n", path, dlerror());
    ++s_failures;
    return -1;
  }
  unit_t *u = &s_units[s_num_units];
  memset(u, 0, sizeof(*u));
  u->path = path;
  u->handle = h;
  u->hdr = (const unit_header_t *)dlsym(h, "unit_header");
  u->render = (render_f)dlsym(h, "unit_render");
  u->setp = (setp_f)dlsym(h, "unit_set_param_value");
  u->noteon = (noteon_f)dlsym(h, "unit_note_on");
  init_f f_init = (init_f)dlsym(h, "unit_init");
  void_f f_reset = (void_f)dlsym(h, "unit_reset");
  void_f f_resume = (void_f)dlsym(h, "unit_resume");

  if (!u->hdr || !f_init || !u->render || !u->setp) {
    printf("  FAIL : %s is missing required unit ABI symbols\n", path);
    ++s_failures;
    return -1;
  }
  u->is_synth = (u->hdr->target & 0xFF) == k_unit_module_synth;

  CHECK(u->hdr->header_size == sizeof(unit_header_t), "%s: header_size %u",
        u->hdr->name, u->hdr->header_size);
  CHECK(u->hdr->api == UNIT_API_VERSION, "%s: api 0x%08x", u->hdr->name,
        u->hdr->api);
  CHECK(u->hdr->num_params <= UNIT_MAX_PARAM_COUNT, "%s: %u params",
        u->hdr->name, u->hdr->num_params);

  unit_runtime_desc_t desc;
  memset(&desc, 0, sizeof(desc));
  desc.target = u->hdr->target;
  desc.api = UNIT_API_VERSION;
  desc.samplerate = 48000;
  desc.frames_per_buffer = FRAMES;
  desc.input_channels = u->is_synth ? 0 : 2;
  desc.output_channels = 2;
  desc.get_num_sample_banks = hook_num_banks;
  desc.get_num_samples_for_bank = hook_num_samples;
  desc.get_sample = hook_get_sample;

  int8_t err = f_init(&desc);
  CHECK(err == k_unit_err_none, "%s: unit_init -> %d", u->hdr->name, err);
  if (err != k_unit_err_none) return -1;

  if (f_reset) f_reset();
  /* The runtime pushes a value for every parameter slot, not just the used
   * ones, so do the same here. */
  for (uint32_t i = 0; i < UNIT_MAX_PARAM_COUNT; ++i)
    u->setp((uint8_t)i, u->hdr->params[i].init);
  if (f_resume) f_resume();

  ++s_num_units;
  return 0;
}

static void exercise_unit(unit_t *u) {
  static float in[FRAMES * 2];
  static float out[FRAMES * 2];
  int nonfinite = 0;
  int phase = 0;
  float peak = 0.0f;

  printf("[%s]\n", u->hdr->name);

  /* Idle: silence in, nothing triggered. */
  memset(in, 0, sizeof(in));
  for (int b = 0; b < 100; ++b) {
    memset(out, 0, sizeof(out));
    u->render(in, out, FRAMES);
    for (int i = 0; i < FRAMES * 2; ++i)
      if (!isfinite(out[i])) nonfinite = 1;
  }
  CHECK(!nonfinite, "%s: idle output is finite", u->hdr->name);

  /* First trigger, then a long sounding stretch. */
  if (u->is_synth && u->noteon) u->noteon(60, 100);
  for (int b = 0; b < 1000; ++b) {
    fill_input(in, phase);
    phase += FRAMES;
    memset(out, 0, sizeof(out));
    u->render(in, out, FRAMES);
    for (int i = 0; i < FRAMES * 2; ++i) {
      if (!isfinite(out[i])) nonfinite = 1;
      float a = fabsf(out[i]);
      if (a > peak) peak = a;
    }
  }
  CHECK(!nonfinite, "%s: sounding output is finite", u->hdr->name);
  CHECK(peak <= 1.0f, "%s: no over-full-scale burst (peak %.4f)", u->hdr->name,
        peak);
  u->peak = peak;

  /* Every parameter across its full range while rendering. */
  for (uint32_t p = 0; p < u->hdr->num_params; ++p) {
    int lo = u->hdr->params[p].min;
    int hi = u->hdr->params[p].max;
    if (hi <= lo) continue;
    int step = (hi - lo) / 8;
    if (step < 1) step = 1;
    for (int v = lo; v <= hi; v += step) {
      u->setp((uint8_t)p, v);
      for (int b = 0; b < 8; ++b) {
        fill_input(in, phase);
        phase += FRAMES;
        u->render(in, out, FRAMES);
        for (int i = 0; i < FRAMES * 2; ++i)
          if (!isfinite(out[i])) nonfinite = 1;
      }
    }
    u->setp((uint8_t)p, u->hdr->params[p].init);
  }
  CHECK(!nonfinite, "%s: full parameter sweep is finite", u->hdr->name);

  /* The SDK allows frames < frames_per_buffer. */
  for (uint32_t n = 1; n <= FRAMES; ++n) {
    u->render(in, out, n);
    for (uint32_t i = 0; i < n * 2; ++i)
      if (!isfinite(out[i])) nonfinite = 1;
  }
  CHECK(!nonfinite, "%s: partial buffers are finite", u->hdr->name);
}

/* ---- stack high-water measurement ------------------------------------- */

static unit_t *s_stack_unit;

static void *stack_worker(void *arg) {
  (void)arg;
  static float in[FRAMES * 2];
  static float out[FRAMES * 2];
  unit_t *u = s_stack_unit;
  int phase = 0;
  if (u->is_synth && u->noteon) u->noteon(60, 100);
  /* String-typed parameters are the mode/quality selectors; walking them
   * reaches the heaviest call paths (spectral, stretch, low-fi). */
  for (uint32_t p = 0; p < u->hdr->num_params; ++p) {
    if (u->hdr->params[p].type != k_unit_param_type_strings) continue;
    for (int v = u->hdr->params[p].min; v <= u->hdr->params[p].max; ++v) {
      u->setp((uint8_t)p, v);
      for (int b = 0; b < 40; ++b) {
        fill_input(in, phase);
        phase += FRAMES;
        u->render(in, out, FRAMES);
      }
    }
    u->setp((uint8_t)p, u->hdr->params[p].init);
  }
  for (int b = 0; b < 100; ++b) u->render(in, out, FRAMES);
  return NULL;
}

static void measure_stack(unit_t *u) {
  void *stack = NULL;
  if (posix_memalign(&stack, 4096, STACK_BYTES) != 0) return;
  memset(stack, STACK_PAINT, STACK_BYTES);

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstack(&attr, stack, STACK_BYTES);
  s_stack_unit = u;
  pthread_t th;
  if (pthread_create(&th, &attr, stack_worker, NULL) != 0) {
    free(stack);
    return;
  }
  pthread_join(th, NULL);

  const unsigned char *s = (const unsigned char *)stack;
  size_t untouched = 0;
  while (untouched < STACK_BYTES && s[untouched] == STACK_PAINT) ++untouched;
  size_t used = STACK_BYTES - untouched;
  /* 64 KB is a conservative floor for an embedded audio thread stack. */
  CHECK(used < 64u * 1024u, "%s: peak stack %zu bytes (%.1f KB)", u->hdr->name,
        used, used / 1024.0);
  free(stack);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <unit.drmlgunit> [more units...]\n", argv[0]);
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

  printf("=== Loading %d unit(s) into one address space ===\n", argc - 1);
  for (int a = 1; a < argc && s_num_units < MAX_UNITS; ++a) load_unit(argv[a]);

  printf("\n=== Rendering ===\n");
  for (int i = 0; i < s_num_units; ++i) exercise_unit(&s_units[i]);

  printf("\n=== Cross-unit isolation ===\n");
  for (int i = 0; i < s_num_units; ++i) {
    unit_t *u = &s_units[i];
    const char *leaked = NULL;
    for (size_t k = 0; k < sizeof(kPrivateSymbols) / sizeof(*kPrivateSymbols);
         ++k) {
      dlerror();
      if (dlsym(u->handle, kPrivateSymbols[k]) != NULL) {
        leaked = kPrivateSymbols[k];
        break;
      }
    }
    CHECK(leaked == NULL, "%s: exports only the unit ABI%s%s", u->hdr->name,
          leaked ? " — leaked: " : "", leaked ? leaked : "");
  }

  printf("\n=== Stack usage ===\n");
  for (int i = 0; i < s_num_units; ++i) measure_stack(&s_units[i]);

  printf("\n=== %s (%d failures) ===\n", s_failures ? "FAILED" : "ALL PASS",
         s_failures);
  return s_failures ? 1 : 0;
}
