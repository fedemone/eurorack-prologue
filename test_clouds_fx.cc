/*
 * Native host test for CloudsFX (the Clouds granular engine as a drumlogue
 * delfx unit).  Links the REAL Clouds engine through the delfx wrapper and
 * verifies the thing the synth build cannot: that FX-bus audio input
 * actually reaches the engine and comes back out.
 *
 * Build: see the `test-clouds-fx` target in the root Makefile.
 */
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdint>

#include "runtime.h"
#include "unit.h"

extern "C" {
int8_t  unit_init(const unit_runtime_desc_t *desc);
void    unit_teardown(void);
void    unit_render(const float *in, float *out, uint32_t frames);
void    unit_set_param_value(uint8_t id, int32_t value);
int32_t unit_get_param_value(uint8_t id);
const char *unit_get_param_str_value(uint8_t id, int32_t value);
}

static int g_fail = 0;
#define CHECK(cond, msg, ...) do { \
  if (!(cond)) { printf("  FAIL: " msg "\n", ##__VA_ARGS__); g_fail++; } \
  else         { printf("  ok:   " msg "\n", ##__VA_ARGS__); } \
} while (0)

static unit_runtime_desc_t make_delfx_desc(void) {
  unit_runtime_desc_t desc;
  memset(&desc, 0, sizeof(desc));
  desc.target = k_unit_target_drumlogue_delfx;
  desc.api = k_unit_api_2_0_0;
  desc.samplerate = 48000;
  desc.frames_per_buffer = 64;
  desc.input_channels = 2;
  desc.output_channels = 2;
  return desc;
}

/* Fill an interleaved stereo buffer with a test sine. */
static void fill_sine(float *buf, uint32_t frames, float &phase, float freq) {
  const float inc = 2.0f * 3.14159265f * freq / 48000.0f;
  for (uint32_t i = 0; i < frames; ++i) {
    float s = 0.6f * sinf(phase);
    buf[i * 2] = s;
    buf[i * 2 + 1] = s;
    phase += inc;
  }
}

static float peak_abs(const float *buf, uint32_t frames) {
  float pk = 0.0f;
  for (uint32_t i = 0; i < frames * 2; ++i) {
    float a = fabsf(buf[i]);
    if (a > pk) pk = a;
  }
  return pk;
}

int main(void) {
  /* --- 1. Header identity --- */
  printf("[1] Header identity (delfx target, name, params)\n");
  CHECK(unit_header.target == (uint16_t)k_unit_target_drumlogue_delfx,
        "target is drumlogue delfx (got 0x%04X)", unit_header.target);
  CHECK(strcmp(unit_header.name, "CloudsFX") == 0,
        "name == CloudsFX (got \"%s\")", unit_header.name);
  CHECK(unit_header.num_params == 11,
        "num_params == 11 (got %u)", unit_header.num_params);
  CHECK(strcmp(unit_header.params[0].name, "Position") == 0, "param0 == Position");
  CHECK(strcmp(unit_header.params[6].name, "Dry/Wet") == 0, "param6 == Dry/Wet");
  CHECK(strcmp(unit_header.params[8].name, "Freeze") == 0, "param8 == Freeze");
  /* No sample params should exist */
  for (uint32_t i = 0; i < unit_header.num_params; ++i) {
    CHECK(strstr(unit_header.params[i].name, "Sample") == nullptr &&
          strcmp(unit_header.params[i].name, "Base Note") != 0,
          "param%u (\"%s\") is not a sample/base-note param", i,
          unit_header.params[i].name);
  }

  /* --- 2. init validation --- */
  printf("[2] init\n");
  int8_t err_bad = unit_init(nullptr);
  CHECK(err_bad == k_unit_err_undef, "null desc rejected");
  unit_runtime_desc_t bad = make_delfx_desc();
  bad.target = k_unit_target_drumlogue_synth;
  CHECK(unit_init(&bad) == k_unit_err_target, "synth target rejected by delfx unit");
  unit_runtime_desc_t desc = make_delfx_desc();
  CHECK(unit_init(&desc) == k_unit_err_none, "valid delfx desc accepted");

  /* --- 3. String param values (Mode / Quality) --- */
  printf("[3] Mode / Quality strings\n");
  CHECK(strcmp(unit_get_param_str_value(9, 0), "Granular") == 0, "Mode 0 == Granular");
  CHECK(strcmp(unit_get_param_str_value(9, 2), "Delay") == 0, "Mode 2 == Delay");
  CHECK(strcmp(unit_get_param_str_value(10, 0), "StHi") == 0, "Quality 0 == StHi");

  /* --- 4. Dry passthrough: Dry/Wet = 0 -> output tracks the input ---
   *
   * The engine runs at 32 kHz, so the signal crosses two sample-rate
   * conversions and a priming cushion on the way through: about 2.2 ms, or
   * two 64-frame buffers.  Measure how many buffers it actually takes rather
   * than assuming — a latency regression is worth catching, and it is the
   * same measurement as "input reached the engine". */
  printf("[4] Dry passthrough carries the FX-bus input\n");
  unit_set_param_value(6, 0);      /* Dry/Wet = 0% (fully dry) */
  float phase = 0.0f;
  float in[64 * 2], out[64 * 2];
  float in_pk = 0.0f, out_pk = 0.0f;
  int latency_blocks = -1;
  for (int blk = 0; blk < 8; ++blk) {
    fill_sine(in, 64, phase, 220.0f);
    memset(out, 0, sizeof(out));
    unit_render(in, out, 64);
    if (blk == 0) in_pk = peak_abs(in, 64);
    out_pk = peak_abs(out, 64);
    if (latency_blocks < 0 && out_pk > 0.05f) latency_blocks = blk;
  }
  CHECK(in_pk > 0.1f, "input is non-silent (peak %.3f)", in_pk);
  CHECK(latency_blocks >= 0,
        "dry output is non-silent (peak %.3f) -> input reached engine", out_pk);
  CHECK(latency_blocks >= 0 && latency_blocks <= 3,
        "dry latency is %d buffers of 64 frames (<= 3 expected)", latency_blocks);

  /* --- 5. Wet granular: prime the buffer, expect non-silent wet output ---
   *
   * At 32 kHz the recording buffer holds 1.5x more time and therefore takes
   * 1.5x longer to fill, so a grain reading from POSITION finds material
   * later than it did when the engine ran at 48 kHz.  600 buffers is ~0.8 s,
   * comfortably past that for any POSITION. */
  printf("[5] Wet granular output\n");
  unit_set_param_value(6, 100);    /* Dry/Wet = 100% (fully wet) */
  unit_set_param_value(9, 0);      /* Mode = Granular */
  unit_set_param_value(2, 70);     /* Density */
  float wet_pk = 0.0f;
  for (int blk = 0; blk < 600; ++blk) {
    fill_sine(in, 64, phase, 220.0f);
    memset(out, 0, sizeof(out));
    unit_render(in, out, 64);
    float pk = peak_abs(out, 64);
    if (pk > wet_pk) wet_pk = pk;
  }
  CHECK(wet_pk > 0.01f, "wet granular output is non-silent (peak %.3f)", wet_pk);

  /* --- 6. Suspend/no-input passes dry through without crashing --- */
  printf("[6] Null input -> dry passthrough (no crash)\n");
  memset(out, 0xAB, sizeof(out));
  unit_render(nullptr, out, 64);
  CHECK(peak_abs(out, 64) == 0.0f, "null input yields silence");

  unit_teardown();

  printf("\n=== %s (%d failures) ===\n", g_fail ? "FAILED" : "ALL PASS", g_fail);
  return g_fail ? 1 : 0;
}
