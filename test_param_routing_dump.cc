/*
 * File: test_param_routing_dump.cc
 *
 * Prints where every panel parameter goes, for one unit build.
 *
 * The panel layout lives in one table per unit now (drumlogue_param_route.h),
 * and that table replaced a 400-line switch in the wrapper. A transcription
 * slip in a change like that does not crash or fail a range check — it sends
 * a knob to the wrong engine parameter and waits to be noticed by ear.
 *
 * So the routing is captured rather than reasoned about: this dumps, for
 * every id and several values, whether the wrapper forwarded to the
 * oscillator and with what index and value. `make test-param-routing`
 * compares the dump against docs/param_routing.txt, which was generated from
 * the code as it stood before the table existed.
 *
 * Regenerating the golden file is a deliberate act: it means the panel layout
 * changed, and the diff is the review.
 *
 * Build/run: make test-param-routing
 */

#include <cstdio>
#include <cstdint>
#include <cstring>

#include "userosc.h"

#include "runtime.h"
#include "unit.h"

/* ---- the port layer the wrapper calls ---------------------------------- */

struct {
  int      param_count;
  uint16_t last_index;
  uint16_t last_value;
} g_seen;

extern "C" {
void OSC_INIT(uint32_t platform, uint32_t api) { (void)platform; (void)api; }
void OSC_CYCLE(const user_osc_param_t *const params, int32_t *yn,
               const uint32_t frames) {
  (void)params;
  for (uint32_t i = 0; i < frames; ++i) yn[i] = 0;
}
void OSC_NOTEON(const user_osc_param_t *const params) { (void)params; }
void OSC_NOTEOFF(const user_osc_param_t *const params) { (void)params; }
void OSC_RESET(void) {}
void OSC_PARAM(uint16_t index, uint16_t value) {
  g_seen.param_count++;
  g_seen.last_index = index;
  g_seen.last_value = value;
}
#if defined(MUSSOLA_VOCAL)
/* The adapter reads Mussola's stereo pair straight from the port. */
static float s_silence[64];
void mussola_get_last_stereo(const float **left, const float **right) {
  *left = s_silence;
  *right = s_silence;
}
#endif
}

static uint8_t no_banks(void) { return 0; }
static uint8_t no_samples(uint8_t b) { (void)b; return 0; }
static const sample_wrapper_t *no_sample(uint8_t b, uint8_t i) {
  (void)b; (void)i; return nullptr;
}

int main() {
  unit_runtime_desc_t desc;
  memset(&desc, 0, sizeof(desc));
  desc.target = unit_header.target;
  desc.api = UNIT_API_VERSION;
  desc.samplerate = 48000;
  desc.frames_per_buffer = 64;
  desc.input_channels = 0;
  desc.output_channels = 2;
  desc.get_num_sample_banks = no_banks;
  desc.get_num_samples_for_bank = no_samples;
  desc.get_sample = no_sample;

  if (unit_init(&desc) != k_unit_err_none) {
    printf("unit_init failed\n");
    return 1;
  }

  printf("# %s: %u params\n", unit_header.name, unit_header.num_params);
  for (uint32_t id = 0; id < unit_header.num_params; ++id) {
    const unit_param_t &p = unit_header.params[id];
    const int32_t values[] = { p.min, (p.min + p.max) / 2, p.max };
    for (int v = 0; v < 3; ++v) {
      g_seen.param_count = 0;
      g_seen.last_index = 0xFFFF;
      g_seen.last_value = 0xFFFF;
      unit_set_param_value((uint8_t)id, values[v]);
      if (g_seen.param_count == 0) {
        printf("%2u %-12s %6d -> wrapper\n", id, p.name, values[v]);
      } else {
        printf("%2u %-12s %6d -> osc[%u] = %u\n", id, p.name, values[v],
               g_seen.last_index, g_seen.last_value);
      }
    }
  }
  unit_teardown();
  return 0;
}
