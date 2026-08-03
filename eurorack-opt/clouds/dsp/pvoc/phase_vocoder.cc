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
// Original: eurorack/clouds/dsp/pvoc/phase_vocoder.cc at 58b9125.
// Only Buffer() differs, plus one line of Init().  See the fork note in
// eurorack-opt/clouds/dsp/pvoc/phase_vocoder.h.
//
// -----------------------------------------------------------------------------
//
// Naive phase vocoder.

#include "clouds/dsp/pvoc/phase_vocoder.h"

#include <algorithm>

#include "stmlib/utils/buffer_allocator.h"

namespace clouds {

using namespace std;
using namespace stmlib;

void PhaseVocoder::Init(
    void** buffer,
    size_t* buffer_size,
    const float* large_window_lut,
    size_t largest_fft_size,
    int32_t num_channels,
    int32_t resolution,
    float sample_rate) {
  num_channels_ = num_channels;
  buffer_channel_ = 0;
  since_service_ = 0;

  size_t fft_size = largest_fft_size;
  size_t hop_ratio = CLOUDS_PVOC_HOP_RATIO;  // see the header for the trade

  // How many Buffer() calls to leave between one channel's transform and the
  // next, so a hop's transforms are spread over the hop instead of bunched.
  // One call is one audio block: this port drives Prepare() once per block,
  // which is also what the static assert in the header assumes.  Dividing the
  // hop by the channel count leaves the last channel served no later than one
  // stride before the next hop arrives, well inside the slack the analysis
  // ring carries.
  //
  // The spacing is also capped.  What it has to achieve is that the two
  // transforms never share a host render, and a render is 1.33 engine blocks
  // -- so a few blocks is enough and anything beyond that buys nothing.  It
  // does cost something: a FREEZE engaging, or a mode change reallocating the
  // workspace, can land between the two channels' transforms and leave them
  // that far apart in what they captured, and the wider the spacing the larger
  // that skew.  `make test-clouds-pvoc-rr` measures it, and without the cap it
  // fails at fft 4096 -- half a hop there is 32 blocks.  Uncapped, the spacing
  // also grows with CLOUDS_PVOC_HOP_RATIO, which is backwards: a longer hop is
  // more slack to sit in, not a reason to spread further.
  //
  // At the shipped geometry (fft 512, hop ratio 2) half a hop is 4 blocks, so
  // the cap changes nothing there; it only reins in the larger sizes the test
  // sweeps and the synth-only override can select.
  const size_t kMaxServiceStride = 4;
  const size_t hop_blocks = (fft_size / hop_ratio) / kMaxBlockSize;
  service_stride_ = hop_blocks / (size_t)num_channels_;
  if (service_stride_ > kMaxServiceStride) {
    service_stride_ = kMaxServiceStride;
  }
  if (service_stride_ < 1) {
    service_stride_ = 1;
  }

  BufferAllocator allocator_0(buffer[0], buffer_size[0]);
  BufferAllocator allocator_1(buffer[1], buffer_size[1]);
  BufferAllocator* allocator[2] = { &allocator_0, &allocator_1 };
  float* fft_buffer = allocator[0]->Allocate<float>(fft_size);
  float* ifft_buffer = allocator[num_channels_ - 1]->Allocate<float>(fft_size);

  size_t num_textures = kMaxNumTextures;
  size_t texture_size = (fft_size >> 1) - kHighFrequencyTruncation;
  for (int32_t i = 0; i < num_channels_; ++i) {
    short* ana_syn_buffer = allocator[i]->Allocate<short>(
        (fft_size + (fft_size >> 1)) * 2);

    num_textures = min(
        allocator[i]->free() / (sizeof(float) * texture_size),
        num_textures);
    stft_[i].Init(
        &fft_,
        fft_size,
        fft_size / hop_ratio,
        fft_buffer,
        ifft_buffer,
        large_window_lut,
        ana_syn_buffer,
        &frame_transformation_[i]);
  }
  for (int32_t i = 0; i < num_channels_; ++i) {
    float* texture_buffer = allocator[i]->Allocate<float>(
        num_textures * texture_size);
    frame_transformation_[i].Init(texture_buffer, fft_size, num_textures);
  }
}

void PhaseVocoder::Process(
    const Parameters& parameters,
    const FloatFrame* input,
    FloatFrame* output, size_t size) {
  const float* input_samples = &input[0].l;
  float* output_samples = &output[0].l;
  for (int32_t i = 0; i < num_channels_; ++i) {
    stft_[i].Process(
        parameters,
        input_samples + i,
        output_samples + i,
        size,
        2);
  }
}

void PhaseVocoder::Buffer() {
#if CLOUDS_PVOC_ROUND_ROBIN
  // One channel per call, spread across the hop: channel 0 goes as soon as
  // its hop is ready and each channel after it waits service_stride_ calls,
  // so a stereo pair's two transforms sit half a hop apart instead of landing
  // in the same audio block.
  //
  // Adjacent blocks were tried first and are not far enough apart.  The
  // deadline that matters is not the engine block, it is the host's 64-frame
  // render, and at 48 kHz against a 32 kHz engine that render covers 1.33
  // blocks -- so two transforms one block apart still share a render about a
  // third of the time, and measured end to end the split bought almost
  // nothing.  Half a hop always crosses a render boundary.  The hop is
  // available to spread into and only a fraction of it is needed.
  //
  // The turn advances only when a transform actually runs, and it starts at
  // channel 0, so the channels are always transformed in ascending order
  // within a hop -- the same order as upstream's loop.  That ordering is
  // load-bearing, which is not obvious: FrameTransformation only writes the
  // lowest (fft_size / 2) - kHighFrequencyTruncation bins of ifft_in, and
  // ifft_in is scratch shared by both channels (see Init above), so every
  // transform inherits its top 16 bins from whichever transform ran last.
  // Upstream leaves those bins alone too -- this is its behaviour, not a
  // choice made here -- but it means a rotation that came up in the other
  // phase would feed each channel the other one's residue and change the
  // output.  Advancing the turn on calls that did nothing would do exactly
  // that, since how many idle calls precede the first hop depends on whether
  // Spectral was entered by Init() or by a mode change.
  for (int32_t n = 0; n < num_channels_; ++n) {
    const int32_t i = buffer_channel_ + n < num_channels_
        ? buffer_channel_ + n
        : buffer_channel_ + n - num_channels_;
    if (!stft_[i].pending()) {
      continue;
    }
    if (i != 0 && since_service_ < service_stride_) {
      break;      // ready, but not yet far enough from the last transform
    }
    // The channels share the fft_/ifft_ scratch pair, so a call must run one
    // channel to completion before another starts.  It does -- the split
    // here is between calls, never inside one.
    stft_[i].Buffer();
    buffer_channel_ = i + 1 < num_channels_ ? i + 1 : 0;
    since_service_ = 0;
    break;
  }
  ++since_service_;

  // Deferring assumes one call is enough to catch up, i.e. that a channel is
  // never more than one hop behind when its turn comes.  That holds for the
  // block sizes this port uses, and the static assert in the header pins it.
  // If it is ever false anyway -- Process() handed more than kMaxBlockSize,
  // say -- the deferred frame's analysis window is about to be overwritten,
  // so take upstream's spike rather than the corruption.
  for (int32_t i = 0; i < num_channels_; ++i) {
    while (stft_[i].pending() > 1) {
      stft_[i].Buffer();
    }
  }
#else
  for (int32_t i = 0; i < num_channels_; ++i) {
    stft_[i].Buffer();
  }
#endif  // CLOUDS_PVOC_ROUND_ROBIN
}

}  // namespace clouds
