/*
 * test_clouds_cola.cc — does the STFT's overlap-add reconstruct its input at
 * the configured hop ratio?
 *
 * `CLOUDS_PVOC_HOP_RATIO` decides how often a transform runs, and so most of
 * what Spectral costs. Halving the overlap is the only change in the engine
 * worth a factor rather than a few percent, and the argument for it being
 * safe is a COLA one: the window is applied at analysis *and* synthesis, so
 * the effective window is sine squared = Hann, and Hann satisfies the
 * constant-overlap-add condition at 50% overlap exactly as it does at 75%.
 *
 * That is algebra, and algebra about a fixed-point pipeline with an int16
 * analysis buffer and a normalisation constant derived at runtime deserves to
 * be checked rather than believed. So this drives the real STFT with the
 * modifier disabled — which makes Buffer() a pure analysis/synthesis pass —
 * and measures the reconstruction ripple of a steady tone.
 *
 * A COLA failure is amplitude modulation at the hop rate, so the metric is
 * the spread between the quietest and loudest short window of output. Ratio 1
 * (no overlap at all) is measured alongside as a control: without it, a test
 * that reported "no ripple" for every ratio would look like a pass instead of
 * a broken measurement.
 *
 * Usage: make test-clouds-cola
 */
#include <cstdio>
#include <cstring>
#include <cmath>

#include "clouds/dsp/pvoc/stft.h"
#include "clouds/dsp/parameters.h"
#include "clouds/resources.h"

using namespace clouds;

static const size_t kFft = 512;
static const size_t kBlock = 32;

typedef stmlib::ShyFFT<float, kFft, stmlib::LutPhasor> FFT;
static FFT s_fft;
alignas(16) static float s_fft_buf[kFft];
alignas(16) static float s_ifft_buf[kFft];
static short s_ana_syn[(kFft + (kFft >> 1)) * 2];
static STFT s_stft;
static Parameters s_params;

static int s_failures;

/* Reconstruction ripple, in dB, for one hop ratio: the spread between the
 * quietest and loudest 128-sample window of output for a steady input. */
static double measure_ripple(size_t hop_ratio) {
  s_fft.Init();
  memset(s_ana_syn, 0, sizeof(s_ana_syn));
  memset(&s_params, 0, sizeof(s_params));
  s_stft.Init(&s_fft, kFft, kFft / hop_ratio, s_fft_buf, s_ifft_buf,
              lut_sine_window_4096, s_ana_syn, NULL /* pass-through */);

  static float in[kBlock], out[kBlock];
  double phase = 0.0, acc = 0.0;
  double lo = 1e30, hi = 0.0;
  int n = 0, win = 0;

  for (int b = 0; b < 3000; ++b) {
    for (size_t i = 0; i < kBlock; ++i) {
      phase += 0.11;                       /* ~560 Hz at 32 kHz */
      in[i] = 0.5f * (float)sin(phase);
    }
    s_stft.Process(s_params, in, out, kBlock, 1);
    s_stft.Buffer();
    if (b <= 200) continue;                /* let the ring fill */
    for (size_t i = 0; i < kBlock; ++i) {
      acc += (double)out[i] * out[i];
      ++n;
    }
    if (++win >= 4) {
      const double r = sqrt(acc / n);
      if (r < lo) lo = r;
      if (r > hi) hi = r;
      acc = 0.0; n = 0; win = 0;
    }
  }
  return 20.0 * log10(hi / (lo + 1e-12));
}

static void check(int ok, const char *fmt, double value) {
  fputs(ok ? "  ok   : " : "  FAIL : ", stdout);
  printf(fmt, value);
  putchar('\n');
  if (!ok) ++s_failures;
}

int main(void) {
  printf("Clouds STFT overlap-add reconstruction\n\n");

  const double r4 = measure_ripple(4);
  const double r2 = measure_ripple(2);
  const double r1 = measure_ripple(1);

  /* 75% overlap is upstream's setting and the reference. */
  check(r4 < 1.0, "hop ratio 4 (75%% overlap): ripple %.2f dB", r4);

  /* The claim under test: half the overlap reconstructs just as well. */
  check(r2 < 1.0, "hop ratio 2 (50%% overlap): ripple %.2f dB", r2);
  check(fabs(r2 - r4) < 0.1,
        "hop ratio 2 matches hop ratio 4 (difference %.2f dB)", fabs(r2 - r4));

  /* Control: without overlap the window cannot sum to a constant, so a test
   * that passes this one is not measuring anything. */
  check(r1 > 3.0, "control: hop ratio 1 (no overlap) ripple %.2f dB", r1);

  printf("\n=== %s (%d failures) ===\n", s_failures ? "FAILED" : "ALL PASS",
         s_failures);
  return s_failures ? 1 : 0;
}
