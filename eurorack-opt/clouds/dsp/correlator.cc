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
// Original: eurorack/clouds/dsp/correlator.cc at 58b9125.
// See eurorack-opt/README.md for what changed and why, and for how to re-sync
// this file if the submodule moves.
//
// Change: the word-straddling shift in EvaluateNextCandidate() no longer
// shifts by 32.
//
// The correlator packs sign bits into 32-bit words and scores a candidate by
// counting matching bits, reassembling each destination word from two:
//
//     destination_bits  = destination[i]     << offset_bits;
//     destination_bits |= destination[i + 1] >> (32 - offset_bits);
//
// offset_bits is candidate_ & 0x1f, so it is zero for every candidate that
// happens to be a multiple of 32 -- a third of a percent of them, and reached
// on ordinary settings. The second shift is then `>> 32`, which C++ leaves
// undefined, and the two targets this repository builds for disagree about
// it in the worst possible way: ARM's variable shift produces 0, which is
// what the algebra wants, while x86 masks the count to five bits and shifts
// by 0, returning the whole word and corrupting the score.
//
// The device is therefore already doing the right thing, and this changes
// nothing on hardware. It matters because most of this repository's evidence
// about Clouds is gathered on the host -- the WSOLA split was settled by a
// 120-point differential sweep, and Stretch is the mode that sweep exercises.
// A host that scores splice candidates differently from the device is not a
// stand-in for it. Making the zero case explicit gives both targets ARM's
// answer and takes the undefined behaviour out of the loop.
//
// -----------------------------------------------------------------------------
//
// Search for stretch/shift splicing points by maximizing correlation.

#include "clouds/dsp/correlator.h"

#include <algorithm>

namespace clouds {

using namespace std;

void Correlator::Init(uint32_t* source, uint32_t* destination) {
  source_ = source;
  destination_ = destination;
  offset_ = 0;
  best_match_ = 0;
  done_ = true;
}

void Correlator::EvaluateNextCandidate() {
  if (done_) {
    return;
  }
  uint32_t num_words = size_ >> 5;
  uint32_t offset_words = candidate_ >> 5;
  uint32_t offset_bits = candidate_ & 0x1f;
  uint32_t* source = &source_[0];
  uint32_t* destination = &destination_[offset_words];
  
  uint32_t xcorr = 0;
  for (uint32_t i = 0; i < num_words; ++i) {
    uint32_t source_bits = source[i];
    uint32_t destination_bits = 0;
    destination_bits |= destination[i] << offset_bits;
    /* offset_bits == 0 would make this `>> 32`, which is undefined and which
     * ARM and x86 answer differently.  Nothing straddles the word boundary in
     * that case, so the term is zero; see the fork note above. */
    if (offset_bits) {
      destination_bits |= destination[i + 1] >> (32 - offset_bits);
    }
    uint32_t count = ~(source_bits ^ destination_bits);
    count = count - ((count >> 1) & 0x55555555);
    count = (count & 0x33333333) + ((count >> 2) & 0x33333333);
    count = (((count + (count >> 4)) & 0xf0f0f0f) * 0x1010101) >> 24;
    xcorr += count;
  }
  if (xcorr > best_score_) {
    best_match_ = candidate_;
    best_score_ = xcorr;
  }
  ++candidate_;
  done_ = candidate_ >= size_;
}

void Correlator::StartSearch(
    int32_t size,
    int32_t offset,
    int32_t increment) {
  offset_ = offset;
  increment_ = increment;
  best_score_ = 0;
  best_match_ = 0;
  candidate_ = 0;
  size_ = size;
  done_ = false;
}

}  // namespace clouds
