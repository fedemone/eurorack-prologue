// Copyright 2014 Olivier Gillet.
//
// Author: Olivier Gillet (pichenettes@mutable-instruments.net)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// FORKED AND MODIFIED for the drumlogue port.
//
// Original: eurorack/clouds/dsp/pvoc/phase_vocoder.h at 58b9125.
// See eurorack-opt/README.md for what changed and why, and for how to re-sync
// this file if the submodule moves.
//
// Change: Buffer() transforms one channel per call instead of all of them,
// and spaces the calls out across the hop.  Upstream loops over both
// channels, so in stereo a full forward FFT, the spectral modifier and a full
// inverse run twice back to back.  Upstream can afford that -- it calls
// Buffer() from the main idle loop, where it is not deadline work.  This port
// has no idle loop and calls it from the audio thread, so the pair lands as
// one spike inside a single audio block, once per hop.  Splitting the pair
// halves that spike.
//
// The spacing is not incidental.  Serving the second channel on the very next
// call was tried first and barely helped: the deadline that matters is the
// host's 64-frame render, which at 48 kHz against a 32 kHz engine spans 1.33
// audio blocks, so two transforms one block apart still share a render about
// a third of the time.  Half a hop apart they never do.  Measured end to end
// on CloudsFX at kMaxFftSize 512, Spectral's p99.9 render cost went 95-111%
// of deadline unsplit, 85-121% split by one block, and 63-69% split across
// the hop -- the last being the only one that misses no deadlines at all.
//
// The deferred channel is not skipped, only delayed, and the buffers have
// room for it: STFT's analysis ring is fft_size + hop_size long while
// Buffer() reads fft_size of it, so there is exactly one hop of slack between
// the write pointer and the window being transformed.  Spreading uses half of
// that and leaves the rest.  Everything the deferred call reads and
// everything it writes is unchanged, which is why steady-state output is
// bit-identical to upstream's scheduling rather than merely close -- see
// test_clouds_pvoc_rr.cc, which pins that at every supported FFT size, in
// stereo and mono, hi-fi and lo-fi.
//
// Two things do move, both from the same cause and neither audible.  A
// transform now reads the knobs as they are when it runs rather than a
// fraction of a hop earlier, because STFT holds a live Parameters* and does
// not snapshot (upstream, unforked); that is a couple of milliseconds of
// skew between the channels of a stereo pair.  And a transition -- FREEZE
// engaging, or a mode change reallocating the workspace -- can land in the
// gap between the two channels' transforms, leaving them a frame apart in
// what they captured.  Both are pinned as level comparisons in the test, and
// at the 512 this port ships both come out at 0.000%.
//
// The static assert below is the condition for the slack argument: the hop
// has to be longer than an audio block, or there is nothing to spread into.
// Raise kMaxBlockSize or drop kMaxFftSize far enough and the build stops
// rather than glitching.  Buffer() also carries a runtime guard for the same
// failure, since Process() can in principle be handed more than
// kMaxBlockSize at a time.
//
// Cost: two size_t and an int32_t.  Benefit: the once-per-hop spike halves.
// That is margin rather than a longer window -- the FFT stays at 512, because
// even split, 1024 still misses deadlines on the FX (1.12% of renders).  See
// eurorack-opt/clouds/dsp/pvoc/stft.h and the measured tables in
// eurorack-opt/README.md.
//
// -----------------------------------------------------------------------------
//
// Naive phase vocoder.

#ifndef CLOUDS_DSP_PVOC_PHASE_VOCODER_H_
#define CLOUDS_DSP_PVOC_PHASE_VOCODER_H_

#include "stmlib/stmlib.h"

#include "stmlib/fft/shy_fft.h"

#include "clouds/dsp/frame.h"
#include "clouds/dsp/pvoc/stft.h"
#include "clouds/dsp/pvoc/frame_transformation.h"

namespace clouds {

struct Parameters;

// Set to 0 to restore upstream's scheduling: every channel transformed in
// every Buffer() call.  Kept as a switch because it is what
// test_clouds_pvoc_rr.cc compares against, and because it is the first thing
// to try if Spectral ever sounds wrong on hardware.
#ifndef CLOUDS_PVOC_ROUND_ROBIN
#define CLOUDS_PVOC_ROUND_ROBIN 1
#endif

// The hop must be longer than one audio block, or there is no room to spread
// the channels into and a deferred window would be overwritten before it is
// read.  Init() divides the hop by the channel count to get the spacing, so
// this is also what keeps that spacing from rounding to zero.  hop_size is
// fft_size / hop_ratio, with hop_ratio fixed at 4 in Init().
STATIC_ASSERT(!CLOUDS_PVOC_ROUND_ROBIN || (kMaxFftSize / 4) > kMaxBlockSize,
              clouds_pvoc_hop_too_short_for_round_robin);

class PhaseVocoder {
 public:
  PhaseVocoder() { }
  ~PhaseVocoder() { }

  void Init(
      void** buffer, size_t* buffer_size,
      const float* large_window_lut, size_t largest_fft_size,
      int32_t num_channels,
      int32_t resolution,
      float sample_rate);

  void Process(
      const Parameters& parameters,
      const FloatFrame* input,
      FloatFrame* output,
      size_t size);
  void Buffer();

 private:
  FFT fft_;

  STFT stft_[2];
  FrameTransformation frame_transformation_[2];

  int32_t num_channels_;

  // Round-robin state; only meaningful with CLOUDS_PVOC_ROUND_ROBIN.
  int32_t buffer_channel_;   // whose turn is next
  size_t since_service_;     // Buffer() calls since the last transform
  size_t service_stride_;    // calls to leave between transforms in a hop

  DISALLOW_COPY_AND_ASSIGN(PhaseVocoder);
};

}  // namespace clouds

#endif  // CLOUDS_DSP_PVOC_PHASE_VOCODER_H_
