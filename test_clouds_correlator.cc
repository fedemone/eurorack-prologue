/*
 * File: test_clouds_correlator.cc
 *
 * The WSOLA correlator's NEON scoring loop against upstream's scalar one.
 *
 * eurorack-opt/clouds/dsp/correlator.cc scores each splice candidate with
 * VCNT on ARM, four words at a time, instead of a word at a time with a SWAR
 * popcount. The claim that makes that safe is that it is the same integer --
 * not a close one -- so the check is exact: every candidate's score, not just
 * the best match, because two candidates can tie and a best-match comparison
 * would then pass with a score that is off.
 *
 * The reference is written out here rather than taken from the fork, so it
 * still means something if the fork's scalar path is ever touched. It is
 * upstream's loop with the offset_bits == 0 case made explicit, which is what
 * ARM computes and what the fork's scalar path computes on every target.
 *
 * Every window size the correlator can be given is a candidate count and a
 * word count; the sizes below cover word counts that are and are not
 * multiples of four (the NEON loop's scalar tail), the smallest window, and
 * kMaxWSOLASize. Every candidate covers every bit offset, including the
 * word-aligned ones where the second shift would be by 32. The buffers are
 * all-zero, all-one and random, so a score that saturates or a lane that is
 * dropped cannot hide.
 *
 * Off ARM both sides are the scalar path, so the host run proves only that
 * the refactor left it alone; the ARM run under QEMU is the one that
 * exercises the vector code.
 *
 * Build/run: make test-clouds-correlator   (host, plus ARM under QEMU if
 *                                           available)
 */

#include <inttypes.h>
#include <stddef.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

/* Score readout: the correlator keeps the running best score and the
 * candidate cursor private, and they are the things this test has to set and
 * read. Opened up here only, after every system header is in. */
#define private public
#include "clouds/dsp/correlator.h"
#undef private

#include "clouds/dsp/wsola_sample_player.h"

static uint32_t reference_score(const uint32_t* source,
                                const uint32_t* destination,
                                uint32_t size, uint32_t candidate) {
  const uint32_t num_words = size >> 5;
  const uint32_t offset_bits = candidate & 0x1f;
  const uint32_t* d = &destination[candidate >> 5];
  uint32_t xcorr = 0;
  for (uint32_t i = 0; i < num_words; ++i) {
    uint32_t bits = d[i] << offset_bits;
    if (offset_bits) bits |= d[i + 1] >> (32 - offset_bits);
    uint32_t match = ~(source[i] ^ bits);
    uint32_t count = 0;
    while (match) {
      match &= match - 1;
      ++count;
    }
    xcorr += count;
  }
  return xcorr;
}

int main() {
  /* Laid out as GranularProcessor::Prepare() lays it out: source, then a
   * destination twice as long, each block kMaxWSOLASize / 32 + 2 words. */
  const uint32_t block = clouds::kMaxWSOLASize / 32 + 2;
  static uint32_t buffer[block * 3];
  const int sizes[] = {32, 64, 96, 128, 160, 224, 256, 480, 512,
                       1000, 1024, 2048, 3000, clouds::kMaxWSOLASize};
  const char* const fills[] = {"zero", "ones", "random", "random", "random"};

  unsigned checks = 0, failures = 0;
  srand(1);
  for (unsigned f = 0; f < sizeof(fills) / sizeof(fills[0]); ++f) {
    for (int size : sizes) {
      for (uint32_t i = 0; i < block * 3; ++i) {
        buffer[i] = f == 0 ? 0u
                  : f == 1 ? 0xffffffffu
                  : (static_cast<uint32_t>(rand()) << 16) ^
                        static_cast<uint32_t>(rand());
      }
      clouds::Correlator correlator;
      correlator.Init(&buffer[0], &buffer[block]);
      correlator.StartSearch(size, 0, 1 << 16);

      /* Each candidate on its own: reset the running best so the score the
       * correlator records is exactly this candidate's. */
      uint32_t best = 0, best_score = 0;
      for (int k = 0; k < size; ++k) {
        const uint32_t expected =
            reference_score(&buffer[0], &buffer[block], size, k);
        if (expected > best_score) {
          best_score = expected;
          best = k;
        }
        correlator.candidate_ = k;
        correlator.best_score_ = 0;
        correlator.done_ = false;
        correlator.EvaluateNextCandidate();
        ++checks;
        if (correlator.best_score_ != expected && ++failures <= 10) {
          printf("  FAIL %-6s size %4d candidate %4d: %u, expected %u\n",
                 fills[f], size, k, correlator.best_score_, expected);
        }
      }

      /* And the search as Prepare() runs it, start to finish. With offset 0
       * and increment 1 << 16, best_match() is the candidate index. */
      correlator.StartSearch(size, 0, 1 << 16);
      while (!correlator.done()) correlator.EvaluateSomeCandidates();
      ++checks;
      if (static_cast<uint32_t>(correlator.best_match()) != best &&
          ++failures <= 10) {
        printf("  FAIL %-6s size %4d: best match %d, expected %u\n",
               fills[f], size, correlator.best_match(), best);
      }
    }
  }

#ifdef __ARM_NEON
  const char* path = "NEON";
#else
  const char* path = "scalar";
#endif
  printf("Correlator (%s path): %u/%u scores and best matches agree with the "
         "scalar reference\n", path, checks - failures, checks);
  if (failures) {
    printf("\n=== %u FAILURES ===\n", failures);
    return 1;
  }
  printf("\n=== ALL PASS (0 failures) ===\n");
  return 0;
}
