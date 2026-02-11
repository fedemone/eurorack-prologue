/*
 * File: test_drumlogue_callbacks.cc
 *
 * Unit tests for the drumlogue port's callback chain:
 *   Synth Module API (unit_*) -> adapter (osc_adapter_*) -> OSC API (OSC_*)
 *
 * Provides mock OSC_* implementations that record calls and produce
 * deterministic output for verification.
 *
 * Build:
 *   g++ -std=c++11 -DOSC_NATIVE_BLOCK_SIZE=24 -Idrumlogue -I. \
 *       test_drumlogue_callbacks.cc drumlogue_osc_adapter.cc \
 *       drumlogue_unit_wrapper.cc -o test_drumlogue_callbacks
 *
 * Run:
 *   ./test_drumlogue_callbacks
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cassert>

/* Include the drumlogue headers */
#include "userosc.h"
#include "runtime.h"
#include "unit.h"
#include "drumlogue_osc_adapter.h"

/* ===========================================================================
 * Test Framework (minimal)
 * ======================================================================== */

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST(name) \
  static void test_##name(void); \
  static void run_test_##name(void) { \
    g_tests_run++; \
    printf("  %-60s ", #name); \
    fflush(stdout); \
    test_##name(); \
    g_tests_passed++; \
    printf("PASS\n"); \
  } \
  static void test_##name(void)

#define ASSERT_EQ(expected, actual) do { \
    auto _e = (expected); auto _a = (actual); \
    if (_e != _a) { \
      printf("FAIL\n    %s:%d: expected %lld, got %lld\n", \
             __FILE__, __LINE__, (long long)_e, (long long)_a); \
      g_tests_failed++; g_tests_passed--; \
      return; \
    } \
  } while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
      printf("FAIL\n    %s:%d: condition false: %s\n", \
             __FILE__, __LINE__, #cond); \
      g_tests_failed++; g_tests_passed--; \
      return; \
    } \
  } while(0)

#define ASSERT_NEAR(expected, actual, tolerance) do { \
    float _e = (expected); float _a = (actual); float _t = (tolerance); \
    if (fabsf(_e - _a) > _t) { \
      printf("FAIL\n    %s:%d: expected %.8f, got %.8f (tol %.8f)\n", \
             __FILE__, __LINE__, _e, _a, _t); \
      g_tests_failed++; g_tests_passed--; \
      return; \
    } \
  } while(0)

/* ===========================================================================
 * Mock OSC API
 *
 * Records all calls so tests can verify the adapter/wrapper correctly
 * translates between drumlogue and OSC APIs.
 * ======================================================================== */

struct MockOscState {
  /* Init tracking */
  int init_count;
  uint32_t last_init_platform;
  uint32_t last_init_api;

  /* Cycle tracking */
  int cycle_count;
  const user_osc_param_t *last_cycle_params;
  uint32_t last_cycle_frames;
  /* Value to fill Q31 output with (allows tests to verify conversion) */
  int32_t cycle_fill_value;

  /* Note on/off tracking */
  int noteon_count;
  int noteoff_count;
  uint16_t last_noteon_pitch;
  int32_t last_noteon_shape_lfo;

  /* Param tracking */
  int param_count;
  uint16_t last_param_index;
  uint16_t last_param_value;
  /* Full history (up to 64 calls) */
  uint16_t param_history_index[64];
  uint16_t param_history_value[64];
  int param_history_count;
};

static MockOscState g_mock;

static void mock_reset(void) {
  memset(&g_mock, 0, sizeof(g_mock));
  /* Default fill: 25% of full scale = 0x20000000 */
  g_mock.cycle_fill_value = 0x20000000;
}

/* --- Mock implementations of OSC_* functions --- */

extern "C" {

void OSC_INIT(uint32_t platform, uint32_t api) {
  g_mock.init_count++;
  g_mock.last_init_platform = platform;
  g_mock.last_init_api = api;
}

void OSC_CYCLE(const user_osc_param_t * const params,
               int32_t *yn, const uint32_t frames) {
  g_mock.cycle_count++;
  g_mock.last_cycle_params = params;
  g_mock.last_cycle_frames = frames;

  /* Fill output with deterministic value */
  for (uint32_t i = 0; i < frames; ++i) {
    yn[i] = g_mock.cycle_fill_value;
  }
}

void OSC_NOTEON(const user_osc_param_t * const params) {
  g_mock.noteon_count++;
  g_mock.last_noteon_pitch = params->pitch;
  g_mock.last_noteon_shape_lfo = params->shape_lfo;
}

void OSC_NOTEOFF(const user_osc_param_t * const params) {
  g_mock.noteoff_count++;
  (void)params;
}

void OSC_PARAM(uint16_t index, uint16_t value) {
  g_mock.param_count++;
  g_mock.last_param_index = index;
  g_mock.last_param_value = value;
  if (g_mock.param_history_count < 64) {
    g_mock.param_history_index[g_mock.param_history_count] = index;
    g_mock.param_history_value[g_mock.param_history_count] = value;
    g_mock.param_history_count++;
  }
}

} /* extern "C" */

/* ===========================================================================
 * Helper: create a valid runtime descriptor
 * ======================================================================== */

static unit_runtime_desc_t make_valid_desc(void) {
  unit_runtime_desc_t desc;
  memset(&desc, 0, sizeof(desc));
  desc.target = k_unit_target_drumlogue_synth;
  desc.api = k_unit_api_2_0_0;
  desc.samplerate = 48000;
  desc.frames_per_buffer = 48;
  desc.input_channels = 0;
  desc.output_channels = 2;
  desc.get_num_sample_banks = nullptr;
  desc.get_num_samples_for_bank = nullptr;
  desc.get_sample = nullptr;
  return desc;
}

/* Helper: init unit to known good state */
static void init_unit(void) {
  mock_reset();
  unit_runtime_desc_t desc = make_valid_desc();
  unit_init(&desc);
}

/* Helper: teardown and reset */
static void teardown_unit(void) {
  unit_teardown();
  mock_reset();
}

/* ===========================================================================
 * Tests: unit_header
 * ======================================================================== */

TEST(unit_header_size) {
  ASSERT_EQ(sizeof(unit_header_t), unit_header.header_size);
}

TEST(unit_header_target) {
  ASSERT_EQ((uint16_t)(UNIT_TARGET_PLATFORM | k_unit_module_synth),
            unit_header.target);
}

TEST(unit_header_api) {
  ASSERT_EQ(UNIT_API_VERSION, unit_header.api);
}

TEST(unit_header_num_params) {
  ASSERT_EQ(6U, unit_header.num_params);
}

TEST(unit_header_param_names) {
  ASSERT_EQ(0, strcmp(unit_header.params[0].name, "Shape"));
  ASSERT_EQ(0, strcmp(unit_header.params[1].name, "ShiftShape"));
  ASSERT_EQ(0, strcmp(unit_header.params[2].name, "Param 1"));
  ASSERT_EQ(0, strcmp(unit_header.params[3].name, "Param 2"));
  ASSERT_EQ(0, strcmp(unit_header.params[4].name, "LFO Target"));
  ASSERT_EQ(0, strcmp(unit_header.params[5].name, "LFO2 Rate"));
}

TEST(unit_header_param_types) {
  ASSERT_EQ(k_unit_param_type_percent, unit_header.params[0].type);
  ASSERT_EQ(k_unit_param_type_percent, unit_header.params[1].type);
  ASSERT_EQ(k_unit_param_type_percent, unit_header.params[2].type);
  ASSERT_EQ(k_unit_param_type_percent, unit_header.params[3].type);
  ASSERT_EQ(k_unit_param_type_enum,    unit_header.params[4].type);
  ASSERT_EQ(k_unit_param_type_percent, unit_header.params[5].type);
}

TEST(unit_header_unused_params_are_none) {
  for (int i = 6; i < UNIT_MAX_PARAM_COUNT; ++i) {
    ASSERT_EQ(k_unit_param_type_none, unit_header.params[i].type);
  }
}

/* ===========================================================================
 * Tests: unit_init validation
 * ======================================================================== */

TEST(unit_init_null_desc) {
  mock_reset();
  int8_t err = unit_init(nullptr);
  ASSERT_EQ(k_unit_err_undef, err);
  ASSERT_EQ(0, g_mock.init_count); /* should NOT call OSC_INIT */
}

TEST(unit_init_bad_target) {
  mock_reset();
  unit_runtime_desc_t desc = make_valid_desc();
  desc.target = 0x0100; /* prologue target, not drumlogue */
  int8_t err = unit_init(&desc);
  ASSERT_EQ(k_unit_err_target, err);
  ASSERT_EQ(0, g_mock.init_count);
}

TEST(unit_init_bad_api_version) {
  mock_reset();
  unit_runtime_desc_t desc = make_valid_desc();
  desc.api = k_unit_api_1_0_0; /* too old */
  int8_t err = unit_init(&desc);
  ASSERT_EQ(k_unit_err_api_version, err);
  ASSERT_EQ(0, g_mock.init_count);
}

TEST(unit_init_bad_samplerate) {
  mock_reset();
  unit_runtime_desc_t desc = make_valid_desc();
  desc.samplerate = 44100;
  int8_t err = unit_init(&desc);
  ASSERT_EQ(k_unit_err_samplerate, err);
  ASSERT_EQ(0, g_mock.init_count);
}

TEST(unit_init_success) {
  mock_reset();
  unit_runtime_desc_t desc = make_valid_desc();
  int8_t err = unit_init(&desc);
  ASSERT_EQ(k_unit_err_none, err);
  ASSERT_EQ(1, g_mock.init_count);
  ASSERT_EQ(desc.target, g_mock.last_init_platform);
  ASSERT_EQ(desc.api, g_mock.last_init_api);
  unit_teardown();
}

/* ===========================================================================
 * Tests: Adapter note events
 * ======================================================================== */

TEST(adapter_note_on_pitch_encoding) {
  init_unit();

  /* Note on: MIDI note 69 (A4) */
  osc_adapter_note_on(69, 100);
  ASSERT_EQ(1, g_mock.noteon_count);

  /* pitch = (note << 8) | frac. For note 69 with 0 pitch mod, frac=0 */
  uint16_t expected_pitch = (69 << 8) | 0;
  ASSERT_EQ(expected_pitch, g_mock.last_noteon_pitch);

  teardown_unit();
}

TEST(adapter_note_on_with_pitch_bend) {
  init_unit();

  /* Apply +1 semitone pitch bend: 8192/2 = 4096 gives +1 semitone */
  osc_adapter_pitch_bend(4096);

  /* Now note on at C4 (60) */
  osc_adapter_note_on(60, 100);

  /* Expected: note=61, frac=0 (60 + 1.0 semitone = 61.0) */
  uint16_t expected_pitch = (61 << 8) | 0;
  ASSERT_EQ(expected_pitch, g_mock.last_noteon_pitch);

  teardown_unit();
}

TEST(adapter_note_off_calls_osc) {
  init_unit();
  osc_adapter_note_off(60);
  ASSERT_EQ(1, g_mock.noteoff_count);
  teardown_unit();
}

TEST(adapter_not_initialized_guards) {
  /* Without init, all adapter functions should be no-ops */
  mock_reset();
  /* Don't call init */
  osc_adapter_note_on(60, 100);
  osc_adapter_note_off(60);
  osc_adapter_set_param(k_user_osc_param_shape, 512);
  ASSERT_EQ(0, g_mock.noteon_count);
  ASSERT_EQ(0, g_mock.noteoff_count);
  ASSERT_EQ(0, g_mock.param_count);
}

/* ===========================================================================
 * Tests: Wrapper note events
 * ======================================================================== */

TEST(wrapper_note_on_delegates) {
  init_unit();
  unit_note_on(72, 127);
  ASSERT_EQ(1, g_mock.noteon_count);
  /* pitch should encode note 72 */
  ASSERT_EQ((uint16_t)(72 << 8), g_mock.last_noteon_pitch);
  teardown_unit();
}

TEST(wrapper_note_off_delegates) {
  init_unit();
  unit_note_on(60, 100);
  unit_note_off(60);
  ASSERT_EQ(1, g_mock.noteoff_count);
  teardown_unit();
}

TEST(wrapper_all_note_off) {
  init_unit();
  unit_note_on(72, 100);
  g_mock.noteoff_count = 0;
  unit_all_note_off();
  ASSERT_EQ(1, g_mock.noteoff_count);
  teardown_unit();
}

TEST(wrapper_gate_on_off) {
  init_unit();
  /* gate_on uses the stored note (default 60 from init) */
  unit_gate_on(100);
  ASSERT_EQ(1, g_mock.noteon_count);
  unit_gate_off();
  ASSERT_EQ(1, g_mock.noteoff_count);
  teardown_unit();
}

/* ===========================================================================
 * Tests: Pitch bend
 * ======================================================================== */

TEST(wrapper_pitch_bend_neutral) {
  init_unit();
  unit_note_on(60, 100);
  g_mock.noteon_count = 0;

  /* Neutral pitch bend = 0x2000 */
  unit_pitch_bend(0x2000);

  /* Re-trigger to see updated pitch (pitch bend updates the param struct) */
  unit_note_on(60, 100);
  ASSERT_EQ((uint16_t)(60 << 8), g_mock.last_noteon_pitch);
  teardown_unit();
}

TEST(wrapper_pitch_bend_up) {
  init_unit();

  /* Full pitch bend up: 0x3FFF -> signed = 0x1FFF = 8191 */
  /* 8191/8192 * 2 semitones ≈ 2.0 semitones */
  unit_pitch_bend(0x3FFF);

  unit_note_on(60, 100);
  /* 60 + ~2.0 = ~62, frac ≈ 0 */
  uint8_t note_part = (uint8_t)(g_mock.last_noteon_pitch >> 8);
  ASSERT_TRUE(note_part == 61 || note_part == 62);
  teardown_unit();
}

TEST(wrapper_pitch_bend_down) {
  init_unit();

  /* Full pitch bend down: 0x0000 -> signed = -8192 */
  /* -8192/8192 * 2 = -2.0 semitones */
  unit_pitch_bend(0x0000);

  unit_note_on(60, 100);
  /* 60 - 2.0 = 58.0 */
  uint8_t note_part = (uint8_t)(g_mock.last_noteon_pitch >> 8);
  ASSERT_EQ(58, note_part);
  teardown_unit();
}

/* ===========================================================================
 * Tests: Parameter mapping
 * ======================================================================== */

TEST(wrapper_param_shape_scaling) {
  init_unit();

  /* Shape: drumlogue 0-100 -> OSC 0-1023 */
  unit_set_param_value(0, 100);  /* max */
  ASSERT_EQ(k_user_osc_param_shape, g_mock.last_param_index);
  ASSERT_EQ(1023, g_mock.last_param_value);

  unit_set_param_value(0, 0);    /* min */
  ASSERT_EQ(0, g_mock.last_param_value);

  unit_set_param_value(0, 50);   /* mid */
  /* (50 * 1023 + 50) / 100 = 512 (rounded) */
  ASSERT_EQ(512, g_mock.last_param_value);

  teardown_unit();
}

TEST(wrapper_param_shiftshape_scaling) {
  init_unit();

  /* Shift-Shape: drumlogue 0-100 -> OSC 0-1023 */
  unit_set_param_value(1, 100);
  ASSERT_EQ(k_user_osc_param_shiftshape, g_mock.last_param_index);
  ASSERT_EQ(1023, g_mock.last_param_value);

  teardown_unit();
}

TEST(wrapper_param_id1_bipolar) {
  init_unit();

  /* Param 1: drumlogue 0-100 -> OSC 0-200 (bipolar centered at 100) */
  unit_set_param_value(2, 50);
  ASSERT_EQ(k_user_osc_param_id1, g_mock.last_param_index);
  ASSERT_EQ(100, g_mock.last_param_value); /* 50 * 2 = 100 (center) */

  unit_set_param_value(2, 0);
  ASSERT_EQ(0, g_mock.last_param_value);

  unit_set_param_value(2, 100);
  ASSERT_EQ(200, g_mock.last_param_value);

  teardown_unit();
}

TEST(wrapper_param_id2_percent) {
  init_unit();

  /* Param 2: drumlogue 0-100 -> OSC 0-100 (direct) */
  unit_set_param_value(3, 75);
  ASSERT_EQ(k_user_osc_param_id2, g_mock.last_param_index);
  ASSERT_EQ(75, g_mock.last_param_value);

  teardown_unit();
}

TEST(wrapper_param_id3_enum) {
  init_unit();

  /* LFO Target: direct enum passthrough */
  unit_set_param_value(4, 3);
  ASSERT_EQ(k_user_osc_param_id3, g_mock.last_param_index);
  ASSERT_EQ(3, g_mock.last_param_value);

  teardown_unit();
}

TEST(wrapper_param_id4_rate) {
  init_unit();

  /* LFO2 Rate: 0-100 percent */
  unit_set_param_value(5, 42);
  ASSERT_EQ(k_user_osc_param_id4, g_mock.last_param_index);
  ASSERT_EQ(42, g_mock.last_param_value);

  teardown_unit();
}

TEST(wrapper_param_out_of_range_ignored) {
  init_unit();
  int before = g_mock.param_count;
  unit_set_param_value(6, 50);   /* id 6 -> default case, should return */
  ASSERT_EQ(before, g_mock.param_count);
  unit_set_param_value(24, 50);  /* id >= MAX -> guard */
  ASSERT_EQ(before, g_mock.param_count);
  teardown_unit();
}

TEST(wrapper_get_param_value) {
  init_unit();
  unit_set_param_value(0, 42);
  ASSERT_EQ(42, unit_get_param_value(0));
  unit_set_param_value(3, 99);
  ASSERT_EQ(99, unit_get_param_value(3));
  teardown_unit();
}

/* ===========================================================================
 * Tests: Shape LFO
 * ======================================================================== */

TEST(adapter_shape_lfo_conversion) {
  init_unit();

  osc_adapter_set_shape_lfo(0.5f);

  /* Trigger a cycle to check shape_lfo in params */
  float output[24];
  osc_adapter_render(output, 24);

  /* The Q31 value of shape_lfo should be ~0x40000000 for 0.5 */
  /* We can't read the params struct directly (it's static),
     but we can check that noteon captures the value */
  osc_adapter_note_on(60, 100);
  int32_t expected_q31 = (int32_t)(0.5f * (float)(1U << 31));
  /* Allow some float rounding tolerance */
  int32_t diff = g_mock.last_noteon_shape_lfo - expected_q31;
  if (diff < 0) diff = -diff;
  ASSERT_TRUE(diff < 256); /* within tiny Q31 margin */

  teardown_unit();
}

TEST(wrapper_channel_pressure_to_shape_lfo) {
  init_unit();

  /* Channel pressure 127 -> shape LFO = 1.0 */
  unit_channel_pressure(127);
  osc_adapter_note_on(60, 100);
  /* Q31 of 1.0 = ~0x7FFFFFFF, but float_to_q31 clips at 1.0 */
  /* 127/127 = 1.0 -> shape_lfo Q31 should be near max */
  ASSERT_TRUE(g_mock.last_noteon_shape_lfo > 0x70000000);

  /* Channel pressure 0 -> shape LFO = 0.0 */
  unit_channel_pressure(0);
  osc_adapter_note_on(60, 100);
  ASSERT_TRUE(g_mock.last_noteon_shape_lfo == 0);

  teardown_unit();
}

/* ===========================================================================
 * Tests: Audio rendering - Q31/float conversion
 * ======================================================================== */

TEST(render_q31_to_float_zero) {
  init_unit();
  g_mock.cycle_fill_value = 0; /* Q31 zero -> float 0.0 */

  float output[24];
  osc_adapter_render(output, 24);

  for (int i = 0; i < 24; ++i) {
    ASSERT_NEAR(0.0f, output[i], 1e-7f);
  }
  teardown_unit();
}

TEST(render_q31_to_float_positive) {
  init_unit();
  /* Q31: 0x40000000 = 0.5 * 2^31, so float = 0.5 * 2^31 / 2^31 = 0.5 */
  g_mock.cycle_fill_value = 0x40000000;

  float output[24];
  osc_adapter_render(output, 24);

  for (int i = 0; i < 24; ++i) {
    ASSERT_NEAR(0.5f, output[i], 1e-4f);
  }
  teardown_unit();
}

TEST(render_q31_to_float_negative) {
  init_unit();
  /* Q31: -0x40000000 = -0.5 * 2^31 -> float -0.5 */
  g_mock.cycle_fill_value = -0x40000000;

  float output[24];
  osc_adapter_render(output, 24);

  for (int i = 0; i < 24; ++i) {
    ASSERT_NEAR(-0.5f, output[i], 1e-4f);
  }
  teardown_unit();
}

/* ===========================================================================
 * Tests: Buffered rendering across block boundaries
 * ======================================================================== */

TEST(render_exact_block_size) {
  init_unit();
  g_mock.cycle_fill_value = 0x20000000; /* 0.25 */

  float output[24];
  osc_adapter_render(output, 24);

  ASSERT_EQ(1, g_mock.cycle_count); /* exactly one OSC_CYCLE call */
  for (int i = 0; i < 24; ++i) {
    ASSERT_NEAR(0.25f, output[i], 1e-4f);
  }
  teardown_unit();
}

TEST(render_less_than_block_size) {
  init_unit();
  g_mock.cycle_fill_value = 0x20000000;

  float output[10];
  osc_adapter_render(output, 10);

  ASSERT_EQ(1, g_mock.cycle_count); /* one block rendered, 10 consumed */
  for (int i = 0; i < 10; ++i) {
    ASSERT_NEAR(0.25f, output[i], 1e-4f);
  }
  teardown_unit();
}

TEST(render_more_than_block_size) {
  init_unit();
  g_mock.cycle_fill_value = 0x20000000;

  float output[48]; /* 48 = 2 full blocks */
  osc_adapter_render(output, 48);

  ASSERT_EQ(2, g_mock.cycle_count); /* two OSC_CYCLE calls */
  for (int i = 0; i < 48; ++i) {
    ASSERT_NEAR(0.25f, output[i], 1e-4f);
  }
  teardown_unit();
}

TEST(render_non_multiple_of_block_size) {
  init_unit();
  g_mock.cycle_fill_value = 0x20000000;

  float output[25]; /* 25 = 1 full block + 1 sample from next */
  osc_adapter_render(output, 25);

  ASSERT_EQ(2, g_mock.cycle_count);
  for (int i = 0; i < 25; ++i) {
    ASSERT_NEAR(0.25f, output[i], 1e-4f);
  }
  teardown_unit();
}

TEST(render_single_sample) {
  init_unit();
  g_mock.cycle_fill_value = 0x20000000;

  float output[1];
  osc_adapter_render(output, 1);

  ASSERT_EQ(1, g_mock.cycle_count);
  ASSERT_NEAR(0.25f, output[0], 1e-4f);
  teardown_unit();
}

TEST(render_accumulates_across_calls) {
  init_unit();
  g_mock.cycle_fill_value = 0x20000000;

  /* First call: 10 samples. Renders 1 block (24), consumes 10 -> 14 buffered */
  float output[10];
  osc_adapter_render(output, 10);
  ASSERT_EQ(1, g_mock.cycle_count);

  /* Second call: 10 samples. 14 still buffered -> consumes 10 -> 4 buffered */
  osc_adapter_render(output, 10);
  ASSERT_EQ(1, g_mock.cycle_count); /* no new block needed */

  /* Third call: 10 samples. 4 buffered + need 6 more -> new block rendered */
  osc_adapter_render(output, 10);
  ASSERT_EQ(2, g_mock.cycle_count);

  teardown_unit();
}

TEST(render_large_request_96_frames) {
  init_unit();
  g_mock.cycle_fill_value = 0x20000000;

  float output[96]; /* 96 = 4 full blocks */
  osc_adapter_render(output, 96);

  ASSERT_EQ(4, g_mock.cycle_count);
  for (int i = 0; i < 96; ++i) {
    ASSERT_NEAR(0.25f, output[i], 1e-4f);
  }
  teardown_unit();
}

/* ===========================================================================
 * Tests: unit_render (stereo interleaving)
 * ======================================================================== */

TEST(unit_render_stereo_interleave) {
  init_unit();
  g_mock.cycle_fill_value = 0x40000000; /* 0.5 */

  /* unit_render outputs stereo interleaved: L R L R ... */
  float stereo_out[48 * 2]; /* 48 frames * 2 channels */
  memset(stereo_out, 0, sizeof(stereo_out));

  unit_render(nullptr, stereo_out, 48);

  for (int i = 0; i < 48; ++i) {
    ASSERT_NEAR(0.5f, stereo_out[i * 2],     1e-4f); /* L */
    ASSERT_NEAR(0.5f, stereo_out[i * 2 + 1], 1e-4f); /* R (same as L) */
  }
  teardown_unit();
}

TEST(unit_render_suspended_outputs_silence) {
  init_unit();
  g_mock.cycle_fill_value = 0x40000000;

  unit_suspend();

  float stereo_out[24 * 2];
  /* Fill with non-zero to verify it gets cleared */
  for (int i = 0; i < 48; ++i) stereo_out[i] = 1.0f;

  unit_render(nullptr, stereo_out, 24);

  for (int i = 0; i < 48; ++i) {
    ASSERT_NEAR(0.0f, stereo_out[i], 1e-7f);
  }
  ASSERT_EQ(0, g_mock.cycle_count); /* OSC_CYCLE should NOT be called */
  teardown_unit();
}

TEST(unit_render_resume_after_suspend) {
  init_unit();
  g_mock.cycle_fill_value = 0x40000000;

  unit_suspend();
  unit_resume();

  float stereo_out[24 * 2];
  unit_render(nullptr, stereo_out, 24);

  /* Should produce audio again after resume */
  ASSERT_TRUE(g_mock.cycle_count > 0);
  ASSERT_NEAR(0.5f, stereo_out[0], 1e-4f);
  teardown_unit();
}

TEST(unit_render_not_initialized) {
  /* Without init, unit_render should output silence */
  mock_reset();
  unit_teardown(); /* ensure not initialized */

  float stereo_out[24 * 2];
  for (int i = 0; i < 48; ++i) stereo_out[i] = 1.0f;

  unit_render(nullptr, stereo_out, 24);

  for (int i = 0; i < 48; ++i) {
    ASSERT_NEAR(0.0f, stereo_out[i], 1e-7f);
  }
}

/* ===========================================================================
 * Tests: Lifecycle
 * ======================================================================== */

TEST(unit_teardown_prevents_further_calls) {
  init_unit();
  unit_teardown();

  /* After teardown, note_on should be a no-op */
  g_mock.noteon_count = 0;
  unit_note_on(60, 100);
  ASSERT_EQ(0, g_mock.noteon_count);
}

TEST(unit_reset_sends_note_off) {
  init_unit();
  unit_note_on(60, 100);
  g_mock.noteoff_count = 0;
  unit_reset();
  ASSERT_EQ(1, g_mock.noteoff_count);
  teardown_unit();
}

/* ===========================================================================
 * Tests: Tempo
 * ======================================================================== */

TEST(wrapper_set_tempo_delegates) {
  init_unit();
  /* Just verify it doesn't crash; tempo is stored but not actively used */
  unit_set_tempo(12000);
  teardown_unit();
}

/* ===========================================================================
 * Tests: Presets (stubs)
 * ======================================================================== */

TEST(preset_stubs) {
  init_unit();
  ASSERT_EQ(0, unit_get_preset_index());
  ASSERT_TRUE(unit_get_preset_name(0) == nullptr);
  unit_load_preset(0); /* no-op, should not crash */
  teardown_unit();
}

/* ===========================================================================
 * Tests: Param string/bmp (stubs)
 * ======================================================================== */

TEST(param_str_value_returns_null) {
  init_unit();
  ASSERT_TRUE(unit_get_param_str_value(0, 50) == nullptr);
  teardown_unit();
}

TEST(param_bmp_value_returns_null) {
  init_unit();
  ASSERT_TRUE(unit_get_param_bmp_value(0, 50) == nullptr);
  teardown_unit();
}

/* ===========================================================================
 * Tests: Adapter render with null/uninitialized
 * ======================================================================== */

TEST(adapter_render_null_output_no_crash) {
  init_unit();
  /* Should not crash with null output */
  osc_adapter_render(nullptr, 24);
  teardown_unit();
}

TEST(adapter_render_uninitialized_outputs_silence) {
  mock_reset();
  /* Don't init adapter */
  float output[24];
  for (int i = 0; i < 24; ++i) output[i] = 1.0f;
  osc_adapter_render(output, 24);
  for (int i = 0; i < 24; ++i) {
    ASSERT_NEAR(0.0f, output[i], 1e-7f);
  }
}

/* ===========================================================================
 * Main: Run all tests
 * ======================================================================== */

int main(void) {
  printf("\n=== Drumlogue Callback Unit Tests ===\n\n");

  printf("Unit Header:\n");
  run_test_unit_header_size();
  run_test_unit_header_target();
  run_test_unit_header_api();
  run_test_unit_header_num_params();
  run_test_unit_header_param_names();
  run_test_unit_header_param_types();
  run_test_unit_header_unused_params_are_none();

  printf("\nunit_init Validation:\n");
  run_test_unit_init_null_desc();
  run_test_unit_init_bad_target();
  run_test_unit_init_bad_api_version();
  run_test_unit_init_bad_samplerate();
  run_test_unit_init_success();

  printf("\nAdapter Note Events:\n");
  run_test_adapter_note_on_pitch_encoding();
  run_test_adapter_note_on_with_pitch_bend();
  run_test_adapter_note_off_calls_osc();
  run_test_adapter_not_initialized_guards();

  printf("\nWrapper Note Events:\n");
  run_test_wrapper_note_on_delegates();
  run_test_wrapper_note_off_delegates();
  run_test_wrapper_all_note_off();
  run_test_wrapper_gate_on_off();

  printf("\nPitch Bend:\n");
  run_test_wrapper_pitch_bend_neutral();
  run_test_wrapper_pitch_bend_up();
  run_test_wrapper_pitch_bend_down();

  printf("\nParameter Mapping:\n");
  run_test_wrapper_param_shape_scaling();
  run_test_wrapper_param_shiftshape_scaling();
  run_test_wrapper_param_id1_bipolar();
  run_test_wrapper_param_id2_percent();
  run_test_wrapper_param_id3_enum();
  run_test_wrapper_param_id4_rate();
  run_test_wrapper_param_out_of_range_ignored();
  run_test_wrapper_get_param_value();

  printf("\nShape LFO:\n");
  run_test_adapter_shape_lfo_conversion();
  run_test_wrapper_channel_pressure_to_shape_lfo();

  printf("\nQ31/Float Conversion:\n");
  run_test_render_q31_to_float_zero();
  run_test_render_q31_to_float_positive();
  run_test_render_q31_to_float_negative();

  printf("\nBuffered Rendering:\n");
  run_test_render_exact_block_size();
  run_test_render_less_than_block_size();
  run_test_render_more_than_block_size();
  run_test_render_non_multiple_of_block_size();
  run_test_render_single_sample();
  run_test_render_accumulates_across_calls();
  run_test_render_large_request_96_frames();

  printf("\nStereo Rendering (unit_render):\n");
  run_test_unit_render_stereo_interleave();
  run_test_unit_render_suspended_outputs_silence();
  run_test_unit_render_resume_after_suspend();
  run_test_unit_render_not_initialized();

  printf("\nLifecycle:\n");
  run_test_unit_teardown_prevents_further_calls();
  run_test_unit_reset_sends_note_off();

  printf("\nTempo:\n");
  run_test_wrapper_set_tempo_delegates();

  printf("\nPresets (stubs):\n");
  run_test_preset_stubs();

  printf("\nParam Display (stubs):\n");
  run_test_param_str_value_returns_null();
  run_test_param_bmp_value_returns_null();

  printf("\nAdapter Edge Cases:\n");
  run_test_adapter_render_null_output_no_crash();
  run_test_adapter_render_uninitialized_outputs_silence();

  printf("\n=== Results: %d/%d passed",
         g_tests_passed, g_tests_run);
  if (g_tests_failed > 0)
    printf(", %d FAILED", g_tests_failed);
  printf(" ===\n\n");

  return g_tests_failed > 0 ? 1 : 0;
}
