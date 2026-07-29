/*
 * File: test_clouds_fft.cc
 *
 * Tests for the vectorised ShyFFT in eurorack-opt/stmlib/fft/shy_fft.h.
 *
 * Spectral mode is the one place in Clouds where a wrong answer does not
 * announce itself: the mode exists to blur spectra, randomise phases and warp
 * magnitudes, so a broken FFT still produces something that sounds like a
 * working FFT. That is the whole reason this file exists — the reverb work
 * could be checked by ear because a bad flush was audible as a burst, and
 * this cannot.
 *
 * Two things are checked, and they cover different failure modes.
 *
 * [1] The interface contract. Layout, sign convention and scaling are pinned
 *     against hand-computed expectations rather than against the other
 *     implementation, so they still hold if someone later swaps in a
 *     different FFT entirely. These are exactly the properties a from-scratch
 *     kernel gets wrong: the spectrum is split (real half then imaginary
 *     half, not interleaved), the imaginary half carries the *negated*
 *     imaginary part of the usual e^-jwt convention, and both directions are
 *     unnormalised, with stft.cc making up the factor of N.
 *
 * [2] The vectorisation. The NEON butterfly and upstream's scalar one are
 *     instantiated side by side from the same header (kUseNeon) and fed the
 *     same buffers. Off ARM both select the scalar path, so the host run only
 *     proves the refactor did not disturb it; the ARM run under QEMU is what
 *     exercises the vector code. The tolerance is -90 dB relative to the peak
 *     bin, which is stricter than the int16 buffers STFT keeps on either side
 *     of the transform (one LSB is 87 dB below signal peak).
 *
 * Build/run: make test-clouds-fft   (host, plus ARM under QEMU if available)
 */

#include "stmlib/fft/shy_fft.h"

#include <cstdio>
#include <cstdint>
#include <cmath>

static const int N = 4096;   /* the only size Clouds uses; see PhaseVocoder::Init */

typedef stmlib::ShyFFT<float, N, stmlib::LutPhasor, false> FftScalar;
typedef stmlib::ShyFFT<float, N, stmlib::LutPhasor, true>  FftVector;

static FftScalar fft_scalar;
static FftVector fft_vector;

static float in_a[N], in_b[N], out_a[N], out_b[N];

static int g_fail = 0;
#define CHECK(cond, ...)                          \
  do {                                            \
    if (!(cond)) { g_fail++; printf("  FAIL: "); } \
    else printf("  ok:   ");                      \
    printf(__VA_ARGS__);                          \
    printf("\n");                                 \
  } while (0)

static double db(double x) { return 20.0 * log10(x > 1e-300 ? x : 1e-300); }

/* Windowed, int16-quantised noise: what the STFT actually hands the FFT. */
static void fill_signal(float *x) {
  uint32_t s = 12345u;
  for (int i = 0; i < N; ++i) {
    s = s * 1664525u + 1013904223u;
    const double n = (double)(int32_t)s / 2147483648.0;
    const double w = 0.5 - 0.5 * cos(2.0 * M_PI * i / N);
    x[i] = (float)(floor(n * 0.7 * 32768.0) * w);
  }
}

int main(void) {
  printf("Clouds FFT Tests\n\n");
  fft_scalar.Init();
  fft_vector.Init();

  /* --- 1. Interface contract ------------------------------------------- */
  printf("[1] Layout, sign and scaling contract\n");
  const int k = 37;

  for (int n = 0; n < N; ++n)
    in_a[n] = cosf(2.0f * (float)M_PI * k * n / N);
  fft_vector.Direct(in_a, out_a);
  CHECK(fabsf(out_a[k] - (float)(N / 2)) < 1.0f,
        "cos at bin %d -> real half [%d] = %.1f (expected %d: split layout, "
        "unnormalised)", k, k, out_a[k], N / 2);
  CHECK(fabsf(out_a[N / 2 + k]) < 1.0f,
        "cos at bin %d -> imag half [%d] = %.3f (expected 0)",
        k, k, out_a[N / 2 + k]);

  for (int n = 0; n < N; ++n)
    in_a[n] = sinf(2.0f * (float)M_PI * k * n / N);
  fft_vector.Direct(in_a, out_a);
  CHECK(fabsf(out_a[N / 2 + k] - (float)(N / 2)) < 1.0f,
        "sin at bin %d -> imag half [%d] = %+.1f (expected %+d: note the sign, "
        "this half is the negated imaginary part of e^-jwt)",
        k, k, out_a[N / 2 + k], N / 2);
  CHECK(fabsf(out_a[k]) < 1.0f,
        "sin at bin %d -> real half [%d] = %.3f (expected 0)", k, k, out_a[k]);

  /* Round trip is N*x, not x: both directions are unnormalised and stft.cc
   * folds the factor into inverse_window_size. Getting this wrong is a
   * factor of 4096 in one direction or the other. */
  fill_signal(in_a);
  float peak = 0.0f;
  for (int i = 0; i < N; ++i) peak = fmaxf(peak, fabsf(in_a[i]));
  for (int i = 0; i < N; ++i) in_b[i] = in_a[i];
  fft_vector.Direct(in_b, out_b);
  fft_vector.Inverse(out_b, in_b);
  double rt = 0.0;
  for (int i = 0; i < N; ++i)
    rt = fmax(rt, fabs((double)in_b[i] / N - (double)in_a[i]));
  CHECK(rt / peak < 1e-4,
        "Direct + Inverse reconstructs N*x, error %.1f dB below signal peak "
        "(one int16 LSB is %.1f dB down)", -db(rt / peak), -db(1.0 / peak));

  /* --- 2. Vectorised butterfly against upstream's scalar one ------------ */
#ifdef __ARM_NEON
  printf("\n[2] NEON butterfly vs upstream scalar (NEON path active)\n");
#else
  printf("\n[2] NEON butterfly vs upstream scalar "
         "(no NEON on this target: both paths scalar, refactor smoke test)\n");
#endif

  fill_signal(in_a);
  for (int i = 0; i < N; ++i) in_b[i] = in_a[i];
  fft_scalar.Direct(in_a, out_a);
  fft_vector.Direct(in_b, out_b);
  double specmax = 0.0, specerr = 0.0;
  for (int i = 0; i < N; ++i) {
    specmax = fmax(specmax, fabs((double)out_a[i]));
    specerr = fmax(specerr, fabs((double)out_a[i] - (double)out_b[i]));
  }
  CHECK(specerr / specmax < 3.2e-5,   /* -90 dB */
        "Direct: worst bin difference %.1f dB below peak bin (need >= 90)",
        -db(specerr / specmax));

  /* Inverse, fed the identical spectrum so only the transform differs. */
  for (int i = 0; i < N; ++i) out_b[i] = out_a[i];
  fft_scalar.Inverse(out_a, in_a);
  fft_vector.Inverse(out_b, in_b);
  double sigmax = 0.0, sigerr = 0.0;
  for (int i = 0; i < N; ++i) {
    sigmax = fmax(sigmax, fabs((double)in_a[i]));
    sigerr = fmax(sigerr, fabs((double)in_a[i] - (double)in_b[i]));
  }
  CHECK(sigerr / sigmax < 3.2e-5,
        "Inverse: worst sample difference %.1f dB below peak (need >= 90)",
        -db(sigerr / sigmax));

  printf("\n=== %s (%d failures) ===\n", g_fail ? "FAILED" : "ALL PASS", g_fail);
  return g_fail ? 1 : 0;
}
