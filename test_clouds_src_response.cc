/*
 * File: test_clouds_src_response.cc
 *
 * Does the sample-rate converter pass the filter it was designed to pass?
 *
 * clouds_src.h ships two prototype lengths and the tables for both are
 * generated (tools/generate_src_tables.py).  Generated tables have a specific
 * failure mode: a branch stored in the wrong order, or the phases swapped,
 * still produces plausible audio -- continuous, right level, no NaN -- while
 * being the wrong filter.  Nothing else in the suite would notice, because
 * every other test compares the port against itself.
 *
 * So this measures the converters instead of inspecting them: sines through
 * SrcDown then SrcUp, which is the round trip clouds-fx.cc performs on every
 * sample, and the output level against what the design says it should be.
 * Two filter passes, so the expectations are the single-pass response
 * doubled.  A mis-ordered table misses these by tens of dB.
 *
 * The expectations come from the design and are checked here; the design
 * itself is checked by `python3 tools/generate_src_tables.py --verify`, which
 * regenerates the shipped 120-tap tables from the same script and diffs them
 * against the header.
 *
 * Build/run: make test-clouds-src-response
 */

#include "clouds_src.h"

#include <cstdio>
#include <cstring>
#include <cmath>

using namespace clouds_src;

static int failures_ = 0;

/* Round-trip gain at `f`, in dB.
 *
 * RMS rather than peak: at frequencies that divide the sample rate evenly the
 * sampled peak can sit off the true one and read low by more than a dB, which
 * is a property of the measurement and not of the filter. */
static double round_trip_db(double f) {
  SrcDown d;
  SrcUp u;
  d.Init();
  u.Init();
  double ph = 0.0, energy = 0.0;
  long n = 0;
  for (int blk = 0; blk < 400; ++blk) {
    const int need48 = d.InputNeeded(32);
    float *in = d.Input();
    for (int i = 0; i < need48; ++i) {
      in[i] = (float)sin(ph);
      ph += 2.0 * M_PI * f / 48000.0;
    }
    float mid[32];
    d.Process(mid, 32, 1);

    const int need32 = u.InputNeeded(48);
    memcpy(u.Input(), mid, (size_t)need32 * sizeof(float));
    float out[48];
    u.Process(out, 48, 1);

    if (blk > 200) {                 /* past both group delays */
      for (int i = 0; i < 48; ++i) { energy += (double)out[i] * out[i]; ++n; }
    }
  }
  const double rms = sqrt(energy / (double)n);
  const double amp = rms * sqrt(2.0);   /* sine */
  return 20.0 * log10(amp > 1e-9 ? amp : 1e-9);
}

struct Point { double f; double expect_db; };

/* Round-trip expectations: the single-pass design response, doubled.
 * Tolerance is generous where the curve is steep, because a few hertz of
 * measurement offset is worth several dB there and the point of the test is
 * table ordering, not the third decimal of a Kaiser window. */
#if CLOUDS_SRC_TAPS == 120
static const Point kPoints[] = {
  {   100.0,   0.0 }, {  1000.0,  0.0 }, {  5000.0,  0.0 }, { 10000.0,   0.0 },
  { 12000.0,   0.0 }, { 13000.0, -0.4 }, { 14000.0, -6.0 }, { 15000.0, -27.5 },
};
#else
static const Point kPoints[] = {
  {   100.0,   0.0 }, {  1000.0,   0.0 }, {  5000.0,  -0.0 }, { 10000.0,  -0.5 },
  { 11000.0,  -2.5 }, { 12000.0,  -7.8 }, { 13000.0, -17.8 }, { 15000.0, -62.5 },
};
#endif

int main(void) {
  printf("Clouds SRC Round-Trip Response  (CLOUDS_SRC_TAPS=%d, "
         "up %d taps/branch, down %d)\n\n",
         (int)CLOUDS_SRC_TAPS, kUpTaps, kDownTaps);
  printf("  48 -> 32 -> 48 kHz, the round trip CloudsFX runs on every sample.\n");
  printf("  Expectations are the designed single-pass response doubled.\n\n");

  for (size_t i = 0; i < sizeof(kPoints) / sizeof(kPoints[0]); ++i) {
    const double got = round_trip_db(kPoints[i].f);
    const double want = kPoints[i].expect_db;
    /* Steep parts of the curve need more room than flat ones. */
    const double tol = want > -1.0 ? 0.6 : (want > -20.0 ? 2.0 : 6.0);
    const bool ok = fabs(got - want) <= tol;
    if (!ok) ++failures_;
    printf("  %s %6.0f Hz  %8.2f dB   (expected %7.2f +/- %.1f)\n",
           ok ? "ok:  " : "FAIL:", kPoints[i].f, got, want, tol);
  }

  /* Whatever the length, the converter must not invent gain: a resampler that
   * amplifies has the wrong branch scaling, which is the other way generated
   * tables go wrong. */
  const double dc = round_trip_db(100.0);
  const bool unity = fabs(dc) <= 0.3;
  if (!unity) ++failures_;
  printf("\n  %s unity passband gain (%.3f dB at 100 Hz)\n",
         unity ? "ok:  " : "FAIL:", dc);

  printf("\n%s\n", failures_ ? "=== FAILURES ===" : "=== ALL PASS (0 failures) ===");
  return failures_ ? 1 : 0;
}
