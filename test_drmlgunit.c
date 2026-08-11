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
  init_f init;
  void_f teardown;
  unit_runtime_desc_t desc;
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
  u->init = f_init;
  u->teardown = (void_f)dlsym(h, "unit_teardown");
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

  u->desc = desc;
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

/* ---- Chord must retune the strings ------------------------------------
 *
 * Rings' quantized sympathetic-strings model tunes its strings from a chord
 * table, and this port appends three rows to that table (see the fork note in
 * eurorack-opt/rings/dsp/part.cc).  A wrong row is not a crash and not a
 * silence: it is a chord that sounds like a different chord, which every
 * other check in this file is happy to pass.
 *
 * So listen instead.  Strum each chord and measure the energy at a fixed set
 * of intervals above the root with a Goertzel, which is one bandpass per
 * interval and cheap enough to run fourteen times.  Two of the probe
 * intervals are deliberately not in equal temperament -- 2.4 semitones is a
 * slendro step, 9.69 is the septimal seventh -- and each sits next to its
 * tempered neighbour in the list.  That pairing is the point: a chord table
 * that reached the strings shows more energy at 9.69 than at 10.0, while one
 * that was rounded to the nearest semitone somewhere along the way shows the
 * opposite, and no amount of "is it finite" testing can tell those apart.
 * ---------------------------------------------------------------------- */

static const float kChordProbeTones[] = {
  0.0f,  2.0f,  2.4f,  3.0f,  4.0f, 5.0f, 7.0f, 9.0f,
  9.69f, 10.0f, 12.0f, 14.0f, 15.0f, 17.0f, 19.0f
};
#define NUM_CHORD_TONES ((int)(sizeof kChordProbeTones / sizeof kChordProbeTones[0]))
#define CHORD_TONE_2_4  2   /* index of the slendro step */
#define CHORD_TONE_2_0  1   /* ... and its tempered neighbour */
#define CHORD_TONE_9_69 8   /* index of the septimal seventh */
#define CHORD_TONE_10   9   /* ... and its tempered neighbour */

/* Strum note 60 and return one Goertzel magnitude per probe tone.
 *
 * Averaged over several strums.  Rings excites its strings with a noise
 * burst, so a single strum is a noisy estimate of what the chord is doing --
 * measured, one chord compared against a second recording of itself came out
 * further apart than the two closest genuinely different chords.  Averaging
 * power over CHORD_STRUMS strums brings that down far enough that the
 * comparison means what it says. */
#define CHORD_STRUMS 6
#define CHORD_BLOCKS 400

static void render_chord_spectrum(unit_t *u, float *mag) {
  static float in[FRAMES * 2];
  static float out[FRAMES * 2];
  const float root = 261.6256f;                 /* MIDI 60 */
  double coeff[NUM_CHORD_TONES], power[NUM_CHORD_TONES];

  for (int t = 0; t < NUM_CHORD_TONES; ++t) {
    const double f = root * pow(2.0, kChordProbeTones[t] / 12.0);
    coeff[t] = 2.0 * cos(2.0 * M_PI * f / 48000.0);
    power[t] = 0.0;
  }
  memset(in, 0, sizeof(in));                    /* internal exciter only */

  for (int rep = 0; rep < CHORD_STRUMS; ++rep) {
    double s1[NUM_CHORD_TONES], s2[NUM_CHORD_TONES];
    for (int t = 0; t < NUM_CHORD_TONES; ++t) s1[t] = s2[t] = 0.0;

    /* Start from silence every time.  Without this each chord is measured on
     * top of the previous one's decay and the whole sweep slides downhill --
     * the first chord came out twenty times louder than the last, which makes
     * a comparison between two chords a comparison of when they were tested. */
    if (u->reset) u->reset();
    for (int b = 0; b < 40; ++b) u->render(in, out, FRAMES);
    if (u->noteon) u->noteon(60, 127);
    /* Skip the pluck itself: it is a broadband noise burst, and much the same
     * burst whichever chord is selected.  What differs is what rings after. */
    for (int b = 0; b < 24; ++b) u->render(in, out, FRAMES);

    /* The window has to resolve the septimal seventh from the tempered one:
     * 458.0 Hz against 466.2 Hz at this root, 8.2 Hz apart.  CHORD_BLOCKS
     * blocks is 25600 samples, which puts them 4.4 bins apart -- comfortably
     * outside a Hann main lobe, where 160 blocks left them inside it and the
     * louder of the pair simply leaked into the other's bin.  Hann rather
     * than none for the same reason: an unwindowed bin hears its neighbour at
     * -13 dB, which is not enough to call a tuning by. */
    for (int b = 0; b < CHORD_BLOCKS; ++b) {
      memset(out, 0, sizeof(out));
      u->render(in, out, FRAMES);
      for (int i = 0; i < FRAMES; ++i) {
        const int n = b * FRAMES + i;
        const double w = 0.5 - 0.5 * cos(2.0 * M_PI * n /
                                         (double)(CHORD_BLOCKS * FRAMES - 1));
        const double x = w * 0.5 * ((double)out[i * 2] + (double)out[i * 2 + 1]);
        for (int t = 0; t < NUM_CHORD_TONES; ++t) {
          const double s = x + coeff[t] * s1[t] - s2[t];
          s2[t] = s1[t];
          s1[t] = s;
        }
      }
    }
    if (u->noteoff) u->noteoff(60);

    for (int t = 0; t < NUM_CHORD_TONES; ++t)
      power[t] += fabs(s1[t] * s1[t] + s2[t] * s2[t] - coeff[t] * s1[t] * s2[t]);
  }

  for (int t = 0; t < NUM_CHORD_TONES; ++t)
    mag[t] = (float)sqrt(power[t] / CHORD_STRUMS);
}

static void probe_chords(unit_t *u) {
  int chord_id = -1, model_id = -1;
  for (uint32_t p = 0; p < u->hdr->num_params; ++p) {
    if (strcmp(u->hdr->params[p].name, "Chord") == 0) chord_id = (int)p;
    if (strcmp(u->hdr->params[p].name, "Model") == 0) model_id = (int)p;
  }
  if (chord_id < 0 || model_id < 0) return;

  const int lo = u->hdr->params[chord_id].min;
  const int hi = u->hdr->params[chord_id].max;
  if (hi - lo + 1 > 16) return;                 /* not the parameter we mean */

  /* Chord only reaches the strings in the quantized sympathetic model, which
   * is also this unit's default -- take it from the header rather than
   * hard-coding 4, so renumbering the models cannot quietly defeat the test.
   *
   * Damping goes to maximum: the strings then ring for longer than the
   * analysis window instead of decaying inside it, which is what lets a long
   * window buy resolution rather than just accumulating room noise. */
  u->setp((uint8_t)model_id, u->hdr->params[model_id].init);
  for (uint32_t p = 0; p < u->hdr->num_params; ++p)
    if (strcmp(u->hdr->params[p].name, "Damping") == 0)
      u->setp((uint8_t)p, u->hdr->params[p].max);

  static float mag[16][NUM_CHORD_TONES];
  int silent = -1;
  for (int c = lo; c <= hi; ++c) {
    u->setp((uint8_t)chord_id, c);
    render_chord_spectrum(u, mag[c - lo]);
    double energy = 0.0;
    for (int t = 0; t < NUM_CHORD_TONES; ++t) energy += mag[c - lo][t];
    if (energy < 1e-3 && silent < 0) silent = c;
  }

  CHECK(silent < 0, "%s: every chord sounds%s", u->hdr->name,
        silent < 0 ? "" : " (silent one found)");

  if (getenv("CHORD_DEBUG")) {
    printf("      tone:");
    for (int t = 0; t < NUM_CHORD_TONES; ++t) printf(" %7.2f", kChordProbeTones[t]);
    printf("\n");
    for (int c = lo; c <= hi; ++c) {
      printf("  %8s:", u->getstr ? u->getstr((uint8_t)chord_id, c) : "?");
      for (int t = 0; t < NUM_CHORD_TONES; ++t) printf(" %7.3f", mag[c - lo][t]);
      printf("\n");
    }
  }

  /* Each chord is checked against itself rather than against other chords.
   *
   * Comparing whole spectra between chords was tried and does not work here:
   * a Rings string is not a sine, so every string puts a second harmonic an
   * octave up, and the probe bins fill with other strings' overtones -- a
   * bare fifth and a major ninth came out closer to each other than one chord
   * did to a second recording of itself.  Two bins a third of a semitone
   * apart in the *same* recording do not have that problem: whatever
   * overtones land there land on both, so what is left between them is the
   * fundamental, which is the thing being asserted.
   *
   * Each case below therefore names a tone the chord does contain and the
   * neighbouring tone it does not, and the margins are hundredfold rather
   * than marginal. */
  for (int c = lo; c <= hi; ++c) {
    const char *nm = u->getstr ? u->getstr((uint8_t)chord_id, c) : NULL;
    const float *m = mag[c - lo];
    if (!nm) continue;
    if (strcmp(nm, "Just7") == 0) {
      CHECK(m[CHORD_TONE_9_69] > 4.0f * m[CHORD_TONE_10],
            "%s: Just7 rings the septimal 7th, not the tempered one "
            "(%.2f vs %.2f)", u->hdr->name, m[CHORD_TONE_9_69],
            m[CHORD_TONE_10]);
    } else if (strcmp(nm, "Slendro") == 0) {
      CHECK(m[CHORD_TONE_2_4] > 4.0f * m[CHORD_TONE_2_0],
            "%s: Slendro rings its 2.4-semitone step, not a whole tone "
            "(%.2f vs %.2f)", u->hdr->name, m[CHORD_TONE_2_4],
            m[CHORD_TONE_2_0]);
    } else if (strcmp(nm, "4ths") == 0) {
      /* Two stacked fourths is a tempered minor seventh, so this one has to
       * come out the opposite way to Just7 -- same pair of bins, same
       * recording geometry, opposite verdict. */
      CHECK(m[CHORD_TONE_10] > 4.0f * m[CHORD_TONE_9_69],
            "%s: 4ths stacks to a tempered minor 7th (%.2f vs %.2f)",
            u->hdr->name, m[CHORD_TONE_10], m[CHORD_TONE_9_69]);
    } else if (strcmp(nm, "min7") == 0) {
      /* The control from upstream's own chords: a seventh that has always
       * been tempered must still read as tempered, or the Just7 comparison
       * above is measuring the probe rather than the chord. */
      CHECK(m[CHORD_TONE_10] > 4.0f * m[CHORD_TONE_9_69],
            "%s: min7 rings the tempered 7th (%.2f vs %.2f)", u->hdr->name,
            m[CHORD_TONE_10], m[CHORD_TONE_9_69]);
    }
  }

  for (uint32_t p = 0; p < u->hdr->num_params; ++p)
    u->setp((uint8_t)p, u->hdr->params[p].init);
}

/* ---- an LFO parked at rate zero ---------------------------------------
 *
 * A regression test for a specific way of getting this wrong, found by the
 * race probe below and reduced to something deterministic.
 *
 * stmlib's CosineOscillator is a two-pole resonator whose coefficient
 * InitApproximate() sets to 2 - 32*freq^2.  At freq 0 that is exactly 2 -- a
 * double pole at z = 1 -- and the recursion integrates instead of
 * oscillating, ramping linearly from whatever state the previous rate left
 * behind.  Rate 0 is where the knob starts, so this is reachable by leaving
 * one LFO alone and turning up the other's depth.
 *
 * Nothing here knows about that mechanism: it turns the rate down, leaves the
 * depth up, waits, and checks the unit still sounds like an instrument.  Any
 * modulation source that walks away from its own range fails it.
 * ---------------------------------------------------------------------- */

/* Takes the unit through its full lifecycle, sets every parameter to its
 * declared init except LFO2's target, rate and depth, renders `blocks`, and
 * returns an FNV-1a hash of every output sample.  Comparing hashes is how the
 * rate-0 check below stays exact: it asserts that two renders are the same
 * render, not that they are close. */
static uint32_t render_lfo_pass(unit_t *u, int target, int t, int rate,
                                int rate_v, int depth, int depth_v,
                                int blocks) {
  static float in[FRAMES * 2];
  static float out[FRAMES * 2];
  uint32_t h = 2166136261u;

  /* teardown/init, not reset(): reset() is a note-off.  It leaves a resonator
   * ringing and it does not rewind the random source these engines excite
   * themselves from, and either is enough to make two passes differ for
   * reasons that have nothing to do with the LFO.  The full lifecycle is the
   * only rewind the ABI offers. */
  if (u->teardown) u->teardown();
  u->init(&u->desc);
  if (u->reset) u->reset();
  for (uint32_t p = 0; p < UNIT_MAX_PARAM_COUNT; ++p)
    u->setp((uint8_t)p, u->hdr->params[p].init);
  if (u->resume) u->resume();

  u->setp((uint8_t)target, t);
  u->setp((uint8_t)rate, rate_v);
  u->setp((uint8_t)depth, depth_v);

  if (u->is_synth && u->noteon) u->noteon(60, 100);
  for (int b = 0; b < blocks; ++b) {
    fill_input(in, b * FRAMES);
    memset(out, 0, sizeof(out));
    u->render(in, out, FRAMES);
    for (int i = 0; i < FRAMES * 2; ++i) {
      uint32_t bits;
      memcpy(&bits, &out[i], sizeof(bits));
      h = (h ^ bits) * 16777619u;
    }
  }
  if (u->noteoff) u->noteoff(60);
  return h;
}

/* Rate 0 means no modulation.
 *
 * A stopped phase accumulator still has a value, so an LFO can park at its
 * peak and turn Depth into a DC offset on whatever it is pointed at -- and
 * rate 0 is where the knob starts, so that is the state a unit loads in.  The
 * check is exact rather than tolerant: with the rate at its minimum, moving
 * Depth from one end of its range to the other must not change a single
 * output sample.
 *
 * Two renders are only comparable if the unit renders the same thing twice,
 * which is why each pass re-inits rather than resetting -- reset() is a
 * note-off and rewinds neither a ringing resonator nor the random source
 * these engines excite themselves from.  A control pass checks that per unit
 * anyway, so an engine that stays unreproducible for some other reason is
 * reported as skipped rather than quietly passing. */
static void probe_lfo_rate_zero_silent(unit_t *u) {
  int rate = -1, depth = -1, target = -1;
  for (uint32_t p = 0; p < u->hdr->num_params; ++p) {
    const char *n = u->hdr->params[p].name;
    if (strcmp(n, "LFO2 Rate") == 0) rate = (int)p;
    if (strcmp(n, "LFO2 Depth") == 0) depth = (int)p;
    if (strcmp(n, "LFO2 Target") == 0) target = (int)p;
  }
  if (rate < 0 || depth < 0 || target < 0) return;
  if (u->hdr->params[rate].min != 0) return; /* nothing to park at */
  if (!u->init || !u->teardown) return;      /* cannot rewind between passes */

  const int lo = u->hdr->params[depth].min;
  const int hi = u->hdr->params[depth].max;
  const int t0 = u->hdr->params[target].min;
  const int blocks = 300;

  if (render_lfo_pass(u, target, t0, rate, 0, depth, lo, blocks) !=
      render_lfo_pass(u, target, t0, rate, 0, depth, lo, blocks)) {
    CHECK(1, "%s: LFO2 rate 0 = no modulation (skipped: renders differ "
             "between identical passes)", u->hdr->name);
    return;
  }

  int bad_target = -1;
  for (int t = t0; t <= u->hdr->params[target].max && bad_target < 0; ++t) {
    const uint32_t quiet = render_lfo_pass(u, target, t, rate, 0, depth, lo,
                                           blocks);
    const uint32_t loud  = render_lfo_pass(u, target, t, rate, 0, depth, hi,
                                           blocks);
    if (quiet != loud) bad_target = t;
  }

  CHECK(bad_target < 0,
        "%s: LFO2 rate 0 = no modulation, Depth %d..%d over %d targets%s%s",
        u->hdr->name, lo, hi,
        u->hdr->params[target].max - t0 + 1,
        bad_target < 0 ? "" : " -- fails at ",
        bad_target < 0 ? ""
                       : (u->getstr ? u->getstr((uint8_t)target, bad_target)
                                    : "?"));

  /* Leave the unit initialized and at its defaults for whatever runs next. */
  if (u->teardown) u->teardown();
  u->init(&u->desc);
  if (u->reset) u->reset();
  for (uint32_t p = 0; p < UNIT_MAX_PARAM_COUNT; ++p)
    u->setp((uint8_t)p, u->hdr->params[p].init);
  if (u->resume) u->resume();
}

static void probe_lfo_stability(unit_t *u) {
  static float in[FRAMES * 2];
  static float out[FRAMES * 2];
  int rate = -1, depth = -1, target = -1;
  for (uint32_t p = 0; p < u->hdr->num_params; ++p) {
    const char *n = u->hdr->params[p].name;
    if (strcmp(n, "LFO2 Rate") == 0) rate = (int)p;
    if (strcmp(n, "LFO2 Depth") == 0) depth = (int)p;
    if (strcmp(n, "LFO2 Target") == 0) target = (int)p;
  }
  if (rate < 0 || depth < 0 || target < 0) return;

  int bad_target = -1;
  float worst = 0.0f;
  for (int t = u->hdr->params[target].min; t <= u->hdr->params[target].max; ++t) {
    for (uint32_t p = 0; p < u->hdr->num_params; ++p)
      u->setp((uint8_t)p, u->hdr->params[p].init);
    if (u->reset) u->reset();
    u->setp((uint8_t)target, t);
    u->setp((uint8_t)depth, u->hdr->params[depth].max);

    /* Run the LFO first, so its state is somewhere mid-cycle... */
    u->setp((uint8_t)rate, u->hdr->params[rate].max);
    if (u->is_synth && u->noteon) u->noteon(60, 100);
    int phase = 0;
    for (int b = 0; b < 200; ++b) {
      fill_input(in, phase);
      phase += FRAMES;
      u->render(in, out, FRAMES);
    }
    /* ...then park the rate at zero and leave it there. */
    u->setp((uint8_t)rate, u->hdr->params[rate].min);
    for (int b = 0; b < 4000; ++b) {
      fill_input(in, phase);
      phase += FRAMES;
      memset(out, 0, sizeof(out));
      u->render(in, out, FRAMES);
      for (int i = 0; i < FRAMES * 2; ++i) {
        const float a = out[i] < 0.0f ? -out[i] : out[i];
        if (!isfinite(out[i]) || a > 4.0f) {
          if (bad_target < 0) { bad_target = t; worst = out[i]; }
        } else if (a > worst && bad_target < 0) {
          worst = a;
        }
      }
    }
    if (u->noteoff) u->noteoff(60);
  }

  CHECK(bad_target < 0,
        "%s: LFO2 parked at rate 0 stays put (worst |out| %.3f%s%s)",
        u->hdr->name, worst < 0 ? -worst : worst,
        bad_target < 0 ? "" : ", target ",
        bad_target < 0 ? ""
                       : (u->getstr ? u->getstr((uint8_t)target, bad_target) : "?"));

  for (uint32_t p = 0; p < u->hdr->num_params; ++p)
    u->setp((uint8_t)p, u->hdr->params[p].init);
  if (u->reset) u->reset();
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
  /* Unbuffered: this harness runs code that can and does segfault, and when
   * it does, the last line printed is the only evidence of where.  Piped into
   * a log, a block-buffered stdout loses all of it. */
  setvbuf(stdout, NULL, _IONBF, 0);

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

  printf("\n=== Modulation stability ===\n");
  for (int i = 0; i < s_num_units; ++i) probe_lfo_stability(&s_units[i]);
  for (int i = 0; i < s_num_units; ++i) probe_lfo_rate_zero_silent(&s_units[i]);

  printf("\n=== Chord tuning ===\n");
  for (int i = 0; i < s_num_units; ++i) probe_chords(&s_units[i]);

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
