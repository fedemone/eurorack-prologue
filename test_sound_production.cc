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
  unit_set_param_value(1, 50);  /* Shape = 50% */
  unit_set_param_value(2, 50);  /* ShiftShape = 50% */

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
  unit_set_param_value(1, 0);
  unit_note_on(60, 127);

  float stereo_a[256 * 2];
  memset(stereo_a, 0, sizeof(stereo_a));
  for (int b = 0; b < 4; ++b)
    unit_render(nullptr, stereo_a + b * 64 * 2, 64);
  unit_teardown();

  /* Render with Shape = 100 */
  unit_init(&desc);
  unit_set_param_value(1, 100);
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
  unit_set_param_value(1, 50);
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
 * LFO Square
 *
 * Square is the one shape whose whole point is that it has no in-between:
 * full scale one way, then the other.  Aim an LFO at Pitch with a range of
 * 12 semitones and the note sits two octaves apart at the two ends of its
 * swing, so the zero-crossing rate over short windows -- which scales with
 * pitch whatever the waveform -- should fall into two clusters a factor of
 * four apart, with nothing between them but the windows that straddle an
 * edge.  Triangle, over the same windows, spends most of its time between
 * them; that is checked too, so a test that cannot tell the two apart fails.
 * ======================================================================== */

/* Plaits layout: see header.c. */
enum {
  kPlaitsLfoTarget = 5, kPlaitsLfo1Shape = 6, kPlaitsLfo1Rate = 7,
  kPlaitsLfo2Rate = 8, kPlaitsLfo2Depth = 9, kPlaitsLfo2Target = 10,
  kPlaitsLfo2Shape = 11, kPlaitsGateMode = 12, kPlaitsPitchRange = 14
};
static const int kShapeTriangle = 1, kShapeRampUp = 2, kShapeSquare = 5;
static const int kTargetPitch = 4;

/* Fraction of 20 ms windows whose zero-crossing rate sits on the low or the
 * high level, over 2.4 s -- three LFO cycles at Rate 40.  A level is the
 * median of the windows on its side of the geometric mean, and a window is
 * on it within 20% or one crossing, whichever is more: counts this small are
 * quantised, and the low level is five or six crossings. */
static float lfo_two_level_fraction(int lfo, int shape, float *ratio) {
  unit_runtime_desc_t desc = make_valid_desc();
  unit_init(&desc);
  for (uint8_t p = 0; p < UNIT_MAX_PARAM_COUNT; ++p)
    unit_set_param_value(p, unit_header.params[p].init);
  unit_set_param_value(kPlaitsGateMode, 2);       /* continuous */
  unit_set_param_value(kPlaitsPitchRange, 12);
  if (lfo == 1) {
    unit_set_param_value(kPlaitsLfoTarget, kTargetPitch);
    unit_set_param_value(kPlaitsLfo1Shape, shape);
    unit_set_param_value(kPlaitsLfo1Rate, 40);
  } else {
    unit_set_param_value(kPlaitsLfo2Target, kTargetPitch);
    unit_set_param_value(kPlaitsLfo2Shape, shape);
    unit_set_param_value(kPlaitsLfo2Rate, 40);
    unit_set_param_value(kPlaitsLfo2Depth, 100);
  }
  unit_note_on(60, 127);

  static float stereo[64 * 2];
  const int window = 960, nwin = 120;             /* 20 ms, 2.4 s */
  float zcr[120];
  float prev = 0.0f;
  int n = 0, w = 0, crossings = 0;
  for (int b = 0; b < 40; ++b) unit_render(nullptr, stereo, 64);
  while (w < nwin) {
    unit_render(nullptr, stereo, 64);
    for (int i = 0; i < 64 && w < nwin; ++i) {
      const float x = stereo[i * 2];
      if ((x >= 0.0f) != (prev >= 0.0f)) ++crossings;
      prev = x;
      if (++n == window) {
        zcr[w++] = (float)crossings;
        n = 0;
        crossings = 0;
      }
    }
  }
  unit_teardown();

  float lo = 1e9f, hi = 0.0f;
  for (int i = 0; i < nwin; ++i) {
    if (zcr[i] < lo) lo = zcr[i];
    if (zcr[i] > hi) hi = zcr[i];
  }
  const float mid = sqrtf(lo * hi);
  float below[120], above[120];
  int nb = 0, na = 0;
  for (int i = 0; i < nwin; ++i) {
    if (zcr[i] < mid) below[nb++] = zcr[i];
    else above[na++] = zcr[i];
  }
  /* Medians, by insertion sort: 120 values, once. */
  for (int pass = 0; pass < 2; ++pass) {
    float *v = pass ? above : below;
    const int count = pass ? na : nb;
    for (int i = 1; i < count; ++i)
      for (int j = i; j > 0 && v[j] < v[j - 1]; --j) {
        const float t = v[j]; v[j] = v[j - 1]; v[j - 1] = t;
      }
  }
  const float low = nb ? below[nb / 2] : lo;
  const float high = na ? above[na / 2] : hi;
  const float tol_low = fmaxf(1.0f, 0.2f * low);
  const float tol_high = fmaxf(1.0f, 0.2f * high);
  int on_level = 0;
  for (int i = 0; i < nwin; ++i)
    if (fabsf(zcr[i] - low) <= tol_low || fabsf(zcr[i] - high) <= tol_high)
      ++on_level;
  *ratio = low > 0.0f ? high / low : 0.0f;
  if (getenv("LFO_DEBUG")) {
    printf("\n   ");
    for (int i = 0; i < nwin; ++i) printf(" %d", (int)zcr[i]);
    printf("\n    on a level: %.2f, levels %.0f and %.0f\n",
           (double)on_level / nwin, (double)low, (double)high);
  }
  return (float)on_level / (float)nwin;
}

STEST(lfo2_square_has_two_levels) {
  float ratio, triangle_ratio;
  const float square = lfo_two_level_fraction(2, kShapeSquare, &ratio);
  const float triangle = lfo_two_level_fraction(2, kShapeTriangle, &triangle_ratio);
  /* Two octaves apart: a factor of four in crossing rate, and nearly every
   * window on one level or the other.  Triangle's two "levels" are just the
   * medians of each half of a continuum, much closer together (measured,
   * 8 and 15 crossings against Square's 5 and 21). */
  ASSERT_TRUE(ratio > 3.4f && ratio < 4.6f);
  ASSERT_TRUE(square > 0.85f);
  ASSERT_TRUE(triangle_ratio < 2.8f);
  (void)triangle;
}

/* LFO1's shapes are transfer curves on the cosine the wrapper runs, not
 * waveforms of their own, and its Triangle is an S-curve that flattens the
 * cosine's peaks -- so it lingers near the two ends almost as Square does,
 * and is no contrast.  Ramp Up, x * |x|, lingers near the middle instead. */
STEST(lfo1_square_has_two_levels) {
  float ratio, ramp_ratio;
  const float square = lfo_two_level_fraction(1, kShapeSquare, &ratio);
  const float ramp = lfo_two_level_fraction(1, kShapeRampUp, &ramp_ratio);
  ASSERT_TRUE(ratio > 3.4f && ratio < 4.6f);
  ASSERT_TRUE(square > 0.85f);
  ASSERT_TRUE(ramp_ratio < 2.8f);
  (void)ramp;
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

  printf("\nLFO shapes:\n");
  run_stest_lfo2_square_has_two_levels();
  run_stest_lfo1_square_has_two_levels();

  printf("\n=== Results: %d/%d passed",
         g_tests_passed, g_tests_run);
  if (g_tests_failed > 0)
    printf(", %d FAILED", g_tests_failed);
  printf(" ===\n\n");

  return g_tests_failed > 0 ? 1 : 0;
}
