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
// Original: eurorack/clouds/dsp/pvoc/stft.h at 58b9125.
// See eurorack-opt/README.md for what changed and why, and for how to re-sync
// this file if the submodule moves.
//
// Change: the FFT's twiddle-factor generator, RotationPhasor -> LutPhasor.
// Both are stmlib, both produce the same sequence cos(k*pi/2^pass); the
// difference is that RotationPhasor advances by complex multiplication (four
// multiplies and two adds per butterfly group) while LutPhasor walks a table
// built once in Init() -- two pointer bumps and two loads.  It is also the
// more accurate of the two, since repeated rotation drifts.
//
// Cost: 8176 bytes of BSS for the table at kMaxFftSize 4096, less at the
// smaller sizes below.  Benefit, measured under qemu-arm with Granular mode
// as a control for run-to-run noise: Spectral/Granular cost ratio 1.24-1.33
// -> 1.13-1.16 across three paired runs, i.e. roughly half of the FFT's
// excess cost.
//
// Change: kMaxFftSize is smaller than upstream's 4096, and settable at build
// time.  Upstream computes the whole transform in one call, in the idle loop
// where it is not deadline work; this port has no idle loop and runs it on
// the audio thread, where it lands as a single burst once per hop.  At 4096
// that burst measured around 4x a whole block's budget and hung the hardware.
// Peak cost falls as (N/2)*log2(N) while the hop only falls as N, so halving
// the size cuts the burst by more than half and leaves the average roughly
// where it was.  See eurorack-opt/README.md for the measured table.
//
// This is not a free change: it is the analysis window, so it sets what
// Spectral mode *is*.  At 32 kHz, 4096 is a 128 ms window with 7.8 Hz bins
// and 512 is a 16 ms window with 62.5 Hz bins -- much less smeared, tighter,
// far more transient detail.  Different, not worse, but different.
//
// 512 rather than 1024 because of clouds_fx, not the synth.  The synth clears
// its deadline at 1024; the FX does not -- it carries stereo resampling in
// both directions including the dry path, so less of its buffer is left for
// the burst, and at 1024 it still missed 0.93% of renders.  Both units clear
// at 512 with the FX's Spectral tail (p99.9 75-82%) sitting alongside its
// other modes rather than above them.  A synth-only build can raise this to
// 1024 and get the longer window back.
//
// Lowering kMaxFftSize rather than passing a smaller largest_fft_size to
// PhaseVocoder::Init() is deliberate.  STFT::Buffer() switches to
// fft_->Direct(in, out, num_passes) as soon as fft_size != FFT::max_size,
// which is a runtime-sized path that has never executed in this port and
// that the NEON butterfly in shy_fft.h does not cover.  Keeping the two
// equal keeps the compile-time path, and the vectorisation, live.
//
// This header must shadow the submodule's copy for EVERY translation unit in
// the build.  It changes sizeof(FFT), hence sizeof(PhaseVocoder) and
// sizeof(GranularProcessor); a build where some objects see this and others
// see upstream's is silently corrupt.  The same goes for CLOUDS_FFT_SIZE: it
// must be defined for every translation unit or none.  Setting it in one
// project's UDEFS and not in a shared object file is the same class of bug,
// and the CLOUDS_OPT_ACTIVE guard will not catch it because both sides are
// this header.  Change the default here rather than passing -D, unless you
// are sure the flag reaches everything.
//
// -----------------------------------------------------------------------------
//
// STFT with overlap-add.

#ifndef CLOUDS_DSP_PVOC_STFT_H_
#define CLOUDS_DSP_PVOC_STFT_H_

#include "stmlib/stmlib.h"

// #define USE_ARM_FFT

#ifdef USE_ARM_FFT
  #include <arm_math.h>
#else
  #include "stmlib/fft/shy_fft.h"
#endif  // USE_ARM_FFT

namespace clouds {

struct Parameters;

// Any power of two from 256 to 4096 works -- all five were round-tripped
// against ShyFFT before this was made settable, so 2048 and 512 are fine even
// though they are not powers of four.  4096 restores upstream's window (and
// upstream's burst).  The window LUT is strided by
// LUT_SINE_WINDOW_4096_SIZE / fft_size, which is why the ceiling is 4096.
#ifndef CLOUDS_FFT_SIZE
#define CLOUDS_FFT_SIZE 512
#endif

const size_t kMaxFftSize = CLOUDS_FFT_SIZE;

STATIC_ASSERT(CLOUDS_FFT_SIZE >= 256 && CLOUDS_FFT_SIZE <= 4096 &&
              (CLOUDS_FFT_SIZE & (CLOUDS_FFT_SIZE - 1)) == 0,
              clouds_fft_size_must_be_a_power_of_two_from_256_to_4096);

#ifdef USE_ARM_FFT
  typedef arm_rfft_fast_instance_f32 FFT;
#else
  typedef stmlib::ShyFFT<float, kMaxFftSize, stmlib::LutPhasor> FFT;
#endif  // USE_ARM_FFT

typedef class FrameTransformation Modifier;

class STFT {
 public:
  STFT() { }
  ~STFT() { }
  
  struct Frame { short l; short r; };
  
  void Init(
      FFT* fft,
      size_t fft_size,
      size_t hop_size,
      float* fft_buffer,
      float* ifft_buffer,
      const float* window_lut,
      short* stft_frame_processor_buffer,
      Modifier* modifier);

  void Reset();

  void Process(
      const Parameters& parameters,
      const float* input,
      float* output,
      size_t size,
      size_t stride);

  void Buffer();
  
 private:
  FFT* fft_;
  size_t fft_size_;
  size_t fft_num_passes_;
  size_t hop_size_;
  size_t buffer_size_;
  float* fft_in_;
  float* fft_out_;
  float* ifft_out_;
  float* ifft_in_;
  
  const float* window_;
  size_t window_stride_;

  short* analysis_;
  short* synthesis_;
  
  size_t buffer_ptr_;
  size_t process_ptr_;
  size_t block_size_;
  
  size_t ready_;
  size_t done_;
  
  const Parameters* parameters_;
  
  Modifier* modifier_;
  
  DISALLOW_COPY_AND_ASSIGN(STFT);
};

}  // namespace clouds

#endif  // CLOUDS_DSP_PVOC_STFT_H_
