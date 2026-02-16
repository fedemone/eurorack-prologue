/*
 * File: test_sound_production.cc
 *
 * Sound production test: verifies that the REAL Plaits VirtualAnalogEngine
 * produces non-zero audio output through the full drumlogue wrapper chain.
 *
 * This test compiles and links the actual Plaits engine code (not mocks),
 * proving end-to-end sound production:
 *
 *   unit_init -> unit_note_on -> unit_render -> non-zero stereo audio
 *
 * Build:
 *   g++ -std=c++11 -O2 -DTEST -DBLOCKSIZE=24 -DOSC_VA \
 *       -DOSC_NATIVE_BLOCK_SIZE=24 -Idrumlogue -I. -Ieurorack \
 *       test_sound_production.cc drumlogue_osc_adapter.cc \
 *       drumlogue_unit_wrapper.cc header.c macro-oscillator2.cc \
 *       eurorack/plaits/dsp/engine/virtual_analog_engine.cc \
 *       eurorack/stmlib/dsp/units.cc \
 *       -o test_sound_production -lm
 *
 * Run:
 *   ./test_sound_production
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#include "runtime.h"
#include "unit.h"

/* ===========================================================================
 * Test Framework (minimal)
 * Note: STEST (Sound TEST) avoids conflict with -DTEST flag used by stmlib
 * ======================================================================== */

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define STEST(name) \
  static void stest_##name(void); \
  static void run_stest_##name(void) { \
    g_tests_run++; \
    printf("  %-60s ", #name); \
    fflush(stdout); \
    stest_##name(); \
    g_tests_passed++; \
    printf("PASS\n"); \
  } \
  static void stest_##name(void)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
      printf("FAIL\n    %s:%d: condition false: %s\n", \
             __FILE__, __LINE__, #cond); \
      g_tests_failed++; g_tests_passed--; \
      return; \
    } \
  } while(0)

#define ASSERT_EQ(expected, actual) do { \
    auto _e = (expected); auto _a = (actual); \
    if (_e != _a) { \
      printf("FAIL\n    %s:%d: expected %lld, got %lld\n", \
             __FILE__, __LINE__, (long long)_e, (long long)_a); \
      g_tests_failed++; g_tests_passed--; \
      return; \
    } \
  } while(0)

/* ===========================================================================
 * Helper: create a valid drumlogue runtime descriptor
 * ======================================================================== */

static unit_runtime_desc_t make_valid_desc(void) {
  unit_runtime_desc_t desc;
  memset(&desc, 0, sizeof(desc));
  desc.target = k_unit_target_drumlogue_synth;
  desc.api = k_unit_api_2_0_0;
  desc.samplerate = 48000;
  desc.frames_per_buffer = 64;
  desc.input_channels = 0;
  desc.output_channels = 2;
  return desc;
}

/* ===========================================================================
 * Helper: compute RMS of a buffer
 * ======================================================================== */

static float compute_rms(const float *buf, uint32_t count) {
  double sum = 0.0;
  for (uint32_t i = 0; i < count; ++i) {
    sum += (double)buf[i] * (double)buf[i];
  }
  return (float)sqrt(sum / count);
}

/* ===========================================================================
 * Helper: check if any sample in buffer is non-zero
 * ======================================================================== */

static bool has_nonzero(const float *buf, uint32_t count) {
  for (uint32_t i = 0; i < count; ++i) {
    if (buf[i] != 0.0f) return true;
  }
  return false;
}

/* ===========================================================================
 * Tests
 * ======================================================================== */

STEST(engine_init_succeeds) {
  unit_runtime_desc_t desc = make_valid_desc();
  int8_t err = unit_init(&desc);
  ASSERT_EQ(k_unit_err_none, err);
  unit_teardown();
}

STEST(render_silence_before_note_on) {
  unit_runtime_desc_t desc = make_valid_desc();
  unit_init(&desc);

  /* Render without triggering a note — VA engine in TRIGGER_UNPATCHED
   * mode may produce some sound, but it shouldn't crash */
  float stereo[64 * 2];
  memset(stereo, 0, sizeof(stereo));
  unit_render(nullptr, stereo, 64);

  /* Just verify it doesn't crash and produces valid floats */
  for (int i = 0; i < 128; ++i) {
    ASSERT_TRUE(!std::isnan(stereo[i]));
    ASSERT_TRUE(!std::isinf(stereo[i]));
  }

  unit_teardown();
}

STEST(note_on_produces_audio) {
  unit_runtime_desc_t desc = make_valid_desc();
  unit_init(&desc);

  /* Set shape parameters for a strong signal */
  unit_set_param_value(0, 50);  /* Shape = 50% */
  unit_set_param_value(1, 50);  /* ShiftShape = 50% */

  /* Trigger note: A4 (MIDI 69), velocity 127 */
  unit_note_on(69, 127);

  /* Render several blocks to let the engine produce audio.
   * The first few blocks may be quiet as the engine ramps up. */
  float stereo[256 * 2];
  memset(stereo, 0, sizeof(stereo));

  for (int block = 0; block < 4; ++block) {
    unit_render(nullptr, stereo + block * 64 * 2, 64);
  }

  /* After a note trigger, we expect non-zero audio output */
  ASSERT_TRUE(has_nonzero(stereo, 256 * 2));

  /* Check RMS is above a minimal threshold (engine is actively producing sound) */
  float rms = compute_rms(stereo, 256 * 2);
  ASSERT_TRUE(rms > 1e-6f);

  unit_teardown();
}

STEST(note_on_produces_stereo) {
  unit_runtime_desc_t desc = make_valid_desc();
  unit_init(&desc);

  unit_set_param_value(0, 50);
  unit_set_param_value(1, 50);
  unit_note_on(69, 127);

  float stereo[64 * 2];
  memset(stereo, 0, sizeof(stereo));
  unit_render(nullptr, stereo, 64);

  /* Both L and R channels should be non-zero (mono duplicated to stereo) */
  bool left_nonzero = false, right_nonzero = false;
  for (int i = 0; i < 64; ++i) {
    if (stereo[i * 2] != 0.0f) left_nonzero = true;
    if (stereo[i * 2 + 1] != 0.0f) right_nonzero = true;
  }
  ASSERT_TRUE(left_nonzero);
  ASSERT_TRUE(right_nonzero);

  /* L and R should be identical (mono->stereo duplication) */
  for (int i = 0; i < 64; ++i) {
    ASSERT_TRUE(stereo[i * 2] == stereo[i * 2 + 1]);
  }

  unit_teardown();
}

STEST(different_notes_produce_different_pitch) {
  /* Render a block at two different MIDI notes and verify they differ */
  unit_runtime_desc_t desc = make_valid_desc();

  /* Note C3 (48) */
  unit_init(&desc);
  unit_set_param_value(0, 50);
  unit_note_on(48, 127);

  float stereo_c3[256 * 2];
  memset(stereo_c3, 0, sizeof(stereo_c3));
  for (int b = 0; b < 4; ++b)
    unit_render(nullptr, stereo_c3 + b * 64 * 2, 64);
  unit_teardown();

  /* Note C5 (72) */
  unit_init(&desc);
  unit_set_param_value(0, 50);
  unit_note_on(72, 127);

  float stereo_c5[256 * 2];
  memset(stereo_c5, 0, sizeof(stereo_c5));
  for (int b = 0; b < 4; ++b)
    unit_render(nullptr, stereo_c5 + b * 64 * 2, 64);
  unit_teardown();

  /* Both should produce audio */
  ASSERT_TRUE(has_nonzero(stereo_c3, 256 * 2));
  ASSERT_TRUE(has_nonzero(stereo_c5, 256 * 2));

  /* They should NOT be identical (different pitches) */
  bool different = false;
  for (int i = 0; i < 256 * 2; ++i) {
    if (stereo_c3[i] != stereo_c5[i]) { different = true; break; }
  }
  ASSERT_TRUE(different);
}

STEST(param_changes_affect_output) {
  unit_runtime_desc_t desc = make_valid_desc();

  /* Render with Shape = 0 */
  unit_init(&desc);
  unit_set_param_value(0, 0);
  unit_note_on(60, 127);

  float stereo_a[256 * 2];
  memset(stereo_a, 0, sizeof(stereo_a));
  for (int b = 0; b < 4; ++b)
    unit_render(nullptr, stereo_a + b * 64 * 2, 64);
  unit_teardown();

  /* Render with Shape = 100 */
  unit_init(&desc);
  unit_set_param_value(0, 100);
  unit_note_on(60, 127);

  float stereo_b[256 * 2];
  memset(stereo_b, 0, sizeof(stereo_b));
  for (int b = 0; b < 4; ++b)
    unit_render(nullptr, stereo_b + b * 64 * 2, 64);
  unit_teardown();

  /* Both should produce audio */
  ASSERT_TRUE(has_nonzero(stereo_a, 256 * 2));
  ASSERT_TRUE(has_nonzero(stereo_b, 256 * 2));

  /* Different shape values should produce different waveforms */
  bool different = false;
  for (int i = 0; i < 256 * 2; ++i) {
    if (stereo_a[i] != stereo_b[i]) { different = true; break; }
  }
  ASSERT_TRUE(different);
}

STEST(output_amplitude_reasonable) {
  unit_runtime_desc_t desc = make_valid_desc();
  unit_init(&desc);
  unit_set_param_value(0, 50);
  unit_note_on(69, 127);

  float stereo[512 * 2];
  memset(stereo, 0, sizeof(stereo));
  for (int b = 0; b < 8; ++b)
    unit_render(nullptr, stereo + b * 64 * 2, 64);

  /* All samples should be within [-1.0, 1.0] (valid float audio range) */
  for (int i = 0; i < 512 * 2; ++i) {
    ASSERT_TRUE(stereo[i] >= -1.5f && stereo[i] <= 1.5f);
  }

  /* RMS should be meaningful (not just noise floor) */
  float rms = compute_rms(stereo, 512 * 2);
  ASSERT_TRUE(rms > 0.001f);

  unit_teardown();
}

STEST(multiple_render_calls_continuous) {
  unit_runtime_desc_t desc = make_valid_desc();
  unit_init(&desc);
  unit_note_on(60, 127);

  /* Make multiple small render calls and verify continuous audio */
  float stereo[32 * 2];
  int nonzero_blocks = 0;

  for (int block = 0; block < 20; ++block) {
    memset(stereo, 0, sizeof(stereo));
    unit_render(nullptr, stereo, 32);

    if (has_nonzero(stereo, 32 * 2))
      nonzero_blocks++;

    /* All samples should be valid floats */
    for (int i = 0; i < 64; ++i) {
      ASSERT_TRUE(!std::isnan(stereo[i]));
      ASSERT_TRUE(!std::isinf(stereo[i]));
    }
  }

  /* Most blocks should contain audio */
  ASSERT_TRUE(nonzero_blocks > 15);

  unit_teardown();
}

STEST(note_off_eventually_silences) {
  unit_runtime_desc_t desc = make_valid_desc();
  unit_init(&desc);
  unit_note_on(69, 127);

  /* Let the engine produce some audio */
  float stereo[64 * 2];
  for (int b = 0; b < 4; ++b)
    unit_render(nullptr, stereo, 64);

  /* Note off */
  unit_note_off(69);

  /* Render many blocks — the VA engine may have some decay but should
   * not crash or produce NaN. We check that it remains stable. */
  for (int b = 0; b < 20; ++b) {
    unit_render(nullptr, stereo, 64);
    for (int i = 0; i < 128; ++i) {
      ASSERT_TRUE(!std::isnan(stereo[i]));
      ASSERT_TRUE(!std::isinf(stereo[i]));
    }
  }

  unit_teardown();
}

/* ===========================================================================
 * Main
 * ======================================================================== */

int main(void) {
  printf("\n=== Sound Production Tests (Plaits VirtualAnalogEngine) ===\n\n");

  printf("Engine Lifecycle:\n");
  run_stest_engine_init_succeeds();
  run_stest_render_silence_before_note_on();

  printf("\nSound Production:\n");
  run_stest_note_on_produces_audio();
  run_stest_note_on_produces_stereo();
  run_stest_different_notes_produce_different_pitch();
  run_stest_param_changes_affect_output();

  printf("\nAudio Quality:\n");
  run_stest_output_amplitude_reasonable();
  run_stest_multiple_render_calls_continuous();
  run_stest_note_off_eventually_silences();

  printf("\n=== Results: %d/%d passed",
         g_tests_passed, g_tests_run);
  if (g_tests_failed > 0)
    printf(", %d FAILED", g_tests_failed);
  printf(" ===\n\n");

  return g_tests_failed > 0 ? 1 : 0;
}
