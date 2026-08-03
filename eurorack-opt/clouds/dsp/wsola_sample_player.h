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
// -----------------------------------------------------------------------------
//
// FORKED AND MODIFIED for the drumlogue port.
//
// Original: eurorack/clouds/dsp/wsola_sample_player.h at 58b9125.
// See eurorack-opt/README.md for what changed and why, and for how to re-sync
// this file if the submodule moves.
//
// Change: LoadCorrelator() is split across two Prepare() calls.  It packs
// sign bits for a window_size_ source window and a 2 * window_size_
// destination window, which at the default size is around 6144 interpolated
// buffer reads -- doubled again in stereo -- and upstream does all of it in
// whichever block follows a window being scheduled.  Upstream can afford
// that; it runs this from the main idle loop.  This port drives Prepare()
// from the audio thread, so it lands as one spike per window, and with
// Spectral's FFT burst gone this is the largest one left in the engine.
//
// Doing the source window on one call and the destination window on the next
// leaves the worst block paying two thirds of what it did.  The split only
// engages above kCorrelatorSplitWindow: the burst scales with window_size_,
// and so does the number of blocks between one window and the next, so at
// small sizes there is both nothing worth splitting and no room to split into.
//
// Which half is deferred, and when the split is safe at all, were both
// settled by a differential sweep against upstream rather than by argument.
// See the notes in LoadCorrelator().
//
// WSOLASamplePlayer gains two members, so this changes sizeof(GranularProcessor).
// The fork is all-or-nothing for that reason; see the CLOUDS_OPT_ACTIVE guard
// in granular_processor.h.
//
//
// WSOLA playback.

#ifndef CLOUDS_DSP_WSOLA_SAMPLE_PLAYER_H_
#define CLOUDS_DSP_WSOLA_SAMPLE_PLAYER_H_

#include <algorithm>

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "stmlib/stmlib.h"
#include "stmlib/dsp/units.h"

#include "clouds/dsp/audio_buffer.h"
#include "clouds/dsp/correlator.h"
#include "clouds/dsp/frame.h"
#include "clouds/dsp/window.h"
#include "clouds/dsp/parameters.h"
#include "clouds/resources.h"

namespace clouds {

const int32_t kMaxWSOLASize = 4096;

using namespace stmlib;

class WSOLASamplePlayer {
 public:
  WSOLASamplePlayer() { }
  ~WSOLASamplePlayer() { }
  
  void Init(
      Correlator* correlator,
      int32_t num_channels) {
    correlator_ = correlator;
    num_channels_ = num_channels;

    pitch_ = 0.0f;
    position_ = 0.0f;
    smoothed_pitch_ = 0.0f;

    windows_[0].Init();
    windows_[1].Init();

    next_pitch_ratio_ = 1.0f;
    correlator_loaded_ = true;
    search_source_ = 0;
    search_target_ = 0;
    load_stage_ = 0;
    load_num_samples_ = 0;
    head_margin_ = 0;          // no split until a window has been scheduled
    
    window_size_ = kMaxWSOLASize / 2;
    env_phase_ = 0.0f;
    env_phase_increment_ = 0.5f;
    elapsed_ = 0;
  }
  
  template<Resolution resolution>
  void Play(
      const AudioBuffer<resolution>* buffer,
      const Parameters& parameters,
      float* out,
      size_t size) {
    elapsed_++;
    if (parameters.trigger) {
      env_phase_ = 0.0f;
      env_phase_increment_ = 1.0f / static_cast<float>(elapsed_);
      CONSTRAIN(env_phase_increment_, 0.0001f, 0.1f);
      elapsed_ = 0;
    }
    env_phase_ += env_phase_increment_;
    if (env_phase_ >= 1.0f) {
      env_phase_ = 1.0;
    }
    position_ = parameters.position;
    position_ += (1.0f - env_phase_) * (1.0f - position_);
    
    pitch_ = parameters.pitch;
    size_factor_ = parameters.size;
    
    if (windows_[0].done() && windows_[1].done()) {
      windows_[1].MarkAsRegenerated();
      ScheduleAlignedWindow(buffer, &windows_[0]);
    }
    
    while (size--) {
      // Sum the two windows.
      std::fill(&out[0], &out[kMaxNumChannels], 0);
      for (int32_t i = 0; i < 2; ++i) {
        windows_[i].OverlapAdd(buffer, out, num_channels_);
      }

      // Regenerate expired windows.
      for (int32_t i = 0; i < 2; ++i) {
        if (windows_[i].needs_regeneration()) {
          windows_[i].MarkAsRegenerated();
          ScheduleAlignedWindow(buffer, &windows_[1 - i]);
          windows_[1 - i].OverlapAdd(buffer, out, num_channels_);
        }
      }
      out += 2;
    }
  }
  
  template<int32_t num_channels, Resolution resolution>
  int32_t ReadSignBits(
      const AudioBuffer<resolution>* buffer,
      int32_t phase_increment,
      int32_t source,
      int32_t size,
      uint32_t* destination) {
    int32_t phase = 0;
    uint32_t bits = 0;
    uint32_t bit_counter = 0;
    int32_t num_samples = 0;
    if (source < 0) {
      source += buffer->size();
    }
    while ((phase >> 16) < size) {
      int32_t integral = source + (phase >> 16);
      uint16_t fractional = phase & 0xffff;
      float s = buffer[0].ReadLinear(integral, fractional);
      if (num_channels == 2) {
        s += buffer[1].ReadLinear(integral, fractional);
      }
      bits |= s > 0.0f ? 1 : 0;
      if ((bit_counter & 0x1f) == 0x1f) {
        destination[bit_counter >> 5] = bits;
        num_samples += 32;
      }
      ++bit_counter;
      bits <<= 1;
      phase += phase_increment;
    }
    while (bit_counter & 0x1f) {
      if ((bit_counter & 0x1f) == 0x1f) {
        destination[bit_counter >> 5] = bits;
        num_samples += 32;
      }
      ++bit_counter;
      bits <<= 1;
    }
    return num_samples;
  }
  
  // Only windows at least this long are split.
  //
  // The load must finish before the next ScheduleAlignedWindow() reads
  // best_match(), so what decides whether a split is safe is how many
  // Prepare() calls separate one window from the next.  Measured, driving the
  // real engine and recording that gap:
  //
  //     window_size_   pitch ratio   min gap between windows
  //     ~2048          1.0           ~500 blocks
  //     ~2048          4.0           8 blocks
  //     ~512           1.0           8 blocks
  //     <256           1.0           3 blocks
  //
  // The gap shrinks with the window size and with the pitch ratio -- a shifted
  // voice eats its window faster -- but the *burst* shrinks with the window
  // size too, and faster: the load is O(window_size_), so at the 128-sample
  // floor it is a sixteenth of what it is at 2048.  Burst and slack move
  // together in the helpful direction, and 1024 is the smallest threshold that
  // keeps at least eight blocks of gap everywhere in the table while giving up
  // only the sizes where there was nothing to win.
  //
  // A guard on the *measured* previous gap was tried instead and is worse:
  // the gap varies from window to window, so a long interval followed by a
  // short one lets a split through that does not fit. It differed from
  // upstream in 18 of 90 sweep points, against 0 for this one.
  static const int32_t kCorrelatorSplitWindow = 1024;

  // ...and only when the destination window's top edge is at least this far
  // behind the buffer's write head.
  //
  // That edge sits at head - limit * POSITION, so at POSITION 0 it *is* the
  // head, and deferring the read by a block reads a block of audio recorded
  // since the window was scheduled -- 32 samples of a 2048-sample correlation
  // window, occasionally enough to flip the best match.  This is what the
  // differential sweep was still catching at POSITION 0 with +-24 semitones,
  // where the pitch ratio pulls the read hardest against the head.  Two
  // blocks of margin covers the one-block deferral with room over.
  //
  // The cost is that POSITION 0 does not get the split.  It is one end of one
  // knob, and correctness there is worth more than the burst.
  static const int32_t kCorrelatorHeadMargin = 2 * kMaxBlockSize;

  template<Resolution resolution>
  void LoadCorrelator(const AudioBuffer<resolution>* buffer) {
    if (correlator_loaded_) {
      return;
    }
    float stride = window_size_ / 2048.0f;
    CONSTRAIN(stride, 1.0f, 2.0f);
    stride *= 65536.0f;
    int32_t increment = static_cast<int32_t>(
          stride * (next_pitch_ratio_ < 1.25f ? 1.25f : next_pitch_ratio_));

    // Stage 0: the source window.  Stage 1: the destination window, which is
    // twice as long, plus the search itself.  ScheduleAlignedWindow() puts
    // load_stage_ back to 0, so a window arriving mid-split restarts the load
    // against the new search_source_/search_target_ rather than pairing halves
    // that were read against different ones.
    //
    // Which half is deferred was decided by measurement, not by argument: the
    // other order -- destination first -- differed from upstream at 12 of 90
    // sweep points against this order's 3, because search_source_ is the
    // window WSOLA is about to play and so tracks the read position, which
    // sits closer to the write head than search_target_ does.
    if (load_stage_ == 0) {
      if (num_channels_ == 1) {
        load_num_samples_ = ReadSignBits<1>(
            buffer,
            increment,
            search_source_,
            window_size_,
            correlator_->source());
      } else {
        load_num_samples_ = ReadSignBits<2>(
            buffer,
            increment,
            search_source_,
            window_size_,
            correlator_->source());
      }
      load_stage_ = 1;
      if (window_size_ >= kCorrelatorSplitWindow &&
          head_margin_ >= kCorrelatorHeadMargin) {
        return;                 // destination window on the next call
      }
    }

    if (num_channels_ == 1) {
      ReadSignBits<1>(
          buffer,
          increment,
          search_target_ - window_size_,
          window_size_ * 2,
          correlator_->destination());
    } else {
      ReadSignBits<2>(
          buffer,
          increment,
          search_target_ - window_size_,
          window_size_ * 2,
          correlator_->destination());
    }
    correlator_->StartSearch(
        load_num_samples_,
        search_target_ - window_size_ + (window_size_ >> 1),
        increment);
    correlator_loaded_ = true;
    load_stage_ = 0;
  }
 private:
  template<Resolution resolution>
  void ScheduleAlignedWindow(
      const AudioBuffer<resolution>* buffer,
      Window* window) {
    int32_t next_window_position = correlator_->best_match();
    correlator_loaded_ = false;
    load_stage_ = 0;
    window->Start(
        buffer->size(),
        next_window_position - (window_size_ >> 1),
        window_size_,
        static_cast<uint32_t>(next_pitch_ratio_ * 65536.0f));
    
    float pitch_error = pitch_ - smoothed_pitch_;
    float pitch_error_sign = pitch_error < 0.0f ? -1.0 : 1.0;
    pitch_error *= pitch_error_sign;
    if (pitch_error >= 12.0f) {
      pitch_error = 12.0f;
    }
    smoothed_pitch_ += pitch_error * pitch_error_sign;
    float pitch_ratio = SemitonesToRatio(smoothed_pitch_);
    float inv_pitch_ratio = SemitonesToRatio(-smoothed_pitch_);
    next_pitch_ratio_ = pitch_ratio;
    
    float size_factor = SemitonesToRatio((size_factor_ - 1.0f) * 60.0f);
    int32_t new_window_size = static_cast<int32_t>(size_factor * kMaxWSOLASize);
    if (abs(new_window_size - window_size_) > 64) {
      int32_t error = (new_window_size - window_size_) >> 5;
      new_window_size = window_size_ + error;
      window_size_ = new_window_size - (new_window_size % 4);
    }
    
    // The center offset of the window we want to mix in.
    int32_t limit = buffer->size();
    limit -= static_cast<int32_t>(2.0f * window_size_ * inv_pitch_ratio);
    limit -= 2 * window_size_;
    if (limit < 0) {
      limit = 0;
    }
    
    float position = position_;
    int32_t target_position = buffer->head();
    target_position -= static_cast<int32_t>(limit * position);
    target_position -= window_size_;
    
    search_source_ = next_window_position;
    search_target_ = target_position;
    head_margin_ = static_cast<int32_t>(limit * position);
  }
  
  Correlator* correlator_;

  Window windows_[2];

  int32_t window_size_;
  int32_t num_channels_;
  
  float pitch_;
  float smoothed_pitch_;
  float position_;
  float size_factor_;
  
  float next_pitch_ratio_;
  bool correlator_loaded_;
  int32_t search_source_;
  int32_t search_target_;

  // Split-load state; see LoadCorrelator().  load_stage_ is 0 when no load is
  // in flight and 1 when the source window has been packed and the
  // destination window has not.  load_num_samples_ carries ReadSignBits()'s
  // return across the gap, since StartSearch() needs it and it is produced by
  // the first half.
  int32_t load_stage_;
  int32_t load_num_samples_;
  int32_t head_margin_;
  
  float env_phase_;
  float env_phase_increment_;
  int32_t elapsed_;
  
  DISALLOW_COPY_AND_ASSIGN(WSOLASamplePlayer);
};

}  // namespace clouds

#endif  // CLOUDS_DSP_WSOLA_SAMPLE_PLAYER_H_
