/*
 * File: test_clouds_fx_preset.cc
 *
 * Reproduction for a hardware report: CloudsFX in Stretch went to clicks and
 * then silence, seconds after the Quality knob moved StHi -> MoHi.
 *
 * The settings are the ones reported, not a sweep and not defaults:
 *
 *   Position 67  Size 26  Density 55  Texture 37  Pitch 24 (= 0 semitones)
 *   Feedback 14  Dry/Wet 50  Reverb 16  Freeze off  Mode Stretch
 *   Quality StHi, then MoHi
 *
 * Two of those are not incidental.  Feedback 14% routes output back into the
 * input, so the unit is a loop and any level or state problem compounds
 * rather than decaying.  Size 26% is a short WSOLA window, which schedules
 * windows *more* often than a large one -- so the mode's per-window work runs
 * at its highest rate here, not its lowest.
 *
 * Silence is the whole point.  It is the one symptom the existing tests were
 * built to catch (test_clouds_fx_reconfig.cc checks the renderer is never
 * stuck parked) and the one that has several possible causes that look
 * identical from outside: the renderer never acknowledged the park, the
 * reconfiguration was abandoned at the hard ceiling, the control thread is
 * still inside Quiesce() waiting for a worker, or the engine came back from
 * the reconfiguration genuinely silent.  clouds-fx.cc exports a counter for
 * each branch under CLOUDS_FX_TEST so a failure here says which.
 *
 * Note what this harness cannot see.  It runs the unit alone; the report has
 * it on an FX bus with a kit and a second reverb, so any CPU margin here is
 * more generous than the one that failed.  A pass is therefore not proof the
 * hardware is fine -- it narrows the cause to something this harness does not
 * model, which is a useful answer but a different one.
 *
 * Build/run: make test-clouds-fx-preset        (worker off)
 *            make test-clouds-fx-preset-worker (worker on, as it ships)
 */

#include "clouds/dsp/pvoc/phase_vocoder.h"   /* CLOUDS_PVOC_WORKER */

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

extern uint32_t g_park_calls;
extern uint32_t g_park_normal;
extern uint32_t g_park_idle_take;
extern uint32_t g_park_abandoned;
extern uint32_t g_park_max_wait_us;
}

namespace clouds {
#if CLOUDS_PVOC_WORKER
extern std::atomic<uint32_t> g_pvoc_worker_ran;
extern std::atomic<uint32_t> g_pvoc_worker_forced;
#endif
}

static int g_fail = 0;
#define CHECK(cond, ...)                            \
  do {                                              \
    if (!(cond)) { g_fail++; printf("  FAIL: "); }   \
    else printf("  ok:   ");                        \
    printf(__VA_ARGS__);                            \
    printf("\n");                                   \
  } while (0)

static std::atomic<bool> stop_{false};
static std::atomic<long> blocks_{0};
static std::atomic<long> bad_{0};
static std::atomic<long> silent_blocks_{0};
static std::atomic<long> max_silent_run_{0};
static std::atomic<long> peak_milli_{0};   /* peak |out|, x1000, as an integer */

/* The audio thread, paced at roughly real time.
 *
 * Pacing matters here in a way it does not for a throughput test.  The park
 * handshake's timeouts are in wall-clock milliseconds -- 10 ms to decide
 * nothing is rendering, 50 ms to give up -- so a harness that renders flat
 * out or far too slowly exercises different branches than the instrument
 * does.  64 frames at 48 kHz is 1.33 ms, so that is the sleep. */
static void audio_thread(uint32_t frames) {
  static float in[128 * 2], out[128 * 2];
  double ph = 0.0;
  long run = 0;
  auto next = std::chrono::steady_clock::now();
  while (!stop_.load(std::memory_order_relaxed)) {
    /* A drum-loop-ish input: a decaying tone burst every ~0.25 s, which is
     * closer to what an FX bus carries than a steady sine, and gives the
     * feedback loop something with transients in it. */
    const long b = blocks_.load(std::memory_order_relaxed);
    const double env = exp(-3.0 * fmod(b * frames / 48000.0, 0.25));
    for (uint32_t i = 0; i < frames; ++i) {
      float x = (float)(env * (0.5 * sin(ph) + 0.25 * sin(ph * 2.7)));
      ph += 0.05;
      in[i * 2] = x;
      in[i * 2 + 1] = -x * 0.8f;
    }
    clouds_fx_process(in, out, frames);
    blocks_.fetch_add(1, std::memory_order_relaxed);

    double peak = 0.0;
    for (uint32_t i = 0; i < frames * 2; ++i) {
      const float v = out[i];
      if (!(v == v) || v > 4.0f || v < -4.0f)
        bad_.fetch_add(1, std::memory_order_relaxed);
      if (fabs(v) > peak) peak = fabs(v);
    }
    const long pm = (long)(peak * 1000.0);
    if (pm > peak_milli_.load(std::memory_order_relaxed))
      peak_milli_.store(pm, std::memory_order_relaxed);

    if (peak < 1e-6) {
      silent_blocks_.fetch_add(1, std::memory_order_relaxed);
      if (++run > max_silent_run_.load(std::memory_order_relaxed))
        max_silent_run_.store(run, std::memory_order_relaxed);
    } else {
      run = 0;
    }

    next += std::chrono::microseconds(frames * 1000000ull / 48000ull);
    std::this_thread::sleep_until(next);
  }
}

/* The reported preset.  Ids are the CloudsFX param ids in header.c. */
static void apply_preset(void) {
  clouds_fx_set_param(0, 67);   /* Position */
  clouds_fx_set_param(1, 26);   /* Size     */
  clouds_fx_set_param(2, 55);   /* Density  */
  clouds_fx_set_param(3, 37);   /* Texture  */
  clouds_fx_set_param(4, 24);   /* Pitch: 24 == 0 semitones */
  clouds_fx_set_param(5, 14);   /* Feedback */
  clouds_fx_set_param(6, 50);   /* Dry/Wet  */
  clouds_fx_set_param(7, 16);   /* Reverb   */
  clouds_fx_set_param(8, 0);    /* Freeze off */
}

static void reset_counters(void) {
  blocks_.store(0); bad_.store(0);
  silent_blocks_.store(0); max_silent_run_.store(0); peak_milli_.store(0);
  g_park_calls = g_park_normal = g_park_idle_take = g_park_abandoned = 0;
  g_park_max_wait_us = 0;
#if CLOUDS_PVOC_WORKER
  clouds::g_pvoc_worker_ran.store(0, std::memory_order_relaxed);
  clouds::g_pvoc_worker_forced.store(0, std::memory_order_relaxed);
#endif
}

static void report(const char *what, long expect_blocks) {
  printf("  (%ld blocks, %ld silent, longest silent run %ld, peak %.3f)\n",
         blocks_.load(), silent_blocks_.load(), max_silent_run_.load(),
         peak_milli_.load() / 1000.0);
  printf("  (parks: %u calls, %u acknowledged, %u idle-take, %u abandoned, "
         "longest wait %u us)\n",
         g_park_calls, g_park_normal, g_park_idle_take, g_park_abandoned,
         g_park_max_wait_us);
#if CLOUDS_PVOC_WORKER
  printf("  (worker: %u transforms, %u forced)\n",
         clouds::g_pvoc_worker_ran.load(std::memory_order_relaxed),
         clouds::g_pvoc_worker_forced.load(std::memory_order_relaxed));
#endif
  CHECK(blocks_.load() > expect_blocks, "%s: renderer kept up (%ld blocks)",
        what, blocks_.load());
  CHECK(bad_.load() == 0, "%s: no non-finite or out-of-range output (%ld)",
        what, bad_.load());
  /* A park costs a handful of blocks of silence.  64 blocks is 85 ms, well
   * past the 50 ms hard ceiling, so anything above it is not a handover. */
  CHECK(max_silent_run_.load() < 64,
        "%s: renderer never went quiet for long (longest run %ld blocks)",
        what, max_silent_run_.load());
  CHECK(g_park_abandoned == 0,
        "%s: no reconfiguration abandoned at the ceiling (%u)",
        what, g_park_abandoned);
  printf("\n");
}

int main(void) {
  printf("CloudsFX Reported-Preset Reproduction\n\n");
  printf("Stretch, Position 67 Size 26 Density 55 Texture 37 Pitch 0\n");
  printf("Feedback 14 Dry/Wet 50 Reverb 16, paced at real time.\n\n");

  /* --- 1. The preset alone, no reconfiguration, long enough to see a
   *        feedback loop misbehave if it is going to. --- */
  {
    printf("[Stretch at the reported settings, StHi, 6 s]\n");
    clouds_fx_init();
    clouds_fx_set_param(9, 1);    /* Mode = Stretch */
    clouds_fx_set_param(10, 0);   /* Quality = StHi */
    apply_preset();
    reset_counters();
    stop_.store(false);
    std::thread audio(audio_thread, 64);
    std::this_thread::sleep_for(std::chrono::seconds(6));
    stop_.store(true);
    audio.join();
    report("steady StHi", 3000);
  }

  /* --- 2. The transition that preceded the failure, once, with the engine
   *        already running and settled. --- */
  {
    printf("[StHi for 3 s, then StHi -> MoHi, then 6 s]\n");
    clouds_fx_init();
    clouds_fx_set_param(9, 1);
    clouds_fx_set_param(10, 0);
    apply_preset();
    reset_counters();
    stop_.store(false);
    std::thread audio(audio_thread, 64);
    std::this_thread::sleep_for(std::chrono::seconds(3));
    clouds_fx_set_param(10, 1);   /* Quality = MoHi */
    std::this_thread::sleep_for(std::chrono::seconds(6));
    stop_.store(true);
    audio.join();
    report("StHi -> MoHi", 4000);
    CHECK(clouds_fx_test_mode() == 1, "still in Stretch after the change");
    printf("\n");
  }

  /* --- 3. The same transition many times over, because once is a sample of
   *        one and the failure was reported after knob movement rather than
   *        at a particular value. --- */
  {
    printf("[StHi <-> MoHi every 250 ms for 8 s, knobs moving]\n");
    clouds_fx_init();
    clouds_fx_set_param(9, 1);
    clouds_fx_set_param(10, 0);
    apply_preset();
    reset_counters();
    stop_.store(false);
    std::thread audio(audio_thread, 64);

    int q = 0, flips = 0;
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < end) {
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
      q ^= 1;
      clouds_fx_set_param(10, q);
      ++flips;
      /* Knobs moving across the change, which is what was happening. */
      clouds_fx_set_param(0, 60 + (flips % 15));
      clouds_fx_set_param(1, 20 + (flips % 13));
      clouds_fx_set_param(5, 10 + (flips % 9));
    }
    stop_.store(true);
    audio.join();
    printf("  (%d quality changes)\n", flips);
    report("repeated StHi <-> MoHi", 4000);
  }

  printf("=== %s (%d failures) ===\n", g_fail ? "FAILED" : "ALL PASS", g_fail);
  return g_fail ? 1 : 0;
}
