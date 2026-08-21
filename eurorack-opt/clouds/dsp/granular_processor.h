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
// Original: eurorack/clouds/dsp/granular_processor.h at 58b9125.
// See eurorack-opt/README.md for what changed and why, and for how to re-sync
// this file if the submodule moves.
//
// Change: two pieces of state for the reverb/diffuser early-out implemented
// in the matching .cc.  Nothing else in this header differs from upstream.
//
// This header must shadow the submodule's copy for EVERY translation unit in
// the build -- it changes sizeof(GranularProcessor), so a build where some
// objects see this and others see upstream's is silently corrupt.  The
// CLOUDS_OPT_ACTIVE marker below is what the port layer checks.
//
// -----------------------------------------------------------------------------
//
// Main processing class.

#ifndef CLOUDS_DSP_GRANULAR_PROCESSOR_H_
#define CLOUDS_DSP_GRANULAR_PROCESSOR_H_

// Marker for the include-path check in clouds-granular.cc / clouds-fx.cc.
#define CLOUDS_OPT_ACTIVE 1

#include "stmlib/stmlib.h"
#include "stmlib/dsp/filter.h"

#include "clouds/dsp/correlator.h"
#include "clouds/dsp/frame.h"
#include "clouds/dsp/fx/diffuser.h"
#include "clouds/dsp/fx/pitch_shifter.h"
#include "clouds/dsp/fx/reverb.h"
#include "clouds/dsp/granular_processor.h"
#include "clouds/dsp/granular_sample_player.h"
#include "clouds/dsp/looping_sample_player.h"
#include "clouds/dsp/pvoc/phase_vocoder.h"
#include "clouds/dsp/sample_rate_converter.h"
#include "clouds/dsp/wsola_sample_player.h"

namespace clouds {

const int32_t kDownsamplingFactor = 2;

enum PlaybackMode {
  PLAYBACK_MODE_GRANULAR,
  PLAYBACK_MODE_STRETCH,
  PLAYBACK_MODE_LOOPING_DELAY,
  PLAYBACK_MODE_SPECTRAL,
  PLAYBACK_MODE_LAST
};

// State of the recording buffer as saved in one of the 4 sample memories.
struct PersistentState {
  int32_t write_head[2];
  uint8_t quality;
  uint8_t spectral;
};

// Data block as saved in one of the 4 sample memories.
struct PersistentBlock {
  uint32_t tag;
  uint32_t size;
  void* data;
};

class GranularProcessor {
 public:
  GranularProcessor() { }
  ~GranularProcessor() { }
  
  void Init(
      void* large_buffer,
      size_t large_buffer_size,
      void* small_buffer,
      size_t small_buffer_size);

  void Process(ShortFrame* input, ShortFrame* output, size_t size);
  void Prepare();

  // Return with no phase-vocoder transform running or scheduled.  Init() and
  // Prepare()'s reallocation path call it themselves; it is public because a
  // caller that means to clear or reuse the buffers it handed this engine has
  // to stop the worker before touching them, and only the caller knows when
  // that is.  Nothing to do in a build without the worker.
  void Quiesce() { phase_vocoder_.Quiesce(); }

  // Ask the next Prepare() to take the full re-initialization path -- audio
  // buffers cleared, FX workspace re-allocated, filters and pitch shifter
  // reset -- the same one a channel-count change takes.
  //
  // Added for the port. The drumlogue's unit_reset() is contracted to return
  // the engine to a neutral state, and it arrives on the control thread,
  // where clearing a buffer the renderer is reading is a race. Prepare()
  // already runs on the audio thread once per block, so setting the flag is
  // the whole handshake.
  inline void RequestBufferReset() {
    reset_buffers_ = true;
  }
  
  inline Parameters* mutable_parameters() {
    return &parameters_;
  }

  inline const Parameters& parameters() const {
    return parameters_;
  }
  
  inline void ToggleFreeze() {
    parameters_.freeze = !parameters_.freeze;
  }
  
  inline void set_freeze(bool freeze) {
    parameters_.freeze = freeze;
  }

  inline bool frozen() const {
    return parameters_.freeze;
  }

  inline void set_silence(bool silence) {
    silence_ = silence;
  }
  
  inline void set_bypass(bool bypass) {
    bypass_ = bypass;
  }
  
  inline bool bypass() const {
    return bypass_;
  }
  
  inline void set_playback_mode(PlaybackMode playback_mode) {
    playback_mode_ = playback_mode;
  }
  
  inline PlaybackMode playback_mode() const { return playback_mode_; }
  
  inline void set_quality(int32_t quality) {
    set_num_channels(quality & 1 ? 1 : 2);
    set_low_fidelity(quality >> 1 ? true : false);
  }
  
  inline void set_num_channels(int32_t num_channels) {
    reset_buffers_ = reset_buffers_ || num_channels_ != num_channels;
    num_channels_ = num_channels;
  }
  
  inline void set_low_fidelity(bool low_fidelity) {
    reset_buffers_ = reset_buffers_ || low_fidelity != low_fidelity_;
    low_fidelity_ = low_fidelity;
  }
  
  inline int32_t quality() const {
    int32_t quality = 0;
    if (num_channels_ == 1) quality |= 1;
    if (low_fidelity_) quality |= 2;
    return quality;
  }
  
  void GetPersistentData(PersistentBlock* block, size_t *num_blocks);
  bool LoadPersistentData(const uint32_t* data);
  void PreparePersistentData();

 private:
  inline int32_t resolution() const {
    return low_fidelity_ ? 8 : 16;
  }

  inline float sample_rate() const {
    return 32000.0f / \
        (low_fidelity_ ? kDownsamplingFactor : 1);
  }
     
  void ResetFilters();
  void ProcessGranular(FloatFrame* input, FloatFrame* output, size_t size);

  PlaybackMode playback_mode_;
  PlaybackMode previous_playback_mode_;
  int32_t num_channels_;
  bool low_fidelity_;
  
  bool silence_;
  bool bypass_;
  bool reset_buffers_;
  float freeze_lp_;
  float dry_wet_;
  
  void* buffer_[2];
  size_t buffer_size_[2];
  
  Correlator correlator_;
  
  GranularSamplePlayer player_;
  WSOLASamplePlayer ws_player_;
  LoopingSamplePlayer looper_;
  PhaseVocoder phase_vocoder_;
  
  Diffuser diffuser_;
  Reverb reverb_;

  // Early-out state for the reverb and diffuser.  Both are pure
  // `out += amount * (wet - out)` mixers, so at amount 0 their output is
  // bit-identical to their input and running them is pure waste -- together
  // about a quarter of a block in Granular mode.  Skipping them is not quite
  // free, though: their delay lines freeze, and content that is stale by
  // however long the skip lasted would be released the moment the amount
  // comes back up.  See the .cc for how each one handles that.
  int32_t reverb_drain_;      // blocks of muted-input flush left before idling
  float diffuser_fade_;       // 0..1 amount ramp applied on resume
  PitchShifter pitch_shifter_;
  stmlib::Svf fb_filter_[2];
  stmlib::Svf hp_filter_[2];
  stmlib::Svf lp_filter_[2];
  
  AudioBuffer<RESOLUTION_8_BIT_MU_LAW> buffer_8_[2];
  AudioBuffer<RESOLUTION_16_BIT> buffer_16_[2];
  
  FloatFrame in_[kMaxBlockSize];
  FloatFrame in_downsampled_[kMaxBlockSize / kDownsamplingFactor];
  FloatFrame out_downsampled_[kMaxBlockSize / kDownsamplingFactor];
  FloatFrame out_[kMaxBlockSize];
  FloatFrame fb_[kMaxBlockSize];
  
  int16_t tail_buffer_[2][256];
  
  Parameters parameters_;
  
  SampleRateConverter<-kDownsamplingFactor, 45, src_filter_1x_2_45> src_down_;
  SampleRateConverter<+kDownsamplingFactor, 45, src_filter_1x_2_45> src_up_;
  
  PersistentState persistent_state_;
  
  DISALLOW_COPY_AND_ASSIGN(GranularProcessor);
};

}  // namespace clouds

#endif  // CLOUDS_DSP_GRANULAR_PROCESSOR_H_
