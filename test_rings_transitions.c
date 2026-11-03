/*
 * test_rings_transitions.c -- what Rings sounds like while it is being changed.
 *
 * Model, Polyphony and Chord on/off reconfigure Rings' engine while a note may
 * be ringing, and rings-resonator.cc bridges each such change (see "Model /
 * Polyphony / Chord transitions" there).  Nothing else in the test suites can
 * tell a good bridge from a bad one: every one of the failures below renders
 * finite, in-range audio, and most of them were in a build that passed
 * everything else.  Measured on that build, with a note ringing:
 *
 *   - Polyphony 4 -> 1 on String or FM: silence.  The voices above the new
 *     count were dropped, and the notes were ringing on them.
 *   - Sympathetic String -> String: the note fell 34 dB, because the seven
 *     sympathetic strings it was ringing on were cleared.
 *   - Polyphony 4 -> 1 on Sympathetic Quantized: a click 23 times the
 *     surrounding programme -- strings dragged 2.5 octaves in 7 ms.
 *   - A Model change and a note-on together, which is what motion-sequencing
 *     Model does: the note was lost, 35-42 dB down, on any change of engine.
 *   - The Model knob swept: a 12-16 dB hole in the sound at every step.
 *
 * So this renders the real unit -- the same host build test-asan makes, with
 * the drumlogue ABI driven the way the firmware drives it -- through each kind
 * of change and measures, around the moment of the change:
 *
 *   click  the largest second difference in [-5 ms, +60 ms], over the RMS
 *          second difference of the signal either side.  Ordinary programme
 *          sits at 2-4; the click above was 23.
 *   dip    the quietest 5 ms in the 150 ms after the change, against the
 *          50 ms before.  A note left alone dips 2-5 dB there by itself.
 *   step   the level 60-110 ms after the change, against the 50 ms before.
 *
 * The limits sit between what the old build did and what this one does, with
 * room either side: they are there to catch the next regression of the same
 * kind, not to pin today's numbers.
 *
 * Usage: test_rings_transitions <rings.so>   (see `make test-rings-transitions`)
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime.h"

#define FR 64
#define SR 48000
#define MS(x) ((x) * SR / 1000)

/* Limits.  See the note at the top. */
static const double kMaxClick       = 8.0;
static const double kMaxClickChord  = 6.0;   /* chord on/off: tighter, see below */
static const double kMinDipDb       = -16.0;
static const double kMinStepDb      = -9.0;
static const double kMinSweepDipDb  = -8.0;
static const double kMaxStrikeLossDb = 6.0;

static const unit_header_t *hdr;
static void (*render)(const float *, float *, uint32_t);
static void (*setp)(uint8_t, int32_t);
static void (*noteon)(uint8_t, uint8_t);
static void (*noteoff)(uint8_t);
static int8_t (*uinit)(const unit_runtime_desc_t *);
static void (*teardown)(void);
static void (*ureset)(void);
static void (*resume)(void);
static const char *(*getstr)(uint8_t, int32_t);

static int s_failures, s_checks;

#define CHECK(cond, fmt, ...)                                   \
  do {                                                          \
    ++s_checks;                                                 \
    if (cond) {                                                 \
      if (getenv("VERBOSE")) printf("  ok   : " fmt "\n", ##__VA_ARGS__); \
    } else {                                                    \
      printf("  FAIL : " fmt "\n", ##__VA_ARGS__);              \
      ++s_failures;                                             \
    }                                                           \
  } while (0)

static int pid(const char *n) {
  for (uint32_t p = 0; p < hdr->num_params; ++p)
    if (strcmp(hdr->params[p].name, n) == 0) return (int)p;
  fprintf(stderr, "no parameter named %s\n", n);
  exit(2);
}

/* Full lifecycle and the declared defaults: every scenario starts from the
 * same state, random source included, so a run is reproducible. */
static void reinit(void) {
  unit_runtime_desc_t d;
  memset(&d, 0, sizeof d);
  d.target = hdr->target;
  d.api = UNIT_API_VERSION;
  d.samplerate = SR;
  d.frames_per_buffer = FR;
  d.output_channels = 2;
  if (teardown) teardown();
  uinit(&d);
  ureset();
  for (int p = 0; p < UNIT_MAX_PARAM_COUNT; ++p)
    setp((uint8_t)p, hdr->params[p].init);
  resume();
}

#define MAX_SAMPLES (SR * 2)
static float buf[MAX_SAMPLES];

typedef struct { int ms; int param; int value; } ev_t;
enum { EV_NOTE = -1, EV_OFF = -2 };

static void run(const ev_t *evs, int nev, int total_ms) {
  static float in[FR * 2], out[FR * 2];
  memset(in, 0, sizeof in);
  const int nblocks = MS(total_ms) / FR;
  int e = 0;
  for (int b = 0; b < nblocks; ++b) {
    const int ms = b * FR * 1000 / SR;
    while (e < nev && evs[e].ms <= ms) {
      if (evs[e].param == EV_NOTE) noteon((uint8_t)evs[e].value, 100);
      else if (evs[e].param == EV_OFF) noteoff((uint8_t)evs[e].value);
      else setp((uint8_t)evs[e].param, evs[e].value);
      ++e;
    }
    render(in, out, FR);
    for (int i = 0; i < FR; ++i)
      buf[b * FR + i] = 0.5f * (out[2 * i] + out[2 * i + 1]);
  }
}

static double rms(int a, int b) {
  double s = 0;
  if (a < 0) a = 0;
  for (int i = a; i < b; ++i) s += (double)buf[i] * buf[i];
  return sqrt(s / (b > a ? b - a : 1));
}

static double rms_d2(int a, int b) {
  double s = 0;
  int n = 0;
  if (a < 2) a = 2;
  for (int i = a; i < b; ++i) {
    const double d = buf[i] - 2.0 * buf[i - 1] + buf[i - 2];
    s += d * d;
    ++n;
  }
  return sqrt(s / (n ? n : 1));
}

typedef struct { double click, dip_db, step_db; } measure_t;

static measure_t measure(int t0ms) {
  const int t0 = MS(t0ms);
  measure_t m;
  const double ref = 0.5 * (rms_d2(t0 - MS(100), t0 - MS(10)) +
                            rms_d2(t0 + MS(80), t0 + MS(170)));
  double mx = 0;
  for (int i = t0 - MS(5); i < t0 + MS(60); ++i) {
    const double d = fabs(buf[i] - 2.0 * buf[i - 1] + buf[i - 2]);
    if (d > mx) mx = d;
  }
  const double before = rms(t0 - MS(50), t0) + 1e-9;
  double dip = 1e9;
  for (int w = t0; w < t0 + MS(150); w += MS(1)) {
    const double r = rms(w, w + MS(5));
    if (r < dip) dip = r;
  }
  m.click = ref > 0 ? mx / ref : 0;
  m.dip_db = 20 * log10((dip + 1e-9) / before);
  m.step_db = 20 * log10((rms(t0 + MS(60), t0 + MS(110)) + 1e-9) / before);
  return m;
}

static const char *model_name(int m) {
  const char *s = getstr ? getstr((uint8_t)pid("Model"), m) : NULL;
  return s ? s : "?";
}

static void check_change(const char *what, measure_t m, double max_click) {
  CHECK(m.click < max_click && m.dip_db > kMinDipDb && m.step_db > kMinStepDb,
        "%-34s click %5.2f  dip %6.1f dB  step %6.1f dB", what, m.click,
        m.dip_db, m.step_db);
}

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  if (argc < 2) {
    fprintf(stderr, "usage: %s <rings.so>\n", argv[0]);
    return 2;
  }
  void *h = dlopen(argv[1], RTLD_NOW);
  if (!h) {
    fprintf(stderr, "dlopen %s: %s\n", argv[1], dlerror());
    return 2;
  }
  hdr = (const unit_header_t *)dlsym(h, "unit_header");
  render = dlsym(h, "unit_render");
  setp = dlsym(h, "unit_set_param_value");
  noteon = dlsym(h, "unit_note_on");
  noteoff = dlsym(h, "unit_note_off");
  uinit = dlsym(h, "unit_init");
  teardown = dlsym(h, "unit_teardown");
  ureset = dlsym(h, "unit_reset");
  resume = dlsym(h, "unit_resume");
  getstr = dlsym(h, "unit_get_param_str_value");
  if (!hdr || !render || !setp || !noteon || !uinit || !ureset || !resume) {
    fprintf(stderr, "%s is missing unit ABI symbols\n", argv[1]);
    return 2;
  }

  const int kModel = pid("Model"), kPoly = pid("Polyphony");
  const int kChord = pid("Chord"), kDamp = pid("Damping");
  const int nmodels = hdr->params[kModel].max + 1;
  char label[96];

  printf("Rings transitions\n\n");

  /* Every Model change at Polyphony 1, a note struck and left to ring. */
  printf("  Model a -> b, note ringing ...\n");
  for (int a = 0; a < nmodels; ++a)
    for (int b = 0; b < nmodels; ++b) {
      if (a == b) continue;
      reinit();
      const ev_t ev[] = {{0, kDamp, 70}, {0, kModel, a}, {20, EV_NOTE, 60},
                         {120, EV_OFF, 60}, {400, kModel, b}};
      run(ev, 5, 1000);
      snprintf(label, sizeof label, "%s -> %s", model_name(a), model_name(b));
      check_change(label, measure(400), kMaxClick);
    }

  /* Polyphony changes, up and down, with two notes ringing on two voices --
   * so a change that drops voices drops the ones that are sounding. */
  printf("  Polyphony changes, two notes ringing ...\n");
  static const int kPolyChanges[][2] = {{1, 2}, {1, 4}, {4, 1}, {2, 3}, {3, 1}};
  for (int m = 0; m < nmodels; ++m)
    for (size_t k = 0; k < sizeof kPolyChanges / sizeof *kPolyChanges; ++k) {
      reinit();
      const ev_t ev[] = {{0, kDamp, 70}, {0, kModel, m},
                         {0, kPoly, kPolyChanges[k][0]},
                         {20, EV_NOTE, 60}, {120, EV_OFF, 60},
                         {170, EV_NOTE, 64}, {270, EV_OFF, 64},
                         {450, kPoly, kPolyChanges[k][1]}};
      run(ev, 8, 1000);
      snprintf(label, sizeof label, "%s poly %d -> %d", model_name(m),
               kPolyChanges[k][0], kPolyChanges[k][1]);
      check_change(label, measure(450), kMaxClick);
    }

  /* Chord on and off.  The limit is tighter here because the one defect this
   * section has caught so far was a click of 7.8 -- inside the general limit
   * -- from the struck voices jumping to Oct at the start of the bridge when
   * a chord was turned off. */
  printf("  Chord on/off, note ringing ...\n");
  for (int m = 0; m < nmodels; ++m)
    for (int poly = 1; poly <= 2; ++poly)
      for (int dir = 0; dir < 2; ++dir) {
        const int c0 = dir ? 4 : -1, c1 = dir ? -1 : 4;   /* min7 <-> Off */
        reinit();
        const ev_t ev[] = {{0, kDamp, 70}, {0, kModel, m}, {0, kPoly, poly},
                           {0, kChord, c0}, {20, EV_NOTE, 60},
                           {120, EV_OFF, 60}, {400, kChord, c1}};
        run(ev, 7, 1000);
        snprintf(label, sizeof label, "%s poly %d chord %s", model_name(m),
                 poly, dir ? "min7 -> Off" : "Off -> min7");
        check_change(label, measure(400), kMaxClickChord);
      }

  /* The Model knob swept through its range and back, a step every 30 ms. */
  printf("  Model knob swept, note ringing ...\n");
  {
    reinit();
    ev_t ev[32];
    int n = 0;
    ev[n++] = (ev_t){0, kDamp, 70};
    ev[n++] = (ev_t){0, kModel, 0};
    ev[n++] = (ev_t){20, EV_NOTE, 60};
    ev[n++] = (ev_t){120, EV_OFF, 60};
    int t = 400;
    for (int m = 1; m < nmodels; ++m, t += 30) ev[n++] = (ev_t){t, kModel, m};
    for (int m = nmodels - 2; m >= 0; --m, t += 30) ev[n++] = (ev_t){t, kModel, m};
    run(ev, n, 1500);
    double worst = 0;
    int worst_step = 0;
    for (int k = 0; k < 2 * (nmodels - 1); ++k) {
      const measure_t m = measure(400 + 30 * k);
      if (m.dip_db < worst) { worst = m.dip_db; worst_step = k + 1; }
    }
    CHECK(worst > kMinSweepDipDb,
          "sweep: worst dip %.1f dB (step %d of %d)", worst, worst_step,
          2 * (nmodels - 1));
  }

  /* A Model change and a note-on in the same instant, a note still ringing:
   * the new note has to sound, on the new model, as loud as it does when the
   * change was made beforehand.  Compared on broadband level over the first
   * 160 ms of the note: a fresh strike is well above what is left of the old
   * note, and a single frequency is no good on FM, whose index moves the
   * energy between the carrier and its sidebands as the note decays. */
  printf("  Model change riding on a note-on ...\n");
  for (int m = 0; m < nmodels; ++m) {
    const int from = (m + 3) % nmodels;
    double level[2];
    for (int same = 0; same < 2; ++same) {
      reinit();
      setp((uint8_t)kDamp, 70);
      setp((uint8_t)kModel, (uint8_t)from);
      const ev_t ev[] = {{20, EV_NOTE, 60}, {150, EV_OFF, 60},
                         {same ? 450 : 200, kModel, m}, {450, EV_NOTE, 67}};
      run(ev, 4, 800);
      level[same] = rms(MS(455), MS(615));
    }
    const double loss = 20 * log10((level[1] + 1e-9) / (level[0] + 1e-9));
    CHECK(loss > -kMaxStrikeLossDb,
          "%s -> %s with the note: struck note %+5.1f dB against the change "
          "made beforehand", model_name(from), model_name(m), loss);
  }

  printf("\n  %d checks, %d failed\n", s_checks, s_failures);
  printf("\n=== %s (%d failures) ===\n", s_failures ? "FAILED" : "ALL PASS",
         s_failures);
  return s_failures ? 1 : 0;
}
