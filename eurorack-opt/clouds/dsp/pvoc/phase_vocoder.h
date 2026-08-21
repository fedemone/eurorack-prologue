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
// Change: the transform runs on a worker thread (CLOUDS_PVOC_WORKER, below).
//
// Spreading the pair made the spike half as tall.  This removes it from the
// audio thread instead, which is where upstream had it all along -- Clouds
// calls Buffer() from its main idle loop, and the port put it on the audio
// thread only because it had no idle loop to call it from.  A worker at
// SCHED_OTHER is that idle loop.
//
// The safety argument is the same slack the round-robin spends, used the same
// way and measured rather than assumed.  A transform may run any time within
// one hop of being scheduled and still read and write exactly what it would
// have: `make test-clouds-pvoc-defer` defers every transform by N calls, on
// the audio thread so the result is deterministic, and the fixed-parameter
// hashes do not move at any N it sweeps.
//
// Three properties hold it together, and each is checked:
//
//   The audio thread never blocks on the worker and never takes a lock to
//      reach it.  It posts with a release store and a sem_post; if the slot
//      is busy it returns and tries next call.  There is no path where the
//      render waits for a SCHED_OTHER thread -- which on a single core would
//      be a livelock, the renderer spending the budget that the worker needs
//      in order to release it.  The one exception is Quiesce(), which is a
//      mode change, not deadline work.
//
//   A transform runs on exactly one thread at a time.  Both channels share
//      the fft_/ifft_ scratch, so this is not a formality; BufferWorker()
//      states the invariant that enforces it.  `make test-tsan` runs the
//      handoff under ThreadSanitizer and reports nothing.
//
//   The worst case is the previous behaviour.  If the thread cannot be
//      created, every transform runs inline exactly as before.  If the worker
//      falls behind, the catch-up valve takes one transform back onto the
//      audio thread -- one, capped, because an earlier version drained the
//      whole backlog and that was worse than having no worker at all.
//
// Measured on ARM under QEMU with `make bench-clouds-spike`, Spectral, per
// 32-sample block against a 1 ms deadline.  The worker is starved in this
// bench -- it drives blocks flat out, so there is no wall clock for the
// worker to run in -- which makes it the pessimistic case, the fallback path
// rather than the intended one:
//
//                              mean     p99    p99.9     max   over deadline
//   worker off                7.50%  34.02%   40.02%  41.35%     0.00%
//   worker on, valve capped   5.45%  22.74%   29.06%  39.72%     0.00%
//   worker on, uncapped       5.49%  60.56%   80.56% 540.22%     0.04%
//
// The third row is the version that drained the backlog in one call, kept
// here because it is the reason the cap exists.
//
// Given real time the picture is different again: `make
// test-clouds-pvoc-worker` drives the engine at the sample rate, and there
// the worker takes every transform -- 150 of them, none forced back -- and
// the output is bit-identical to the same build with the worker switched off.
// That is the claim this change stands on, and QEMU cannot make it, because
// what it depends on is the scheduler.
//
// -----------------------------------------------------------------------------
//
// Naive phase vocoder.

#ifndef CLOUDS_DSP_PVOC_PHASE_VOCODER_H_
#define CLOUDS_DSP_PVOC_PHASE_VOCODER_H_

#include "stmlib/stmlib.h"

#include "stmlib/fft/shy_fft.h"

#include "clouds/dsp/frame.h"
#include "clouds/dsp/parameters.h"
#include "clouds/dsp/pvoc/stft.h"
#include "clouds/dsp/pvoc/frame_transformation.h"

// Run the transform on a worker thread instead of the audio thread.  Settled
// here, above the namespace, because it pulls in system headers and those
// must not land inside namespace clouds.  The rationale is with the rest of
// the scheduling switches below, next to CLOUDS_PVOC_ROUND_ROBIN.
//
// Gated on __linux__ rather than a -D: the drumlogue is
// arm-unknown-linux-gnueabihf and has pthreads; prologue, minilogue-xd and
// NTS-1 are bare-metal Cortex-M4 through arm-none-eabi and have neither
// pthreads nor a scheduler to take advantage of one.  This changes
// sizeof(PhaseVocoder), so it has to be identical in every translation unit
// of a build -- a per-project flag set in one config.mk and not in a shared
// object file is the footgun stft.h warns about for CLOUDS_FFT_SIZE, and a
// predefined macro cannot be got wrong that way.
#ifndef CLOUDS_PVOC_WORKER
#if defined(__linux__)
#define CLOUDS_PVOC_WORKER 1
#else
#define CLOUDS_PVOC_WORKER 0
#endif
#endif

// Test builds only: make the audio thread wait for each transform to land, so
// the worker's timing stops being a variable.  See BufferWorker().  A
// shipping unit must never set this -- it is the priority inversion the
// design exists to avoid.
#ifndef CLOUDS_PVOC_WORKER_SYNC
#define CLOUDS_PVOC_WORKER_SYNC 0
#endif

#if CLOUDS_PVOC_WORKER
#include <atomic>
#include <pthread.h>
#include <semaphore.h>
#endif

namespace clouds {

// Set to 0 to restore upstream's scheduling: every channel transformed in
// every Buffer() call.  Kept as a switch because it is what
// test_clouds_pvoc_rr.cc compares against, and because it is the first thing
// to try if Spectral ever sounds wrong on hardware.
#ifndef CLOUDS_PVOC_ROUND_ROBIN
#define CLOUDS_PVOC_ROUND_ROBIN 1
#endif

// Analysis/synthesis overlap: hop_size = fft_size / CLOUDS_PVOC_HOP_RATIO.
// Upstream is 4 (75% overlap); this port runs 2 (50% overlap), taken
// deliberately after hardware reported Spectral still clicking with the
// smaller FFT in place.
//
// This is the only lever in the engine that changes Spectral's cost by a
// factor rather than a few percent, because it changes how *often* a
// transform runs rather than how much one costs.  Measured with
// `make bench-clouds-spike`, ARM under QEMU, per 32-sample engine block:
//
//   hop_ratio 4:  Spectral mean 16.15%,  of which Prepare() 13.33%
//   hop_ratio 2:  Spectral mean  9.08%,  of which Prepare()  6.45%
//
// -44% overall and -52% on the phase vocoder itself.  The peak is unchanged
// (p99.9 stays ~55%) — it is the same transform, just half as many of them —
// and the other three modes do not use the phase vocoder at all, so they do
// not move.
//
// Reconstruction does not suffer.  The window is applied at both analysis and
// synthesis, so the effective window is sine squared = Hann, and Hann sums to
// exactly 1 at 50% overlap just as it sums to 2 at 75%; `inverse_window_size`
// in stft.cc is already derived from hop_size_, so the normalisation follows.
// `make test-clouds-cola` measures it rather than taking the algebra's word
// for it: the reconstruction ripple of a swept sine is 0.62 dB at both ratio
// 4 and ratio 2, and 9.48 dB at ratio 1 — the last being the control that
// shows the test can see a COLA failure at all.
//
// What it does cost is phase-vocoder artifacts on *modified* spectra, which
// is most of what Spectral is for: fewer overlapping frames means less
// averaging of the phase reconstruction, so transients smear differently and
// heavy Warp/Quantize/Pitch settings get grainier.  Pass-through is
// unaffected.  That is a judgement about how the instrument should sound
// rather than a correctness question, and the judgement made here is that a
// mode which clicks is worth less than a mode which is grainier: Spectral on
// this port is already not upstream's effect, and being usable matters more
// than the last of the smoothing.  Build with -DCLOUDS_PVOC_HOP_RATIO=4 to
// get upstream's overlap back, at upstream's cost.
#ifndef CLOUDS_PVOC_HOP_RATIO
#define CLOUDS_PVOC_HOP_RATIO 2
#endif

STATIC_ASSERT(CLOUDS_PVOC_HOP_RATIO == 2 || CLOUDS_PVOC_HOP_RATIO == 4,
              clouds_pvoc_hop_ratio_must_be_2_or_4);

// Run each transform this many Buffer() calls after the scheduler picks it,
// still on the audio thread.  A test instrument, not a shipping setting: it
// is how the slack argument above gets measured rather than asserted, and it
// is how the worker below is tested without depending on thread timing.
//
// The claim it exists to check is that a transform deferred by up to one hop
// reads and writes exactly what it would have read and written immediately.
// `make test-clouds-pvoc-defer` sweeps N and reports both halves of that: the
// fixed-parameter hashes, which must not move, and how each transform
// resolved -- on time from the queue, or clawed back onto the audio thread by
// the catch-up valve in Buffer().
//
// Measured at the shipped geometry (fft 512, hop 256, 32-sample blocks, 348
// transforms), stereo:
//
//   N <= 4   348 on time,   0 forced
//   N = 5    174 on time, 173 forced
//   N = 8    173 on time, 173 forced
//   N >= 9     0 on time, 346 forced
//
// Every fixed-parameter hash is identical at every N, including the ones far
// past the slack -- that is the valve doing its job, not the slack being
// infinite.  Overrunning costs the CPU saving, never correctness.
//
// The two-step shape is the round-robin's stride showing through.  Both
// channels' hops complete on the same call and each window then has one hop
// (8 calls) before Process() overwrites it, but the scheduler makes channel 1
// wait service_stride_ = 4 calls, so channel 1 has 4 left and channel 0 has 8.
// Hence half the transforms start being forced at N=5 and the rest at N=9.
// This is why the worker below drops the stride to 1: spreading the pair only
// ever existed to keep two bursts out of one audio block, which is moot once
// neither burst is on the audio thread, and it was costing half the budget.
//
// N is in Buffer() calls, and this port drives one per audio block, so N is
// also blocks.  Values above the slack are useful too -- they are how the
// fallback path gets exercised.
#ifndef CLOUDS_PVOC_DEFER_BLOCKS
#define CLOUDS_PVOC_DEFER_BLOCKS 0
#endif

// Run the transform on a worker thread instead of the audio thread.
//
// This is the last lever of any size left on Spectral, and it is a different
// kind of lever from the others: the FFT size, the hop ratio and the warp
// skip all made the work smaller, this one moves work that is already small
// enough off the deadline.  What overran was never the total -- Spectral's
// mean sits a few percent of a block -- it is that the whole transform lands
// inside one audio block once per hop, and that block has to fit in a render
// alongside everything else.
//
// It is upstream's own structure, arrived at from the other end.  Clouds runs
// Buffer() from its main idle loop, where a spike costs nothing because it is
// not deadline work; the port had no idle loop and put it on the audio thread.
// A low-priority worker is an idle loop.
//
// The switch itself is above the namespace, with the system headers it pulls
// in.  Set it to 0 to build the drumlogue units with the transform back on
// the audio thread: that is the first thing to try if Spectral misbehaves on
// hardware, and it restores exactly the code that shipped before this
// existed.
//
// The audio thread never blocks on the worker and never takes a lock to reach
// it.  If the thread cannot be created, or it falls behind, the transform
// runs inline exactly as it did before -- so the worst case of this change is
// the previous behaviour, not a stall.  See StartWorker(), BufferWorker() and
// the catch-up valve in Buffer().
//
// The deferral instrument and the worker are two implementations of the same
// handoff and the scheduler cannot serve both at once.
#if CLOUDS_PVOC_WORKER && CLOUDS_PVOC_DEFER_BLOCKS
#error "CLOUDS_PVOC_WORKER and CLOUDS_PVOC_DEFER_BLOCKS are mutually exclusive"
#endif


// The hop must be longer than one audio block, or there is no room to spread
// the channels into and a deferred window would be overwritten before it is
// read.  Init() divides the hop by the channel count to get the spacing, so
// this is also what keeps that spacing from rounding to zero.
STATIC_ASSERT(!CLOUDS_PVOC_ROUND_ROBIN ||
                  (kMaxFftSize / CLOUDS_PVOC_HOP_RATIO) > kMaxBlockSize,
              clouds_pvoc_hop_too_short_for_round_robin);

class PhaseVocoder {
 public:
#if CLOUDS_PVOC_WORKER
  PhaseVocoder()
      : worker_started_(false), worker_ok_(false), worker_stop_(false),
        job_state_(kJobIdle), job_channel_(0) { }
  // Joins the worker.  The units are shared objects the host dlopen()s, and a
  // thread still running in code that dlclose() has unmapped is a segfault
  // with no useful backtrace, so this is not optional tidiness.
  ~PhaseVocoder() { StopWorker(); }
#else
  PhaseVocoder() { }
  ~PhaseVocoder() { }
#endif

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

  // Return with no transform running and none scheduled.  Init() calls it
  // because it is about to reallocate the buffers a running transform holds
  // pointers into; the port calls it before teardown for the same reason.
  void Quiesce();

 private:
  // Run channel `i`'s transform now, on whatever thread is calling.  The one
  // place stft_[i].Buffer() is reached from, so the rule that only one
  // transform runs at a time has one place to hold.
  void RunTransform(int32_t i);

  // Copy the live Parameters into snapshot_[i], at the moment the scheduler
  // picks channel i.
  void Snapshot(int32_t i);

  // The scheduler's decision, separated from carrying it out: returns the
  // channel whose turn it is, or -1 for none this call.
  int32_t NextChannel();

#if CLOUDS_PVOC_WORKER
  // The handoff is one job slot, because only one transform may run at a time
  // anyway: both channels share the fft_/ifft_ scratch pair.  A single worker
  // consuming a single slot enforces that by construction, and preserves the
  // ascending channel order the shared ifft residue depends on.
  //
  // The audio thread's side of this must never block and never take a lock.
  // It posts with a release store and a sem_post -- both wait-free -- and
  // everything else it does here is a load or a compare-exchange.  A mutex
  // would invert priority against a SCHED_OTHER worker and could stall the
  // render for a scheduling quantum, which is the failure this whole change
  // exists to avoid.
  enum JobState {
    kJobIdle = 0,   // nothing outstanding; the audio thread may run inline
    kJobPosted,     // handed over, worker has not picked it up yet
    kJobRunning     // worker is inside the transform; hands off
  };

  bool StartWorker();
  void StopWorker();
  static void* WorkerEntry(void* self);
  void WorkerLoop();
  // The worker-side scheduler: the whole of Buffer() when a worker is running.
  void BufferWorker();

  pthread_t worker_;
  sem_t worker_wake_;
  // Not atomic<bool>: only ever written before the thread starts and after
  // it is joined, or read by the worker between waits.
  bool worker_started_;
  bool worker_ok_;                    // false => everything runs inline
  volatile bool worker_stop_;
  std::atomic<int> job_state_;
  int32_t job_channel_;               // written before job_state_ is released
#endif

#if CLOUDS_PVOC_DEFER_BLOCKS
  // Transforms picked but not yet run, with the call number they come due.
  // Capacity is one per channel per hop plus slack; the queue is drained by
  // call count, so it cannot grow past what the deferral depth allows.
  static const size_t kDeferQueueSize = 8;
  void CancelDeferred(int32_t i);
  int32_t defer_channel_[kDeferQueueSize];
  uint32_t defer_due_[kDeferQueueSize];
  size_t defer_head_;
  size_t defer_count_;
  uint32_t defer_clock_;
#endif

  FFT fft_;

  STFT stft_[2];
  FrameTransformation frame_transformation_[2];

  int32_t num_channels_;

  // Round-robin state; only meaningful with CLOUDS_PVOC_ROUND_ROBIN.
  int32_t buffer_channel_;   // whose turn is next
  size_t since_service_;     // Buffer() calls since the last transform
  size_t service_stride_;    // calls to leave between transforms in a hop

  // Picked by the scheduler but not yet transformed.  Without this the
  // scheduler would keep re-picking a channel whose turn has come but whose
  // transform has not run yet -- stft_[i].pending() only falls when the
  // transform completes.  Always false unless a transform is deferred or
  // handed to the worker.
  bool in_flight_[2];

  // The knobs as they stood when a transform was scheduled, so that running
  // it later -- on a worker, or deferred -- uses what the synchronous path
  // would have used, and so that a worker never reads the struct the audio
  // thread is writing.  See the fork note in stft.cc.  NULL until the first
  // Process(), matching STFT's own behaviour on a transform that arrives
  // before any parameters do.
  const Parameters* parameters_;
  Parameters snapshot_[2];
  bool has_snapshot_[2];

  DISALLOW_COPY_AND_ASSIGN(PhaseVocoder);
};

}  // namespace clouds

#endif  // CLOUDS_DSP_PVOC_PHASE_VOCODER_H_
