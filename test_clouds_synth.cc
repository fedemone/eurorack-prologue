/*
 * File: test_clouds_synth.cc
 *
 * Clouds synth (osc) end-to-end test: links the REAL Clouds engine behind the
 * real OSC_* callbacks, the way drumlogue_osc_adapter.cc drives it.
 *
 * The thing this exists to protect is the sample-rate boundary.  The engine
 * runs at its native 32 kHz while the drumlogue renders at 48 kHz, so every
 * block crosses a 3:2 resampler (clouds_src.h).  Get that wrong in the
 * obvious way — feed the engine 48 kHz samples and play them back as if they
 * were 32 kHz — and everything still runs, produces plausible audio, and is
 * a fifth out of tune.  So the first test measures pitch and rejects exactly
 * that failure, by checking the note's fundamental beats both the 1.5x and
 * the 1/1.5x impostor.
 *
 * The second sweeps all four modes against all four qualities, which is the
 * combination that reallocates the engine's buffers, and checks each one
 * produces finite, in-range, non-silent audio.
 *
 * Build/run: make test-clouds-synth
 */

#include "userosc.h"
#include "sample_wrapper.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdint>

/* The sawtooth source is the one under test here; the sample bank has its own
 * suite in test_clouds_sample_playback.cc. */
extern "C" const sample_wrapper_t *osc_adapter_get_sample(uint8_t bank,
                                                          uint8_t idx) {
  (void)bank; (void)idx;
  return nullptr;
}

static int g_fail = 0;
#define CHECK(cond, ...)                        \
  do {                                          \
    if (!(cond)) { g_fail++; printf("  FAIL: "); } \
    else printf("  ok:   ");                    \
    printf(__VA_ARGS__);                        \
    printf("\n");                               \
  } while (0)

static user_osc_param_t prm;

static const int kBlock = 32;
static const int kCapture = 24576;   /* 0.5 s at 48 kHz */
static float g_buf[kCapture];

/* Render `blocks` blocks, keeping the last kCapture samples' worth. */
static int capture(int blocks) {
  int n = 0;
  int32_t yn[kBlock];
  for (int b = 0; b < blocks; ++b) {
    OSC_CYCLE(&prm, yn, kBlock);
    for (int i = 0; i < kBlock; ++i) {
      if (n < kCapture) g_buf[n++] = (float)yn[i] / 2147483648.0f;
    }
  }
  return n;
}

/* Goertzel magnitude at `f`.  Cheap, and unlike zero-crossing counting it
 * does not fall apart on a signal with grain edges all over it. */
static double goertzel(const float *x, int n, double f, double fs) {
  const double w = 2.0 * M_PI * f / fs;
  const double c = 2.0 * cos(w);
  double s1 = 0.0, s2 = 0.0;
  for (int i = 0; i < n; ++i) {
    const double s0 = x[i] + c * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  return sqrt(fabs(s1 * s1 + s2 * s2 - c * s1 * s2));
}

int main(void) {
  printf("Clouds Synth Tests\n\n");
  memset(&prm, 0, sizeof(prm));
  OSC_INIT(0, 0);

  /* --- 1. Pitch survives the 48 kHz <-> 32 kHz boundary --- */
  printf("[1] Sawtooth pitch through the 32 kHz engine\n");
  OSC_PARAM(k_user_osc_param_id5, 0);   /* Dry/Wet 0 % -> dry sawtooth only */
  OSC_PARAM(k_user_osc_param_id6, 0);   /* Reverb 0 % */
  OSC_PARAM(k_user_osc_param_id4, 0);   /* Feedback 0 % */

  const struct { int note; double hz; } notes[] = {
    {45, 110.0}, {57, 220.0}, {69, 440.0}, {81, 880.0}
  };
  for (unsigned c = 0; c < sizeof(notes) / sizeof(notes[0]); ++c) {
    prm.pitch = (uint16_t)(notes[c].note << 8);
    OSC_NOTEON(&prm);
    capture(40);                         /* let the attack settle */
    const int n = capture(kCapture / kBlock);
    const double at = goertzel(g_buf, n, notes[c].hz, 48000.0);
    const double up = goertzel(g_buf, n, notes[c].hz * 1.5, 48000.0);
    const double dn = goertzel(g_buf, n, notes[c].hz / 1.5, 48000.0);
    /* A sawtooth has real energy at 1.5x (its third harmonic is at 3x, but
     * neighbouring partials leak), so require a clear margin rather than
     * mere dominance. */
    CHECK(at > up * 2.0 && at > dn * 2.0,
          "note %d sounds at %.0f Hz (fundamental %.3f vs %.3f at 1.5x, "
          "%.3f at 1/1.5x)", notes[c].note, notes[c].hz, at, up, dn);
    OSC_NOTEOFF(&prm);
    capture(60);
  }

  /* --- 2. Every mode/quality combination produces usable audio ---
   *
   * Quality changes num_channels_/low_fidelity_ and Mode can cross into
   * Spectral: both send Prepare() down its reallocating path, which is where
   * this port has historically broken. */
  printf("\n[2] All modes x qualities produce finite, non-silent audio\n");
  prm.pitch = (uint16_t)(60 << 8);
  OSC_PARAM(k_user_osc_param_id5, 100);  /* Dry/Wet 100 % */
  OSC_PARAM(k_user_osc_param_id1, 60);   /* Density 60 % */
  OSC_PARAM(k_user_osc_param_id2, 50);   /* Texture 50 % */
  static const char *mode_name[4] = {"Granular", "Stretch", "Delay", "Spectral"};
  static const char *qual_name[4] = {"StHi", "MoHi", "StLo", "MoLo"};
  for (int mode = 0; mode < 4; ++mode) {
    for (int q = 0; q < 4; ++q) {
      OSC_PARAM(9, mode);
      OSC_PARAM(10, q);
      OSC_NOTEON(&prm);
      double peak = 0.0;
      int bad = 0;
      int32_t yn[kBlock];
      for (int b = 0; b < 900; ++b) {
        OSC_CYCLE(&prm, yn, kBlock);
        for (int i = 0; i < kBlock; ++i) {
          const double v = (double)yn[i] / 2147483648.0;
          if (!(v == v) || fabs(v) > 1.001) ++bad;
          if (fabs(v) > peak) peak = fabs(v);
        }
      }
      CHECK(bad == 0 && peak > 1e-4,
            "%s / %s: peak %.4f, %d bad samples", mode_name[mode],
            qual_name[q], peak, bad);
      OSC_NOTEOFF(&prm);
      for (int b = 0; b < 60; ++b) OSC_CYCLE(&prm, yn, kBlock);
    }
  }

  /* --- 3. Note off decays to silence rather than hanging --- */
  printf("\n[3] Note off releases\n");
  OSC_PARAM(9, 0);
  OSC_PARAM(10, 0);
  OSC_NOTEON(&prm);
  capture(300);
  OSC_NOTEOFF(&prm);
  int32_t yn[kBlock];
  for (int b = 0; b < 4000; ++b) OSC_CYCLE(&prm, yn, kBlock);
  double tail = 0.0;
  for (int b = 0; b < 200; ++b) {
    OSC_CYCLE(&prm, yn, kBlock);
    for (int i = 0; i < kBlock; ++i) {
      const double v = fabs((double)yn[i] / 2147483648.0);
      if (v > tail) tail = v;
    }
  }
  CHECK(tail < 0.02, "tail decays after note off (peak %.5f)", tail);

  printf("\n=== %s (%d failures) ===\n", g_fail ? "FAILED" : "ALL PASS", g_fail);
  return g_fail ? 1 : 0;
}
