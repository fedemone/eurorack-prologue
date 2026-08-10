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
// Original: eurorack/rings/dsp/performance_state.h at 58b9125.
// One line differs: kNumChords, 11 -> 14.  See eurorack-opt/README.md.
//
// The three added chords live in eurorack-opt/rings/dsp/part.cc, which is
// forked alongside this header because the two have to move together: the
// table there is indexed by performance_state.chord with no bounds check of
// its own, so a table shorter than kNumChords is an out-of-bounds read on the
// audio thread rather than a missing feature.  Upstream sizes that table with
// a literal 11 rather than with kNumChords, which is exactly why bumping this
// constant alone would not be safe -- the fork changes the dimension to
// kNumChords so the two can no longer drift apart.
//
// One other file keys off this constant: rings/dsp/string_synth_part.cc has a
// parallel chord table of its own, also dimensioned [.][kNumChords][.].  It is
// left unforked, so its three new rows initialise to zero, which reads as a
// unison on the root rather than as garbage.  That is dead code in this port
// -- StringSynthPart is Rings' polyphonic string-synth easter egg and nothing
// here instantiates it -- but if a later change ever reaches it, those three
// rows are the thing to fill in.
//
// -----------------------------------------------------------------------------
//
// Note triggering state.

#ifndef RINGS_DSP_PERFORMANCE_STATE_H_
#define RINGS_DSP_PERFORMANCE_STATE_H_

namespace rings {

// 11 upstream; the drumlogue port adds Quartal, Just7 and Slendro on the end.
const int32_t kNumChords = 14;

struct PerformanceState {
  bool strum;
  bool internal_exciter;
  bool internal_strum;
  bool internal_note;

  float tonic;
  float note;
  float fm;
  int32_t chord;
};

}  // namespace rings

#endif  // RINGS_DSP_PERFORMANCE_STATE_H_
