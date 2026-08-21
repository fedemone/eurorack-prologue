/*
 * File: test_clouds_pvoc_worker.cc
 *
 * Reports how the phase vocoder's transforms were scheduled, and what the
 * output was, for one build of the scheduling switches.
 *
 * This is the counter half of the worker's evidence; the hashes come from
 * test_clouds_pvoc_rr.cc, which this file deliberately does not duplicate.
 * What it adds is the question the hashes cannot answer: did the transform
 * actually run where the build says it should, or did it end up back on the
 * audio thread?  A build where the worker silently never keeps up produces
 * correct audio -- the catch-up valve sees to that -- and buys nothing, and
 * the two cases are indistinguishable from the output alone.
 *
 * The counters are printed rather than asserted, because what they should be
 * depends on how the caller drives the engine:
 *
 *   - Driven flat out, as a host test does, the worker is starved by
 *     construction: the "audio thread" issues a thousand blocks in the time
 *     one transform takes, so the valve reclaims nearly everything.  A high
 *     forced count here means the harness has no wall clock, not that the
 *     code is wrong.
 *
 *   - Driven at the sample rate, which is what `paced` below does and what
 *     hardware does, one hop is 8 ms and one transform is a fraction of a
 *     millisecond, so nothing should be forced at all.
 *
 * Build/run: make test-clouds-pvoc-worker
 */

#include "clouds/dsp/granular_processor.h"
#include "stmlib/utils/random.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <ctime>

using namespace clouds;

namespace clouds {
#if CLOUDS_PVOC_WORKER
extern uint32_t g_pvoc_worker_ran;
extern uint32_t g_pvoc_worker_forced;
#endif
}

static const size_t kLargeBufferSize = 118784;
static const size_t kSmallBufferSize = 65536;
alignas(16) static uint8_t large_buffer_[kLargeBufferSize];
alignas(16) static uint8_t small_buffer_[kSmallBufferSize];
static GranularProcessor processor_;

static int failures_ = 0;

static uint64_t hash_init(void) { return 1469598103934665603ULL; }
static void hash_frames(uint64_t *h, const ShortFrame *f, size_t n) {
  const uint8_t *p = reinterpret_cast<const uint8_t *>(f);
  for (size_t i = 0; i < n * sizeof(ShortFrame); ++i) {
    *h ^= p[i];
    *h *= 1099511628211ULL;
  }
}

static void engine_init(void) {
  processor_.Quiesce();
  stmlib::Random::Seed(0x12345678u);
  memset(large_buffer_, 0, kLargeBufferSize);
  memset(small_buffer_, 0, kSmallBufferSize);
  processor_.Init(large_buffer_, kLargeBufferSize,
                  small_buffer_, kSmallBufferSize);
  processor_.set_playback_mode(PLAYBACK_MODE_SPECTRAL);
  processor_.set_quality(0);
  processor_.set_bypass(false);
  processor_.set_silence(false);
  Parameters *p = processor_.mutable_parameters();
  p->position = 0.5f; p->size = 0.5f; p->pitch = 0.0f; p->density = 0.5f;
  p->texture = 0.5f; p->dry_wet = 0.9995f; p->stereo_spread = 0.5f;
  p->feedback = 0.0f; p->reverb = 0.0f;
  p->freeze = false; p->gate = true; p->trigger = false;
  processor_.Prepare();
}

/* Render `blocks` blocks.  With `paced`, wait out the wall-clock time each
 * block represents, so the worker gets the budget the ring geometry assumes
 * -- one hop, which at 32 kHz and a 32-sample block is 8 ms. */
static uint64_t run(int blocks, uint32_t seed, bool paced) {
  ShortFrame in[kMaxBlockSize], out[kMaxBlockSize];
  uint32_t st = seed;
  uint64_t h = hash_init();
  const long block_ns = (long)(1e9 * (double)kMaxBlockSize / 32000.0);
  struct timespec next;
  clock_gettime(CLOCK_MONOTONIC, &next);
  for (int b = 0; b < blocks; ++b) {
    for (size_t i = 0; i < kMaxBlockSize; ++i) {
      st = st * 1664525u + 1013904223u;
      const int16_t s = (int16_t)((int32_t)(st >> 16) - 32768) / 3;
      in[i].l = s;
      in[i].r = (int16_t)(-s / 2);
    }
    processor_.Prepare();
    processor_.Process(in, out, kMaxBlockSize);
    hash_frames(&h, out, kMaxBlockSize);
    if (paced) {
      next.tv_nsec += block_ns;
      while (next.tv_nsec >= 1000000000L) {
        next.tv_nsec -= 1000000000L;
        ++next.tv_sec;
      }
      clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
    }
  }
  return h;
}

int main(void) {
  printf("# CLOUDS_FFT_SIZE=%d CLOUDS_PVOC_WORKER=%d CLOUDS_PVOC_WORKER_SYNC=%d\n",
         (int)kMaxFftSize, CLOUDS_PVOC_WORKER, CLOUDS_PVOC_WORKER_SYNC);

  /* Paced: the regime the device runs in, and the only one where the worker
   * keeping up is a claim worth making. */
  engine_init();
  run(120, 12345u, true);                       /* settle */
#if CLOUDS_PVOC_WORKER
  g_pvoc_worker_ran = 0;
  g_pvoc_worker_forced = 0;
#endif
  const uint64_t paced_hash = run(600, 999u, true);
  printf("H paced %016llx\n", (unsigned long long)paced_hash);
#if CLOUDS_PVOC_WORKER
  printf("C paced worker %u forced %u\n",
         g_pvoc_worker_ran, g_pvoc_worker_forced);
  if (g_pvoc_worker_ran == 0) {
    printf("FAIL the worker ran no transforms at all -- "
           "it is not doing the work this build claims it does\n");
    ++failures_;
  }
#else
  printf("C paced worker 0 forced 0\n");
#endif

  printf("\n%s\n", failures_ ? "=== FAILURES ===" : "=== ok ===");
  return failures_ ? 1 : 0;
}
