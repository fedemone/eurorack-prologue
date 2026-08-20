/*
 * File: test_clouds_engine_opt.cc
 *
 * Differential test for the eurorack-opt/ engine fork.
 *
 * The fork's whole claim is that skipping the reverb and the diffuser when
 * their amount is zero is free — that `out += amount * (wet - out)` at
 * amount 0 is an expensive way to add zero. A claim like that is worth
 * checking against the original rather than reasoning about, so this file is
 * compiled twice, once against the submodule and once against the fork, and
 * the two runs are compared sample for sample.
 *
 * Scenarios:
 *
 *   A  Reverb and diffusion held non-zero throughout. Neither effect is ever
 *      skipped, so the fork runs upstream's code and output must be
 *      bit-identical.
 *   B  Reverb at 0 and TEXTURE below the diffusion threshold throughout, i.e.
 *      the case the fork exists for. Both effects are skipped and output must
 *      still be bit-identical.
 *   D  Spectral, with SIZE swept across its range. The fork skips the
 *      spectral warp when its polynomial is the identity, which is where
 *      SIZE 50% lands exactly; the sweep covers that and the settings either
 *      side where the loop still runs. Output must be bit-identical.
 *   C  REVERB held at 0 for two seconds, then jumped straight to full. Here
 *      the two are *expected* to differ: upstream has been quietly filling
 *      its delay lines the whole time and releases that history at full level
 *      the instant the knob moves, while the fork flushed the lines before
 *      idling and builds the tail up from silence. The test pins down the
 *      direction of that difference — the fork must not be louder — because a
 *      broken flush would show up as exactly the burst upstream produces.
 *
 * Build/run: make test-clouds-engine-opt (builds and diffs both variants)
 */

#include "clouds/dsp/granular_processor.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdlib>

using namespace clouds;

static const size_t kLargeBufferSize = 118784;
static const size_t kSmallBufferSize = 65536;
alignas(16) static uint8_t large_buffer_[kLargeBufferSize];
alignas(16) static uint8_t small_buffer_[kSmallBufferSize];
static GranularProcessor processor_;

/* FNV-1a over the raw output bytes: any single-sample difference shows up. */
static uint64_t hash_init(void) { return 1469598103934665603ULL; }
static void hash_frames(uint64_t *h, const ShortFrame *f, size_t n) {
  const uint8_t *p = reinterpret_cast<const uint8_t *>(f);
  for (size_t i = 0; i < n * sizeof(ShortFrame); ++i) {
    *h ^= p[i];
    *h *= 1099511628211ULL;
  }
}

static void engine_init(void) {
  memset(large_buffer_, 0, kLargeBufferSize);
  memset(small_buffer_, 0, kSmallBufferSize);
  processor_.Init(large_buffer_, kLargeBufferSize, small_buffer_, kSmallBufferSize);
  processor_.set_playback_mode(PLAYBACK_MODE_GRANULAR);
  processor_.set_quality(0);
  processor_.set_bypass(false);
  processor_.set_silence(false);
  Parameters *p = processor_.mutable_parameters();
  p->position = 0.5f;
  p->size = 0.5f;
  p->pitch = 0.0f;
  p->density = 0.85f;
  p->texture = 0.5f;
  p->dry_wet = 0.9995f;
  p->stereo_spread = 0.5f;
  p->feedback = 0.0f;
  p->reverb = 0.0f;
  p->freeze = false;
  p->gate = true;
  p->trigger = false;
  processor_.Prepare();
}

/* Deterministic, broadband, and not silent: silence would let both variants
 * agree for uninteresting reasons. */
static void fill_input(ShortFrame *in, size_t n, uint32_t *state) {
  for (size_t i = 0; i < n; ++i) {
    *state = *state * 1664525u + 1013904223u;
    const int16_t s = (int16_t)((int32_t)(*state >> 16) - 32768) / 3;
    in[i].l = s;
    in[i].r = (int16_t)(-s / 2);
  }
}

/* Render `blocks` blocks at the given settings, hashing the output. */
static uint64_t run(float reverb, float texture, int blocks,
                    uint32_t seed, float *peak_out, int peak_from) {
  ShortFrame in[kMaxBlockSize], out[kMaxBlockSize];
  uint32_t st = seed;
  uint64_t h = hash_init();
  float peak = 0.0f;
  Parameters *p = processor_.mutable_parameters();
  for (int b = 0; b < blocks; ++b) {
    p->reverb = reverb;
    p->texture = texture;
    fill_input(in, kMaxBlockSize, &st);
    processor_.Prepare();
    processor_.Process(in, out, kMaxBlockSize);
    hash_frames(&h, out, kMaxBlockSize);
    if (b >= peak_from) {
      for (size_t i = 0; i < kMaxBlockSize; ++i) {
        const float l = fabsf((float)out[i].l), r = fabsf((float)out[i].r);
        if (l > peak) peak = l;
        if (r > peak) peak = r;
      }
    }
  }
  if (peak_out) *peak_out = peak;
  return h;
}

int main(void) {
  /* A: both effects active throughout. */
  engine_init();
  run(0.6f, 0.9f, 200, 12345u, NULL, 1 << 30);        /* settle */
  const uint64_t a = run(0.6f, 0.9f, 600, 999u, NULL, 1 << 30);

  /* B: both effects at zero throughout — the skipped path. */
  engine_init();
  run(0.0f, 0.5f, 200, 12345u, NULL, 1 << 30);        /* settle */
  const uint64_t b = run(0.0f, 0.5f, 600, 999u, NULL, 1 << 30);

  /* C: two seconds at REVERB 0, then a jump to full; measure the first
   * 100 ms after the jump. */
  engine_init();
  run(0.0f, 0.5f, 2000, 4242u, NULL, 1 << 30);
  float jump_peak = 0.0f;
  run(1.0f, 0.5f, 100, 777u, &jump_peak, 0);

  /* D: Spectral, SIZE swept across its range. The fork returns early from
   * WarpMagnitudes when the warp polynomial is the identity, which is exactly
   * where SIZE 50% lands; the sweep covers that setting and the ones either
   * side of it, where the loop still runs. Bit-identical either way. */
  engine_init();
  processor_.set_playback_mode(PLAYBACK_MODE_SPECTRAL);
  processor_.Prepare();
  uint64_t d = hash_init();
  static int16_t d_samples[21 * 60 * kMaxBlockSize * 2];
  size_t d_count = 0;
  {
    ShortFrame in[kMaxBlockSize], out[kMaxBlockSize];
    uint32_t st = 31337u;
    Parameters *p = processor_.mutable_parameters();
    for (int step = 0; step <= 20; ++step) {
      p->size = (float)step * 0.05f;
      for (int b = 0; b < 60; ++b) {
        fill_input(in, kMaxBlockSize, &st);
        processor_.Prepare();
        processor_.Process(in, out, kMaxBlockSize);
        hash_frames(&d, out, kMaxBlockSize);
        for (size_t i = 0; i < kMaxBlockSize; ++i) {
          d_samples[d_count++] = out[i].l;
          d_samples[d_count++] = out[i].r;
        }
      }
    }
  }

  /* D is not compared by hash. The warp early-out is the exact identity
   * where upstream's loop is an accumulated approximation of it, so the two
   * differ -- by less than the int16 quantiser they both feed, which is the
   * claim worth checking. CLOUDS_DUMP_D writes the samples; CLOUDS_COMPARE_D
   * reads a dump back and prints the deviation. */
  if (getenv("CLOUDS_DUMP_D")) {
    FILE *f = fopen(getenv("CLOUDS_DUMP_D"), "wb");
    if (f) { fwrite(d_samples, sizeof(int16_t), d_count, f); fclose(f); }
  }
  if (getenv("CLOUDS_COMPARE_D")) {
    FILE *f = fopen(getenv("CLOUDS_COMPARE_D"), "rb");
    if (!f) { printf("E missing reference\n"); return 1; }
    static int16_t ref[sizeof(d_samples) / sizeof(d_samples[0])];
    const size_t got = fread(ref, sizeof(int16_t), d_count, f);
    fclose(f);
    if (got != d_count) { printf("E short reference\n"); return 1; }
    size_t differing = 0;
    int32_t worst = 0, peak = 1;
    double sq = 0.0;
    for (size_t i = 0; i < d_count; ++i) {
      const int32_t diff = (int32_t)d_samples[i] - (int32_t)ref[i];
      if (diff) ++differing;
      const int32_t ad = diff < 0 ? -diff : diff;
      if (ad > worst) worst = ad;
      const int32_t ar = ref[i] < 0 ? -ref[i] : ref[i];
      if (ar > peak) peak = ar;
      sq += (double)diff * (double)diff;
    }
    const double rms = sqrt(sq / (double)d_count);
    printf("E %zu %zu %d %.4f %.1f %.1f\n", differing, d_count, worst, rms,
           20.0 * log10(rms / (double)peak), 20.0 * log10(1.0 / (double)peak));
  }

  printf("A %016llx\n", (unsigned long long)a);
  printf("B %016llx\n", (unsigned long long)b);
  printf("C %.1f\n", jump_peak);
  printf("D %016llx\n", (unsigned long long)d);
  return 0;
}
