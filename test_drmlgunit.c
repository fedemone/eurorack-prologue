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
 *   - parameter values *outside* the declared range leave the output finite
 *   - the note/gate/tempo/bend/pressure callbacks leave the output finite
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
typedef void (*noteoff_f)(uint8_t);
typedef void (*gateon_f)(uint8_t);
typedef void (*tempo_f)(uint32_t);
typedef void (*bend_f)(uint16_t);
typedef void (*pressure_f)(uint8_t);
typedef void (*aftertouch_f)(uint8_t, uint8_t);
typedef int32_t (*getp_f)(uint8_t);
typedef const char *(*getstr_f)(uint8_t, int32_t);
typedef const uint8_t *(*getbmp_f)(uint8_t, int32_t);
typedef uint8_t (*getpreset_f)(void);
typedef const char *(*presetname_f)(uint8_t);
typedef void (*loadpreset_f)(uint8_t);

typedef struct {
  const char *path;
  void *handle;
  const unit_header_t *hdr;
  render_f render;
  setp_f setp;
  noteon_f noteon;
  noteoff_f noteoff;
  gateon_f gateon;
  void_f gateoff;
  void_f all_note_off;
  tempo_f set_tempo;
  bend_f pitch_bend;
  pressure_f channel_pressure;
  aftertouch_f aftertouch;
  getp_f getp;
  getstr_f getstr;
  getbmp_f getbmp;
  getpreset_f get_preset_index;
  presetname_f get_preset_name;
  loadpreset_f load_preset;
  void_f reset;
  void_f resume;
  void_f suspend;
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
    "clouds_fx_request_reset",
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
  u->noteoff = (noteoff_f)dlsym(h, "unit_note_off");
  u->gateon = (gateon_f)dlsym(h, "unit_gate_on");
  u->gateoff = (void_f)dlsym(h, "unit_gate_off");
  u->all_note_off = (void_f)dlsym(h, "unit_all_note_off");
  u->set_tempo = (tempo_f)dlsym(h, "unit_set_tempo");
  u->pitch_bend = (bend_f)dlsym(h, "unit_pitch_bend");
  u->channel_pressure = (pressure_f)dlsym(h, "unit_channel_pressure");
  u->aftertouch = (aftertouch_f)dlsym(h, "unit_aftertouch");
  u->getp = (getp_f)dlsym(h, "unit_get_param_value");
  u->getstr = (getstr_f)dlsym(h, "unit_get_param_str_value");
  u->getbmp = (getbmp_f)dlsym(h, "unit_get_param_bmp_value");
  u->get_preset_index = (getpreset_f)dlsym(h, "unit_get_preset_index");
  u->get_preset_name = (presetname_f)dlsym(h, "unit_get_preset_name");
  u->load_preset = (loadpreset_f)dlsym(h, "unit_load_preset");
  u->reset = (void_f)dlsym(h, "unit_reset");
  u->resume = (void_f)dlsym(h, "unit_resume");
  u->suspend = (void_f)dlsym(h, "unit_suspend");
  init_f f_init = (init_f)dlsym(h, "unit_init");
  void_f f_reset = u->reset;
  void_f f_resume = u->resume;

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

/* ---- out-of-range parameter values ------------------------------------
 *
 * `unit_set_param_value()` takes an int32_t and the firmware pushes a value
 * for every one of the 24 slots, so what a unit receives is not guaranteed to
 * be inside the range it declared — a project recalled against a different
 * unit's slot layout is the obvious way to be handed someone else's number.
 * The units here cast that int32_t to uint16_t on the way to OSC_PARAM, so an
 * out-of-range value does not arrive small, it arrives huge: -100 becomes
 * 65436, and a parameter that exponentiates its argument turns that into
 * +inf and every later sample into NaN.
 *
 * NaN is the failure that does not stay local.  The drumlogue's send effects
 * are IIRs with feedback, so one NaN block silences the instrument until the
 * effect is rebuilt — including after the user has moved to another unit,
 * which is why this is worth a test of its own rather than a shrug about the
 * firmware clamping.  See drumlogue_guards.h.
 * ---------------------------------------------------------------------- */

static int s_oor_phase;

static int render_finite(unit_t *u, int blocks) {
  static float in[FRAMES * 2];
  static float out[FRAMES * 2];
  int ok = 1;
  for (int b = 0; b < blocks; ++b) {
    fill_input(in, s_oor_phase);
    s_oor_phase += FRAMES;
    u->render(in, out, FRAMES);
    for (int i = 0; i < FRAMES * 2; ++i)
      if (!isfinite(out[i])) ok = 0;
  }
  return ok;
}

static void set_all_params(unit_t *u, int which) {
  for (uint32_t p = 0; p < u->hdr->num_params; ++p)
    u->setp((uint8_t)p, which ? u->hdr->params[p].max : u->hdr->params[p].init);
}

static void probe_param_out_of_range(unit_t *u) {
  static const int32_t kWild[] = {-100000, -1024, -257, -128, -1,
                                  256,     1024,  65535, 65536, 100000};
  const size_t nwild = sizeof(kWild) / sizeof(*kWild);
  int nonfinite = 0;

  if (u->is_synth && u->noteon) u->noteon(60, 100);

  /* Every slot holding the same wild value: a project recalled against a
   * different unit's slot layout, in its crudest form. */
  for (size_t k = 0; k < nwild; ++k) {
    for (uint32_t p = 0; p < UNIT_MAX_PARAM_COUNT; ++p)
      u->setp((uint8_t)p, kWild[k]);
    if (!render_finite(u, 24)) nonfinite = 1;
    set_all_params(u, 0);
    (void)render_finite(u, 4);
  }

  /* One wild value at a time, against a background of every other parameter at
   * its maximum.  The background matters: a bad value often only shows through
   * a feature that is off at the defaults — mussola's LFO Rate, for instance,
   * is exponentiated into a frequency, but nothing reads that frequency until
   * LFO Shape and LFO Depth are non-zero. */
  for (uint32_t p = 0; p < UNIT_MAX_PARAM_COUNT; ++p) {
    for (size_t k = 0; k < nwild; ++k) {
      set_all_params(u, 1);
      if (u->is_synth && u->noteon) u->noteon(60, 100);
      u->setp((uint8_t)p, kWild[k]);
      /* A stuck LFO or filter state takes a few blocks to show, and what is
       * being checked is the state left behind, not the block it landed in. */
      if (!render_finite(u, 16)) nonfinite = 1;
    }
  }

  /* Back to a known state, and confirm the unit still works afterwards. */
  set_all_params(u, 0);
  if (u->is_synth && u->noteon) u->noteon(60, 100);
  if (!render_finite(u, 200)) nonfinite = 1;

  CHECK(!nonfinite, "%s: out-of-range parameter values stay finite",
        u->hdr->name);
}

/* ---- note / gate / tempo / expression callbacks ------------------------
 *
 * Everything the firmware can call that is not a parameter.  unit_set_tempo()
 * in particular reaches real DSP state — the Rings arpeggiator derives its
 * step length from it — and nothing else in this file calls it.
 * ---------------------------------------------------------------------- */

static void probe_event_callbacks(unit_t *u) {
  static float in[FRAMES * 2];
  static float out[FRAMES * 2];
  static const uint32_t kTempos[] = {0, 1, 20u << 16, 120u << 16, 480u << 16,
                                     0xFFFFFFFFu};
  int nonfinite = 0;
  int phase = 0;
  unsigned seed = 7;

  for (int step = 0; step < 400; ++step) {
    seed = seed * 1103515245u + 12345u;
    const unsigned r = seed >> 16;
    switch (r % 8) {
      case 0: if (u->noteon) u->noteon(r % 128, 1 + r % 127); break;
      case 1: if (u->noteoff) u->noteoff(r % 128); break;
      case 2: if (u->gateon) u->gateon(1 + r % 127); break;
      case 3: if (u->gateoff) u->gateoff(); break;
      case 4: if (u->all_note_off) u->all_note_off(); break;
      case 5:
        if (u->set_tempo)
          u->set_tempo(kTempos[r % (sizeof(kTempos) / sizeof(*kTempos))]);
        break;
      case 6: if (u->pitch_bend) u->pitch_bend(r % 0x4000); break;
      case 7:
        if (u->channel_pressure) u->channel_pressure(r % 128);
        if (u->aftertouch) u->aftertouch(r % 128, r % 128);
        break;
    }
    fill_input(in, phase);
    phase += FRAMES;
    u->render(in, out, FRAMES);
    for (int i = 0; i < FRAMES * 2; ++i)
      if (!isfinite(out[i])) nonfinite = 1;
  }

  if (u->set_tempo) u->set_tempo(120u << 16);
  if (u->all_note_off) u->all_note_off();
  CHECK(!nonfinite, "%s: note/gate/tempo/expression callbacks stay finite",
        u->hdr->name);
}

/* ---- UI / preset surface ----------------------------------------------
 *
 * The drumlogue calls these from its UI thread, and it calls them for values
 * the audio path never sees: it walks a strings parameter over its whole
 * declared [min,max] to draw the selector list, and it reads preset names
 * before any preset has been loaded.  A short string table behind a wider
 * declared range is an out-of-bounds read that only the device triggers, so
 * probe the full declared surface plus a margin either side.
 * ---------------------------------------------------------------------- */

/* A returned string must be NUL-terminated within a sane bound; anything
 * longer means we handed the firmware a pointer into unrelated memory. */
static int readable_string(const char *s, size_t limit) {
  for (size_t i = 0; i < limit; ++i)
    if (s[i] == '\0') return 1;
  return 0;
}

static void probe_ui_surface(unit_t *u) {
  int bad_str = 0, missing_str = 0, bad_preset = 0;

  for (uint32_t p = 0; p < u->hdr->num_params; ++p) {
    const int lo = u->hdr->params[p].min;
    const int hi = u->hdr->params[p].max;
    const int strings = u->hdr->params[p].type == k_unit_param_type_strings;

    /* Two values past each end: the firmware clamps, but a unit that indexes
     * a table without checking would fault here rather than on the device. */
    for (int v = lo - 2; v <= hi + 2; ++v) {
      if (u->getstr) {
        const char *s = u->getstr((uint8_t)p, v);
        if (s && !readable_string(s, 64)) bad_str = 1;
        /* Inside the declared range a strings parameter owes the UI a label. */
        if (strings && v >= lo && v <= hi && !s) missing_str = 1;
      }
      if (u->getbmp) (void)u->getbmp((uint8_t)p, v);
    }
    if (u->getp) (void)u->getp((uint8_t)p);
  }

  /* Parameter slots past num_params: the runtime pushes and reads all 24. */
  for (uint32_t p = u->hdr->num_params; p < UNIT_MAX_PARAM_COUNT; ++p) {
    if (u->getp) (void)u->getp((uint8_t)p);
    if (u->getstr) {
      const char *s = u->getstr((uint8_t)p, 0);
      if (s && !readable_string(s, 64)) bad_str = 1;
    }
    if (u->getbmp) (void)u->getbmp((uint8_t)p, 0);
  }

  CHECK(!bad_str, "%s: every param string is NUL-terminated", u->hdr->name);
  CHECK(!missing_str, "%s: strings params label their whole range",
        u->hdr->name);

  if (u->get_preset_index) (void)u->get_preset_index();
  for (int i = 0; i <= (int)u->hdr->num_presets + 1 && i < 256; ++i) {
    if (u->get_preset_name) {
      const char *s = u->get_preset_name((uint8_t)i);
      if (s && !readable_string(s, 64)) bad_preset = 1;
    }
    if (u->load_preset && i < (int)u->hdr->num_presets)
      u->load_preset((uint8_t)i);
  }
  CHECK(!bad_preset, "%s: every preset name is NUL-terminated", u->hdr->name);
}

/* ---- Density must stay out of Clouds' dead zone -----------------------
 *
 * Clouds' DENSITY is bipolar around 0.5 and schedules no grains at all
 * between 0.47 and 0.53 — a knob set to its own midpoint is silent.  The port
 * used to paper over that by asserting parameters.trigger on every block,
 * which seeds a grain per 32 samples: a block-rate buzz, and the grain pool
 * pinned at maximum however DENSITY was set.  Density now maps onto the
 * scheduler's usable range instead, so check both that the low end still
 * makes sound and that the knob actually changes the grain load.
 * ---------------------------------------------------------------------- */

static float render_rms(unit_t *u, int blocks, int *phase) {
  static float in[FRAMES * 2];
  static float out[FRAMES * 2];
  double sum = 0.0;
  for (int b = 0; b < blocks; ++b) {
    fill_input(in, *phase);
    *phase += FRAMES;
    memset(out, 0, sizeof(out));
    u->render(in, out, FRAMES);
    for (int i = 0; i < FRAMES * 2; ++i) sum += (double)out[i] * out[i];
  }
  return (float)sqrt(sum / (blocks * FRAMES * 2));
}

static void probe_density(unit_t *u) {
  int id = -1;
  for (uint32_t p = 0; p < u->hdr->num_params; ++p) {
    if (strcmp(u->hdr->params[p].name, "Density") == 0) { id = (int)p; break; }
  }
  if (id < 0) return;

  const int lo = u->hdr->params[id].min;
  const int hi = u->hdr->params[id].max;
  const int mid = lo + (hi - lo) / 2;
  int phase = 0;

  /* Dry signal would mask the result, so ask for as wet a path as the unit
   * offers; on the synth there is no dry path to begin with. */
  for (uint32_t p = 0; p < u->hdr->num_params; ++p)
    if (strcmp(u->hdr->params[p].name, "Dry/Wet") == 0)
      u->setp((uint8_t)p, u->hdr->params[p].max);
  if (u->is_synth && u->noteon) u->noteon(60, 100);

  float rms[3];
  const int vals[3] = {lo, mid, hi};
  for (int k = 0; k < 3; ++k) {
    u->setp((uint8_t)id, vals[k]);
    (void)render_rms(u, 200, &phase);      /* let the grain pool settle */
    rms[k] = render_rms(u, 400, &phase);
  }

  CHECK(rms[0] > 1e-4f, "%s: Density %d%% still sounds (rms %.5f)",
        u->hdr->name, lo, rms[0]);
  CHECK(rms[1] > 1e-4f, "%s: Density %d%% still sounds (rms %.5f)",
        u->hdr->name, mid, rms[1]);
  CHECK(rms[2] > rms[0], "%s: Density scales the cloud (%.5f -> %.5f)",
        u->hdr->name, rms[0], rms[2]);

  for (uint32_t p = 0; p < u->hdr->num_params; ++p)
    u->setp((uint8_t)p, u->hdr->params[p].init);
}

/* ---- control thread racing the audio thread ---------------------------
 *
 * On the device unit_set_param_value(), unit_reset(), unit_suspend() and
 * unit_resume() come from the UI/control thread while unit_render() runs on
 * the audio thread.  Anything a callback does that re-seats a buffer the
 * renderer is reading is a race the single-threaded checks above cannot see.
 * ---------------------------------------------------------------------- */

static unit_t *s_race_unit;
static volatile int s_race_stop;

static void *control_worker(void *arg) {
  (void)arg;
  unit_t *u = s_race_unit;
  unsigned seed = 12345;
  while (!s_race_stop) {
    for (uint32_t p = 0; p < u->hdr->num_params; ++p) {
      const int lo = u->hdr->params[p].min;
      const int hi = u->hdr->params[p].max;
      seed = seed * 1103515245u + 12345u;
      u->setp((uint8_t)p, lo + (int)((seed >> 16) % (unsigned)(hi - lo + 1)));
    }
    if (u->reset) u->reset();
    if (u->suspend) u->suspend();
    if (u->resume) u->resume();
  }
  return NULL;
}

static void race_control_thread(unit_t *u) {
  static float in[FRAMES * 2];
  static float out[FRAMES * 2];
  int nonfinite = 0;
  int phase = 0;

  s_race_unit = u;
  s_race_stop = 0;
  pthread_t th;
  if (pthread_create(&th, NULL, control_worker, NULL) != 0) return;

  if (u->is_synth && u->noteon) u->noteon(60, 100);
  for (int b = 0; b < 4000; ++b) {
    fill_input(in, phase);
    phase += FRAMES;
    u->render(in, out, FRAMES);
    for (int i = 0; i < FRAMES * 2; ++i)
      if (!isfinite(out[i])) nonfinite = 1;
  }

  s_race_stop = 1;
  pthread_join(th, NULL);

  /* Put the unit back in a known state for the checks that follow. */
  for (uint32_t p = 0; p < u->hdr->num_params; ++p)
    u->setp((uint8_t)p, u->hdr->params[p].init);
  if (u->resume) u->resume();

  CHECK(!nonfinite, "%s: survives concurrent control-thread traffic",
        u->hdr->name);
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

  printf("\n=== Density / grain scheduling ===\n");
  for (int i = 0; i < s_num_units; ++i) probe_density(&s_units[i]);

  printf("\n=== Out-of-range parameter values ===\n");
  for (int i = 0; i < s_num_units; ++i) probe_param_out_of_range(&s_units[i]);

  printf("\n=== Note / gate / tempo / expression callbacks ===\n");
  for (int i = 0; i < s_num_units; ++i) probe_event_callbacks(&s_units[i]);

  printf("\n=== UI / preset callbacks ===\n");
  for (int i = 0; i < s_num_units; ++i) probe_ui_surface(&s_units[i]);

  printf("\n=== Control thread racing audio thread ===\n");
  for (int i = 0; i < s_num_units; ++i) race_control_thread(&s_units[i]);

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
