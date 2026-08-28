/*
 * clouds_src.h - rational 3:2 sample-rate conversion for the Clouds ports
 *
 * Clouds is a 32 kHz machine.  GranularProcessor::sample_rate() returns
 * 32000 / (low_fidelity ? 2 : 1), and everything derived from it -- the
 * feedback high-pass corner, the phase vocoder's frame rate, the grain
 * scheduler's notion of time -- assumes the engine is clocked at that rate.
 * The drumlogue runs at 48 kHz.  Feeding the engine 48 kHz directly (which
 * is what this port did originally) has two costs:
 *
 *   1. Every sample-rate-derived constant is off by 1.5x.  Grains, delay
 *      times and buffer capacity are all a third shorter than Clouds', and
 *      the feedback filter sits at 30 Hz instead of 20 Hz.
 *   2. The engine does 1500 blocks per second instead of 1000, so it costs
 *      50 % more CPU than the hardware it was written for ever paid.
 *
 * So: convert at the boundary and run the engine at its native rate.  48 and
 * 32 kHz are exactly 3:2, which makes this a fixed rational resampler rather
 * than anything adaptive -- no drift, no interpolation error that varies with
 * position, and a coefficient set that can be computed once, offline.
 *
 * Both directions share one prototype low-pass, designed at the 96 kHz common
 * multiple: by default 120 taps, Kaiser (beta = 8), -6 dB at 14.4 kHz.
 * Measured response: flat to 13 kHz, -42 dB at 16 kHz, -82 dB at 17 kHz,
 * below -95 dB from 18 kHz up.  The 16 kHz figure is the one that matters --
 * it is the Nyquist of the 32 kHz engine, so it bounds both the aliasing
 * folded in on the way down and the imaging let through on the way up.  The
 * audible cost is a gentle rolloff above 13 kHz, which is roughly what
 * Clouds' own codec does anyway.
 *
 * A 60-tap alternative is available for builds that need the CPU back; see
 * CLOUDS_SRC_TAPS below for what it costs.
 *
 * Polyphase decomposition: for output n, position on the 96 kHz grid is
 * m = n*M, so the phase is m mod L and the base input index is m div L, and
 *
 *     y[n] = sum_k h[phase + k*L] * x[base - k]
 *
 * The tables below store each branch reversed, so both the coefficients and
 * the samples are read forward at run time (see Dot).
 *
 * Only the taps that land on non-zero samples are ever touched, so upsampling
 * costs 40 multiply-adds per output and downsampling 60 -- about 1.5 MMAC/s
 * per stream either way, against an engine that costs a hundred times that.
 *
 * Group delay is (120-1)/2 = 59.5 samples at 96 kHz = 0.62 ms per conversion.
 *
 * Header-only and free of Clouds dependencies so both the synth
 * (clouds-granular.cc) and the FX (clouds-fx.cc) can include it.
 */

#ifndef CLOUDS_SRC_H_
#define CLOUDS_SRC_H_

#include <cstddef>
#include <cstring>

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

namespace clouds_src {

/* Prototype length: how many taps the shared low-pass has.
 *
 * 120 is the shipped design and the default.  60 halves the per-sample cost
 * of every conversion, which for CloudsFX is the largest single lever there
 * is -- the four converters and their FIFOs measure 6.7-7.8% of a render
 * against the whole unit's 17-18% in Stretch (`make bench-clouds-src`,
 * `make bench-units`).  The synth runs one converter, so it saves
 * proportionally less.
 *
 * What it costs is passband, not alias rejection, and that choice is
 * deliberate.  A 60-tap Kaiser has roughly twice the transition width of a
 * 120-tap one, and that width has to be spent somewhere: either the corner
 * stays at 14.4 kHz and rejection at the engine's 16 kHz Nyquist falls from
 * -42 dB to -17 dB, or the corner comes down and rejection is kept.  The
 * short set takes the second, at beta 7 and a 12.5 kHz corner:
 *
 *              12 kHz   13 kHz   14 kHz   16 kHz   17 kHz
 *   120 taps    -0.0     -0.2     -3.0    -41.7    -81.8
 *    60 taps    -3.9     -8.9    -17.3    -61.4    -86.5
 *
 * So the short filter rejects aliases *better* than the shipped one; what it
 * loses is the top of the passband, a gentle dulling from about 11 kHz up.
 * That is the right way round for a drum bus: a slightly dark effect return
 * is a tone change, while aliasing on cymbals is grit that was not in the
 * source and does not sit anywhere musical.
 *
 * Both sets come from tools/generate_src_tables.py, which regenerates the
 * 120-tap tables and diffs them against the ones below (`--verify`) before it
 * is trusted to emit anything else.
 *
 * Build with -DCLOUDS_SRC_TAPS=60 to select the short set. */
#ifndef CLOUDS_SRC_TAPS
#define CLOUDS_SRC_TAPS 120
#endif
#if CLOUDS_SRC_TAPS != 120 && CLOUDS_SRC_TAPS != 60
#error "CLOUDS_SRC_TAPS must be 120 (shipped) or 60 (short); see clouds_src.h"
#endif

/* Taps per polyphase branch: CLOUDS_SRC_TAPS / L. */
static const int kUpTaps = CLOUDS_SRC_TAPS / 3;    /* 32 -> 48 kHz, L=3, M=2 */
static const int kDownTaps = CLOUDS_SRC_TAPS / 2;  /* 48 -> 32 kHz, L=2, M=3 */

/* Largest number of input samples either converter will be asked to absorb in
 * one call.  Up: 64 outputs need (2 + 128)/3 = 43 inputs.  Down: 32 outputs
 * need (1 + 96)/2 = 48 inputs.  64 covers both with room to spare. */
static const int kMaxIn = 64;

/* Largest output block, i.e. the chunk size callers must not exceed. */
static const int kMaxUpOut = 64;
static const int kMaxDownOut = 32;

#if CLOUDS_SRC_TAPS == 120

static const float kSrcUpPhase[3][40] = {
  {
      -0.000065157f,    0.000211598f,   -0.000486889f,    0.000895102f,
      -0.001368745f,    0.001734924f,   -0.001699067f,    0.000860792f,
       0.001228389f,   -0.004965360f,    0.010565363f,   -0.017923728f,
       0.026495445f,   -0.035210335f,    0.042411130f,   -0.045719162f,
       0.041460707f,   -0.021976293f,   -0.042132442f,    0.866843077f,
       0.268320627f,   -0.149825892f,    0.102769541f,   -0.071231769f,
       0.046842138f,   -0.027729439f,    0.013390016f,   -0.003466494f,
      -0.002584538f,    0.005520121f,   -0.006216087f,    0.005536532f,
      -0.004218852f,    0.002802417f,   -0.001609304f,    0.000769985f,
      -0.000277172f,    0.000048224f,    0.000020472f,   -0.000017041f,
  },
  {
      -0.000060664f,    0.000159357f,   -0.000277778f,    0.000339388f,
      -0.000203056f,   -0.000330221f,    0.001482633f,   -0.003434184f,
       0.006232862f,   -0.009696004f,    0.013324834f,   -0.016253764f,
       0.017245625f,   -0.014719894f,    0.006754199f,    0.009105760f,
      -0.036717209f,    0.084818857f,   -0.185072313f,    0.627298739f,
       0.627298739f,   -0.185072313f,    0.084818857f,   -0.036717209f,
       0.009105760f,    0.006754199f,   -0.014719894f,    0.017245625f,
      -0.016253764f,    0.013324834f,   -0.009696004f,    0.006232862f,
      -0.003434184f,    0.001482633f,   -0.000330221f,   -0.000203056f,
       0.000339388f,   -0.000277778f,    0.000159357f,   -0.000060664f,
  },
  {
      -0.000017041f,    0.000020472f,    0.000048224f,   -0.000277172f,
       0.000769985f,   -0.001609304f,    0.002802417f,   -0.004218852f,
       0.005536532f,   -0.006216087f,    0.005520121f,   -0.002584538f,
      -0.003466494f,    0.013390016f,   -0.027729439f,    0.046842138f,
      -0.071231769f,    0.102769541f,   -0.149825892f,    0.268320627f,
       0.866843077f,   -0.042132442f,   -0.021976293f,    0.041460707f,
      -0.045719162f,    0.042411130f,   -0.035210335f,    0.026495445f,
      -0.017923728f,    0.010565363f,   -0.004965360f,    0.001228389f,
       0.000860792f,   -0.001699067f,    0.001734924f,   -0.001368745f,
       0.000895102f,   -0.000486889f,    0.000211598f,   -0.000065157f,
  },
};

static const float kSrcDownPhase[2][60] = {
  {
      -0.000040443f,    0.000013648f,    0.000141065f,   -0.000185186f,
      -0.000184781f,    0.000596735f,   -0.000135370f,   -0.001072870f,
       0.001156616f,    0.000988422f,   -0.002812568f,    0.000573861f,
       0.004155242f,   -0.004144058f,   -0.003310240f,    0.008883223f,
      -0.001723026f,   -0.011949152f,    0.011497083f,    0.008926677f,
      -0.023473557f,    0.004502799f,    0.031228092f,   -0.030479441f,
      -0.024478139f,    0.068513027f,   -0.014650862f,   -0.123381542f,
       0.178880418f,    0.577895385f,    0.418199159f,   -0.028088295f,
      -0.099883928f,    0.056545904f,    0.027640471f,   -0.047487846f,
       0.006070506f,    0.028274087f,   -0.018486293f,   -0.009813263f,
       0.017663630f,   -0.002310996f,   -0.010835843f,    0.007043575f,
       0.003680081f,   -0.006464003f,    0.000818926f,    0.003691021f,
      -0.002289456f,   -0.001132711f,    0.001868278f,   -0.000220147f,
      -0.000912497f,    0.000513324f,    0.000226258f,   -0.000324593f,
       0.000032149f,    0.000106238f,   -0.000043438f,   -0.000011361f,
  },
  {
      -0.000011361f,   -0.000043438f,    0.000106238f,    0.000032149f,
      -0.000324593f,    0.000226258f,    0.000513324f,   -0.000912497f,
      -0.000220147f,    0.001868278f,   -0.001132711f,   -0.002289456f,
       0.003691021f,    0.000818926f,   -0.006464003f,    0.003680081f,
       0.007043575f,   -0.010835843f,   -0.002310996f,    0.017663630f,
      -0.009813263f,   -0.018486293f,    0.028274087f,    0.006070506f,
      -0.047487846f,    0.027640471f,    0.056545904f,   -0.099883928f,
      -0.028088295f,    0.418199159f,    0.577895385f,    0.178880418f,
      -0.123381542f,   -0.014650862f,    0.068513027f,   -0.024478139f,
      -0.030479441f,    0.031228092f,    0.004502799f,   -0.023473557f,
       0.008926677f,    0.011497083f,   -0.011949152f,   -0.001723026f,
       0.008883223f,   -0.003310240f,   -0.004144058f,    0.004155242f,
       0.000573861f,   -0.002812568f,    0.000988422f,    0.001156616f,
      -0.001072870f,   -0.000135370f,    0.000596735f,   -0.000184781f,
      -0.000185186f,    0.000141065f,    0.000013648f,   -0.000040443f,
  },
};

#else  /* CLOUDS_SRC_TAPS == 60 */

/* 60 taps, Kaiser beta=7, -6 dB near 12500 Hz, designed at 96000 Hz.
 * Response: 1k -0.0 dB, 10k -0.2 dB, 12k -3.9 dB, 13k -8.9 dB, 14k -17.3 dB, 15k -31.3 dB, 16k -61.4 dB, 17k -86.5 dB, 18k -88.6 dB, 20k -81.1 dB
 * 16 kHz is the engine's Nyquist and so the figure that bounds both
 * the aliasing folded in and the imaging let out. */

static const float kSrcUpPhase[3][20] = {
  {
        -0.000337716f,      0.002227496f,     -0.005681324f,      0.006785469f,
         0.002697106f,     -0.029893369f,      0.070996795f,     -0.103033636f,
         0.071565468f,      0.758910957f,      0.331932610f,     -0.151494306f,
         0.053086281f,      0.000815224f,     -0.018548082f,      0.015365663f,
        -0.006779230f,      0.001223782f,      0.000339335f,     -0.000161363f,
  },
  {
        -0.000385359f,      0.001506034f,     -0.001927022f,     -0.002410855f,
         0.015487788f,     -0.034575919f,      0.044139652f,     -0.015104100f,
        -0.101130388f,      0.594383011f,      0.594383011f,     -0.101130388f,
        -0.015104100f,      0.044139652f,     -0.034575919f,      0.015487788f,
        -0.002410855f,     -0.001927022f,      0.001506034f,     -0.000385359f,
  },
  {
        -0.000161363f,      0.000339335f,      0.001223782f,     -0.006779230f,
         0.015365663f,     -0.018548082f,      0.000815224f,      0.053086281f,
        -0.151494306f,      0.331932610f,      0.758910957f,      0.071565468f,
        -0.103033636f,      0.070996795f,     -0.029893369f,      0.002697106f,
         0.006785469f,     -0.005681324f,      0.002227496f,     -0.000337716f,
  },
};

static const float kSrcDownPhase[2][30] = {
  {
        -0.000256906f,      0.000226223f,      0.001484997f,     -0.001284682f,
        -0.004519487f,      0.004523646f,      0.010325192f,     -0.012365388f,
        -0.019928913f,      0.029426434f,      0.035390854f,     -0.068689091f,
        -0.067420259f,      0.221288406f,      0.505940638f,      0.396255341f,
         0.047710312f,     -0.100996204f,     -0.010069400f,      0.047331197f,
         0.000543483f,     -0.023050613f,      0.001798070f,      0.010243775f,
        -0.001607236f,     -0.003787549f,      0.000815855f,      0.001004022f,
        -0.000225144f,     -0.000107575f,
  },
  {
        -0.000107575f,     -0.000225144f,      0.001004022f,      0.000815855f,
        -0.003787549f,     -0.001607236f,      0.010243775f,      0.001798070f,
        -0.023050613f,      0.000543483f,      0.047331197f,     -0.010069400f,
        -0.100996204f,      0.047710312f,      0.396255341f,      0.505940638f,
         0.221288406f,     -0.067420259f,     -0.068689091f,      0.035390854f,
         0.029426434f,     -0.019928913f,     -0.012365388f,      0.010325192f,
         0.004523646f,     -0.004519487f,     -0.001284682f,      0.001484997f,
         0.000226223f,     -0.000256906f,
  },
};

#endif  /* CLOUDS_SRC_TAPS */

/* Forward dot product of `n` taps against `n` consecutive samples.
 *
 * Both runs are forward because the coefficient tables above are stored
 * reversed; the alternative -- coefficients forward, samples backward -- cost
 * a vrev64q and a vcombine per multiply-accumulate on NEON, which is three
 * vector ops to do the work of one.
 *
 * Two accumulators, because a single one serialises on the FMA's result
 * latency and the Cortex-A7 has nothing else to issue in the gap.
 *
 * The scalar tail is not decoration.  At the default 120 taps every branch
 * (40 up, 60 down) is a multiple of 4 and the tail never runs; at
 * CLOUDS_SRC_TAPS=60 the down branch is 30, so it runs two scalar
 * multiply-adds per output sample.  That is the cheapest of the shapes on
 * offer -- keeping the branch a multiple of 4 would mean 120/6=20 down-taps,
 * which costs a third of the prototype's length for two instructions. */
static inline float Dot(const float *h, const float *x, int n) {
#ifdef __ARM_NEON
  float32x4_t a0 = vdupq_n_f32(0.0f);
  float32x4_t a1 = vdupq_n_f32(0.0f);
  int k = 0;
  for (; k + 8 <= n; k += 8) {
    a0 = vmlaq_f32(a0, vld1q_f32(h + k), vld1q_f32(x + k));
    a1 = vmlaq_f32(a1, vld1q_f32(h + k + 4), vld1q_f32(x + k + 4));
  }
  for (; k + 4 <= n; k += 4)
    a0 = vmlaq_f32(a0, vld1q_f32(h + k), vld1q_f32(x + k));
  const float32x4_t acc = vaddq_f32(a0, a1);
  float32x2_t s = vadd_f32(vget_low_f32(acc), vget_high_f32(acc));
  float sum = vget_lane_f32(vpadd_f32(s, s), 0);
  for (; k < n; ++k) sum += h[k] * x[k];
  return sum;
#else
  float sum = 0.0f;
  for (int k = 0; k < n; ++k) sum += h[k] * x[k];
  return sum;
#endif
}

/* One mono stream, 32 kHz -> 48 kHz.
 *
 * Pull-driven: ask InputNeeded() how many 32 kHz samples the next `nout`
 * output samples require, write exactly that many into Input(), then call
 * Process().  Keeping the caller in charge of the count is what lets the
 * synth run an engine block only when one is actually needed. */
class SrcUp {
 public:
  void Init() {
    memset(buf_, 0, sizeof(buf_));
    phase_ = 0;
  }

  int InputNeeded(int nout) const { return (phase_ + nout * 2) / 3; }

  /* Where the caller writes InputNeeded() samples, oldest first. */
  float *Input() { return &buf_[kUpTaps]; }

  void Process(float *out, int nout, int stride) {
    /* head walks the oldest sample of the tap window forward. */
    const float *head = buf_;
    int phase = phase_;
    for (int n = 0; n < nout; ++n) {
      *out = Dot(kSrcUpPhase[phase], head, kUpTaps);
      out += stride;
      phase += 2;
      if (phase >= 3) { phase -= 3; ++head; }
    }
    /* Slide the tail back so the next block starts from the same offset. */
    const int consumed = (int)(head - buf_);
    memmove(buf_, buf_ + consumed, kUpTaps * sizeof(float));
    phase_ = phase;
  }

 private:
  float buf_[kUpTaps + kMaxIn];
  int phase_;
};

/* One mono stream, 48 kHz -> 32 kHz.  Same contract as SrcUp. */
class SrcDown {
 public:
  void Init() {
    memset(buf_, 0, sizeof(buf_));
    phase_ = 0;
  }

  int InputNeeded(int nout) const { return (phase_ + nout * 3) / 2; }

  float *Input() { return &buf_[kDownTaps]; }

  void Process(float *out, int nout, int stride) {
    const float *head = buf_;
    int phase = phase_;
    for (int n = 0; n < nout; ++n) {
      *out = Dot(kSrcDownPhase[phase], head, kDownTaps);
      out += stride;
      phase += 3;
      while (phase >= 2) { phase -= 2; ++head; }
    }
    const int consumed = (int)(head - buf_);
    memmove(buf_, buf_ + consumed, kDownTaps * sizeof(float));
    phase_ = phase;
  }

 private:
  float buf_[kDownTaps + kMaxIn];
  int phase_;
};

}  /* namespace clouds_src */

#endif  /* CLOUDS_SRC_H_ */
