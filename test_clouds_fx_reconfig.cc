/*
 * File: test_clouds_fx_reconfig.cc
 *
 * CloudsFX engine-reconfiguration regression test.
 *
 * Mode, Quality and unit_reset() all push GranularProcessor::Prepare() down
 * its reallocating path — around 180 KB of buffer clearing plus the pointer
 * re-seating that Process() reads through.  Doing that from the control
 * thread while the renderer is inside Process() was a verified crash; doing
 * it inside the render callback instead traded the crash for 180 KB of
 * memory traffic under an audio deadline.  clouds-fx.cc now parks the
 * renderer, reconfigures on the control thread, and hands the engine back.
 *
 * This test drives the unit the way the drumlogue does — one thread
 * rendering, one thread turning knobs — and checks the three things the
 * handshake has to guarantee:
 *
 *   1. The renderer never produces non-finite or wildly out-of-range output,
 *      which is what reading through a half-reallocated engine looks like
 *      when it does not segfault outright.
 *   2. The renderer always comes back.  A park costs a few blocks of
 *      silence; getting stuck parked would be the "correct sound, then
 *      silence" failure this replaces.
 *   3. Reconfiguration requests actually take effect.
 *
 * Build/run: make test-clouds-fx-reconfig
 */

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <atomic>
#include <thread>
#include <chrono>

extern "C" {
void clouds_fx_init(void);
void clouds_fx_request_reset(void);
void clouds_fx_set_param(uint8_t id, int32_t value);
void clouds_fx_process(const float *in, float *out, uint32_t frames);
int clouds_fx_test_mode(void);
}

static int g_fail = 0;
#define CHECK(cond, ...)                     \
  do {                                       \
    if (!(cond)) { g_fail++; printf("  FAIL: "); }  \
    else printf("  ok:   ");                 \
    printf(__VA_ARGS__);                     \
    printf("\n");                            \
  } while (0)

static std::atomic<bool> stop_{false};
static std::atomic<long> blocks_{0};
static std::atomic<long> bad_{0};
static std::atomic<long> max_silent_run_{0};

/* Roughly one audio callback: fill a buffer, render it, inspect the result.
 * Paced faster than real time so a short run still covers many blocks. */
static void audio_thread(uint32_t frames) {
  static float in[128 * 2], out[128 * 2];
  double ph = 0.0;
  long run = 0;
  while (!stop_.load(std::memory_order_relaxed)) {
    for (uint32_t i = 0; i < frames; ++i) {
      float x = (float)(0.6 * sin(ph) + 0.3 * sin(ph * 3.7));
      ph += 0.03;
      in[i * 2] = x;
      in[i * 2 + 1] = -x * 0.8f;
    }
    clouds_fx_process(in, out, frames);
    blocks_.fetch_add(1, std::memory_order_relaxed);

    double peak = 0.0;
    for (uint32_t i = 0; i < frames * 2; ++i) {
      float v = out[i];
      if (!(v == v) || v > 4.0f || v < -4.0f)
        bad_.fetch_add(1, std::memory_order_relaxed);
      if (fabs(v) > peak) peak = fabs(v);
    }
    if (peak < 1e-6) {
      if (++run > max_silent_run_.load(std::memory_order_relaxed))
        max_silent_run_.store(run, std::memory_order_relaxed);
    } else {
      run = 0;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(200));
  }
}

static uint32_t s_rnd = 987654321u;
static uint32_t rnd(void) {
  s_rnd = s_rnd * 1664525u + 1013904223u;
  return s_rnd >> 8;
}
static int rr(int lo, int hi) {
  return lo + (int)(rnd() % (uint32_t)(hi - lo + 1));
}

int main(void) {
  printf("CloudsFX Reconfiguration Tests\n\n");

  const uint32_t sizes[] = {32, 64, 128};
  for (unsigned si = 0; si < sizeof(sizes) / sizeof(sizes[0]); ++si) {
    const uint32_t frames = sizes[si];
    printf("[buffer = %u frames]\n", frames);

    clouds_fx_init();
    stop_.store(false);
    blocks_.store(0);
    bad_.store(0);
    max_silent_run_.store(0);

    std::thread audio(audio_thread, frames);

    int resets = 0, modes = 0, quals = 0, last_mode = 0;
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
    while (std::chrono::steady_clock::now() < end) {
      const int id = rr(0, 11);
      if (id == 11) {
        clouds_fx_request_reset();
        ++resets;
      } else {
        int v;
        switch (id) {
          case 4:  v = rr(0, 48); break;
          case 8:  v = rr(0, 1); break;
          case 9:  v = rr(0, 3); last_mode = v; ++modes; break;
          case 10: v = rr(0, 3); ++quals; break;
          default: v = rr(0, 100); break;
        }
        clouds_fx_set_param((uint8_t)id, v);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(rr(1, 8)));
    }

    /* Settle on a known mode so the "requests take effect" check is not
     * racing the last random write. */
    clouds_fx_set_param(9, 2);
    last_mode = 2;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    stop_.store(true);
    audio.join();

    printf("  (%ld blocks, %d resets, %d mode changes, %d quality changes)\n",
           blocks_.load(), resets, modes, quals);
    CHECK(blocks_.load() > 100, "renderer made progress (%ld blocks)",
          blocks_.load());
    CHECK(bad_.load() == 0, "no non-finite or out-of-range output (%ld bad)",
          bad_.load());
    CHECK(resets + modes + quals > 10,
          "reconfiguration was actually exercised (%d requests)",
          resets + modes + quals);
    /* A park is a handful of blocks.  Anything approaching a hundred means
     * the renderer stayed parked, which is the failure mode being tested. */
    CHECK(max_silent_run_.load() < 60,
          "renderer never stuck parked (longest silent run %ld blocks)",
          max_silent_run_.load());
    CHECK(clouds_fx_test_mode() == last_mode,
          "last mode request took effect (mode %d)", clouds_fx_test_mode());
    printf("\n");
  }

  printf("=== %s (%d failures) ===\n", g_fail ? "FAILED" : "ALL PASS", g_fail);
  return g_fail ? 1 : 0;
}
