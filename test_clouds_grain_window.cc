/*
 * File: test_clouds_grain_window.cc
 *
 * Endpoint test for the eurorack-opt/ grain.h fork.
 *
 * Upstream's Grain::RenderEnvelope() folds the envelope phase to a gain in
 * [0, 1] and hands it to Interpolate(lut_window, gain, 4096.0f). lut_window
 * holds 4097 entries and Interpolate reads both table[i] and table[i + 1], so
 * a grain whose envelope lands exactly on gain 1.0 reads lut_window[4097] —
 * one element past the end. Any grain width that divides 2.0 into an exact
 * binary fraction gets there, which is most of them.
 *
 * The fork folds that endpoint back inside the table, and claims to do it
 * without changing a single sample of the window. Both halves of that claim
 * are checked here rather than argued:
 *
 *   1. This file is compiled twice, once against the submodule and once
 *      against the fork, and the two envelopes are compared sample for
 *      sample. They must be bit-identical.
 *   2. The fork build is run again under AddressSanitizer, which is what
 *      found the overrun in the first place. It must come back clean.
 *
 * Several widths are swept because the endpoint is only reachable when the
 * increment divides the phase exactly; a width the test happened to pick
 * badly would make the whole thing pass by never arriving.
 *
 * Build/run: make test-clouds-grain-window
 */

#include "clouds/dsp/grain.h"

#include <cstdio>
#include <cstdint>

using namespace clouds;

/* Widths spanning the range the granular player uses. The powers of two land
 * on gain 1.0 exactly; the others are here so the sweep is not all one case. */
static const int kWidths[] = { 64, 128, 192, 256, 384, 512, 1024, 2048 };
static const int kMaxWidth = 2048;

int main() {
  static float env[kMaxWidth];

  for (unsigned w = 0; w < sizeof(kWidths) / sizeof(kWidths[0]); ++w) {
    const int width = kWidths[w];

    /* window_shape >= 0.5 selects the smooth window; 1.0 puts the LUT in at
     * full weight, so any change to the table shows up undamped in env[]. */
    for (int shape = 0; shape < 2; ++shape) {
      const float window_shape = shape ? 1.0f : 0.75f;

      Grain g;
      g.Init();
      g.Start(0, 65536, 0, width, 1 << 12, window_shape, 1.0f, 1.0f,
              GRAIN_QUALITY_HIGH);
      for (int i = 0; i < width; ++i) env[i] = 0.0f;
      g.RenderEnvelope<true, GRAIN_QUALITY_HIGH>(env, width);

      /* Printed as hex floats: this output is compared with cmp, and decimal
       * would hide a difference in the last bit or two. */
      for (int i = 0; i < width; ++i)
        printf("%d %.2f %d %a\n", width, window_shape, i, env[i]);
    }
  }
  return 0;
}
