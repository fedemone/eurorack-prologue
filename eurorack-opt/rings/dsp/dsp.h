// Copyright 2015 Olivier Gillet.
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
// Original: eurorack/rings/dsp/dsp.h at 58b9125.
// Change: kMaxBlockSize follows OSC_NATIVE_BLOCK_SIZE when the build defines
// it, and RINGS_OPT_DSP_H_ACTIVE says this copy is the one in effect.  See
// eurorack-opt/README.md.
//
// The drumlogue asks for 64 frames per render.  At upstream's 24 that is
// 2.67 blocks, so the adapter renders three blocks in two renders out of
// three and two in the third: the expensive renders do 72 samples of work in
// a 64-sample deadline and pay Part::Process()'s per-block overhead three
// times.  At 64 every render is one block, and the most expensive render
// costs 16-21% fewer instructions in every model.
//
// Upstream's own code is written for any block size: Part::Process() takes
// the size, the note filter and the sympathetic strings' LFOs are
// initialised from kSampleRate / kMaxBlockSize, and the one per-block
// smoother that is not -- the sympathetic strings' frequency glide -- is
// compensated in eurorack-opt/rings/dsp/part.cc.
//
// Builds that do not define OSC_NATIVE_BLOCK_SIZE keep upstream's 24.  On the
// drumlogue the two are equal by construction rather than by two numbers
// being kept in step, which matters: rings-resonator.cc hands the adapter
// kMaxBlockSize samples into a buffer of OSC_NATIVE_BLOCK_SIZE, and it
// static_asserts that they agree.
//
// This changes sizeof(rings::Part), so every translation unit that includes
// it must see this copy -- eurorack-opt ahead of eurorack on the include
// path, as it already has to be for performance_state.h.
//
// -----------------------------------------------------------------------------
//
// Utility DSP routines.

#ifndef RINGS_DSP_DSP_H_
#define RINGS_DSP_DSP_H_

#include "stmlib/stmlib.h"

// #define MIC_W
#define BRYAN_CHORDS

// Marks that this copy, not the submodule's, is the one in effect.
#define RINGS_OPT_DSP_H_ACTIVE 1

namespace rings {
  
static const float kSampleRate = 48000.0f;
const float a3 = 440.0f / kSampleRate;
#if defined(OSC_NATIVE_BLOCK_SIZE)
const size_t kMaxBlockSize = OSC_NATIVE_BLOCK_SIZE;
#else
const size_t kMaxBlockSize = 24;
#endif

}  // namespace rings

#endif  // RINGS_DSP_DSP_H_
