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
// Change: on ARM the scoring loop runs on NEON, four words at a time.
//
// At -O3 this loop is the largest single cost in Stretch: 28% of the mode's
// mean, and most of its tail. A search scores size/4 + 16 candidates per
// Prepare() over size/32 words each, so at a 2048-sample window one block
// carries ~33,000 word comparisons, each a dozen instructions of SWAR
// popcount -- which is why Stretch's worst renders are many times its median.
// NEON does the same comparison with VCNT on sixteen bytes at once.
//
// The result is the same integer. Both shifts are register-form VSHL, which
// for a count of 32 or more returns 0 -- the same answer the scalar path now
// spells out for offset_bits == 0 -- and the loads touch no word the scalar
// loop does not: the highest is destination[offset_words + num_words], which
// the scalar loop reads too whenever offset_bits is non-zero. Words left over
// below a multiple of four go through the scalar loop. Hosts never see the
// NEON path; `make test-arm` and the drumlogue binaries do.
//
// -----------------------------------------------------------------------------
//
// Search for stretch/shift splicing points by maximizing correlation.

#include "clouds/dsp/correlator.h"

#include <algorithm>

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

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
  uint32_t i = 0;
#ifdef __ARM_NEON
  {
    /* VSHL by a negative count shifts right; at -32 it yields 0. */
    const int32x4_t shift_left = vdupq_n_s32(static_cast<int32_t>(offset_bits));
    const int32x4_t shift_right =
        vdupq_n_s32(static_cast<int32_t>(offset_bits) - 32);
    /* Per-lane byte counts are at most 8, two bytes per 16-bit lane, so a
     * lane gains at most 16 per iteration. num_words is at most
     * kMaxWSOLASize / 32 = 128, i.e. 32 iterations: 512, far inside 16 bits. */
    uint16x8_t counts = vdupq_n_u16(0);
    for (; i + 4 <= num_words; i += 4) {
      uint32x4_t source_bits = vld1q_u32(&source[i]);
      uint32x4_t destination_bits = vorrq_u32(
          vshlq_u32(vld1q_u32(&destination[i]), shift_left),
          vshlq_u32(vld1q_u32(&destination[i + 1]), shift_right));
      uint32x4_t match = vmvnq_u32(veorq_u32(source_bits, destination_bits));
      counts = vpadalq_u8(counts, vcntq_u8(vreinterpretq_u8_u32(match)));
    }
    uint64x2_t sum = vpaddlq_u32(vpaddlq_u16(counts));
    xcorr = static_cast<uint32_t>(vgetq_lane_u64(sum, 0) +
                                  vgetq_lane_u64(sum, 1));
  }
#endif
  for (; i < num_words; ++i) {
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

namespace clouds {
// Counters for the WSOLA split differential; declared in
// wsola_sample_player.h, which is a header and so cannot define them.
uint32_t g_wsola_split_taken = 0;
uint32_t g_wsola_split_refused = 0;
}  // namespace clouds
