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

#if CLOUDS_PVOC_WORKER

// How the scheduling actually played out, for the tests to report.  Nothing
// reads them to make a decision, and no shipping code reads them at all.
//
// Relaxed atomics rather than plain uint32_t, which is what these were.  The
// original note said a lost update would only misreport a total, and that is
// still the only consequence -- but "only misreports a total" describes a
// lost update, not undefined behaviour, and unsynchronised increments from
// two threads are the latter.  It also showed up as a TSan report the moment
// a test read a counter while the worker was still alive, which is exactly
// how a test that reports on threading should not fail.
//
// Relaxed keeps the cost where the old note assumed it was: one LDREX/STREX
// pair per transform, a few times a millisecond, ordered against nothing.
std::atomic<uint32_t> g_pvoc_worker_ran(0);     // transforms done by the worker
std::atomic<uint32_t> g_pvoc_worker_forced(0);  // ones the renderer had to take

// Bring the worker up.  Called from Init(), which is a mode change and not
// deadline work, so it is allowed to be slow; it happens once per process
// because worker_started_ latches.
//
// Everything here is about not inheriting the audio thread's scheduling.
// pthread_create defaults to PTHREAD_INHERIT_SCHED, and Init() can be reached
// from the render, so the default would hand the worker the audio thread's
// SCHED_FIFO priority -- a second thread at real-time priority competing with
// the renderer, which is worse than no worker at all.  PTHREAD_EXPLICIT_SCHED
// with SCHED_OTHER makes it what it needs to be: a thread that runs in the
// gaps, exactly like the idle loop upstream calls Buffer() from.
//
// If any of this fails, the return is false and the caller leaves worker_ok_
// clear, which makes every transform run inline -- the behaviour that shipped
// before this existed.  There is no half-configured state to fall into.
bool PhaseVocoder::StartWorker() {
  pthread_attr_t attr;
  if (pthread_attr_init(&attr) != 0) {
    return false;
  }
  bool ok = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED) == 0;
  ok = ok && pthread_attr_setschedpolicy(&attr, SCHED_OTHER) == 0;
  // The transform's working set is in the buffers the allocator handed out,
  // not on the stack, so this only has to cover the FFT's call depth.  Ask
  // for little: the drumlogue loads every unit into one process and the
  // default 8 MB of stack reservation per thread is not free there.
  ok = ok && pthread_attr_setstacksize(&attr, 128 * 1024) == 0;
  if (ok && sem_init(&worker_wake_, 0, 0) != 0) {
    ok = false;
  }
  if (ok) {
    worker_stop_.store(false, std::memory_order_relaxed);
    if (pthread_create(&worker_, &attr, &PhaseVocoder::WorkerEntry, this) != 0) {
      sem_destroy(&worker_wake_);
      ok = false;
    }
  }
  pthread_attr_destroy(&attr);
  return ok;
}

void PhaseVocoder::StopWorker() {
  if (!worker_ok_) {
    return;
  }
  worker_ok_ = false;
  worker_stop_.store(true, std::memory_order_relaxed);
  sem_post(&worker_wake_);
  pthread_join(worker_, NULL);
  sem_destroy(&worker_wake_);
  worker_started_ = false;
}

void* PhaseVocoder::WorkerEntry(void* self) {
  static_cast<PhaseVocoder*>(self)->WorkerLoop();
  return NULL;
}

void PhaseVocoder::WorkerLoop() {
  for (;;) {
    while (sem_wait(&worker_wake_) != 0) {
      // EINTR only; anything else would spin, so treat it as a wakeup and let
      // the state check below decide.
      break;
    }
    if (worker_stop_.load(std::memory_order_relaxed)) {
      return;
    }
    // A wakeup with nothing posted is normal: the audio thread may have taken
    // the job back (Quiesce) between posting and this wait returning, and
    // Quiesce() posts to break the wait.
    int expected = kJobPosted;
    if (!job_state_.compare_exchange_strong(expected, kJobRunning,
                                            std::memory_order_acquire,
                                            std::memory_order_relaxed)) {
      continue;
    }
    g_pvoc_worker_ran.fetch_add(1, std::memory_order_relaxed);
    RunTransform(job_channel_);
    // Release, so everything the transform wrote -- the synthesis ring, done_,
    // process_ptr_ -- is visible to the audio thread once it sees kJobIdle.
    job_state_.store(kJobIdle, std::memory_order_release);
  }
}

// The whole worker-side scheduler, in one place.
//
// It rests on one invariant, and the invariant is what makes it reviewable:
//
//     the audio thread executes a transform only while the job slot is idle.
//
// Both channels share the fft_/ifft_ scratch pair, so a transform here while
// the worker has one in progress is not a race on a counter, it is two FFTs
// writing the same array.  An earlier draft ran the transform inline whenever
// posting failed, which is precisely that case -- posting fails *because* the
// worker is busy -- and it produced exactly the corruption you would expect.
//
// The invariant also buys the ordering for free.  The slot going idle is a
// release by the worker and an acquire here, so once this thread sees idle it
// sees everything the transform wrote: the synthesis ring, done_,
// process_ptr_, and in_flight_.  While the slot is not idle, this thread
// reads none of them -- it returns.
//
// Consequence worth naming: when the worker is busy, this call does nothing
// at all, not even the catch-up valve.  That is correct rather than lazy.  A
// channel that has fallen behind is picked up on the next call where the slot
// is free, which is also the first call where touching it is safe.
void PhaseVocoder::BufferWorker() {
  if (job_state_.load(std::memory_order_acquire) != kJobIdle) {
    return;
  }

  // Slot idle => no transform is running => in_flight_ is false for both
  // channels (RunTransform clears it before the worker releases the slot) and
  // everything it wrote is visible.

  // Catch-up, while the slot is still free: a channel a full hop behind is
  // about to have its analysis window overwritten, so take the spike here
  // rather than lose the frame.  Running it inline is safe for exactly the
  // reason above -- nothing else is using the scratch.
  //
  // One transform per call, at most, and that bound is the point rather than
  // a detail.  Draining the whole backlog here -- both channels, however many
  // hops each -- is what the loop used to do, and it turns a starved worker
  // into something worse than no worker at all: measured on ARM with the
  // worker deliberately starved, the unbounded drain put Spectral's worst
  // block at 540% of deadline against 41% for the same build with the worker
  // switched off, because one call ran two whole transforms back to back.
  // Capped, the inline spike is one transform, which is exactly what the
  // audio thread carried before any of this existed.  A deeper backlog drains
  // over the following calls instead of in one.
  for (int32_t j = 0; j < num_channels_; ++j) {
    if (stft_[j].pending() > 1) {
      g_pvoc_worker_forced.fetch_add(1, std::memory_order_relaxed);
      Snapshot(j);
      RunTransform(j);
      return;                 // spike taken; schedule nothing else this call
    }
  }

  const int32_t i = NextChannel();
  if (i < 0) {
    return;
  }
  Snapshot(i);
  job_channel_ = i;
  in_flight_[i] = true;
  // Release: job_channel_ and snapshot_[i] must be written before the worker
  // can observe kJobPosted and act on them.
  job_state_.store(kJobPosted, std::memory_order_release);
  sem_post(&worker_wake_);

#if CLOUDS_PVOC_WORKER_SYNC
  // Test build only: wait for the transform to land before returning, which
  // makes the worker's timing deterministic and turns "the worker keeps up"
  // from an assumption into the thing under test.  Free-running, the output
  // depends on how far behind the worker happens to be, so it cannot be
  // compared against anything; pinned like this it must equal the synchronous
  // path exactly, and `make test-clouds-pvoc-worker` checks that it does.
  //
  // Never compiled into a shipping unit: the audio thread waiting on the
  // worker is the priority inversion the whole design is arranged to avoid.
  while (job_state_.load(std::memory_order_acquire) != kJobIdle) {
    sched_yield();
  }
#endif
}

#endif  // CLOUDS_PVOC_WORKER

// Return with no transform running and none scheduled.
//
// Init() reallocates every buffer a transform reads and writes, so a
// transform must not be running across it -- on the worker that means waiting
// for one that has already started, which is the whole reason this exists.
//
// Work that is merely *scheduled* is dropped rather than run.  That is not a
// shortcut, it is the behaviour this port already has: the round-robin can
// leave a channel waiting for its turn when a reallocation lands, and
// test_clouds_pvoc_rr.cc records the consequence -- FrameTransformation::
// Reset() clears the magnitude textures but not the phase accumulator sharing
// the buffer, so the next session starts from a slightly different phase and
// keeps it.  Levels, not a hash; the scenario is second-session and it is
// compared with a 3% rms tolerance.
//
// Running the queued transform instead was tried, on the theory that it
// matches the synchronous order more closely.  It does not: it draws from the
// global stmlib LCG at a point the synchronous path never would, which moved
// the fixed-parameter mono hashes -- scenarios that must be exact.  Dropping
// is both simpler and the established semantics.
void PhaseVocoder::Quiesce() {
#if CLOUDS_PVOC_WORKER
  if (worker_ok_) {
    // Take the job back if it has not started; otherwise wait it out.  This
    // is the one place the calling thread may block on the worker, and it is
    // safe to: Init() is a mode change or a buffer reset, which is already
    // the most expensive thing that happens in a render, and the wait is
    // bounded by one transform.  The alternative is reallocating buffers out
    // from under a running FFT.
    int expected = kJobPosted;
    if (!job_state_.compare_exchange_strong(expected, kJobIdle,
                                            std::memory_order_acquire,
                                            std::memory_order_relaxed)) {
      while (job_state_.load(std::memory_order_acquire) != kJobIdle) {
        sched_yield();
      }
    }
  }
#endif
#if CLOUDS_PVOC_DEFER_BLOCKS
  defer_head_ = 0;
  defer_count_ = 0;
#endif
  in_flight_[0] = in_flight_[1] = false;
}

void PhaseVocoder::Init(
    void** buffer,
    size_t* buffer_size,
    const float* large_window_lut,
    size_t largest_fft_size,
    int32_t num_channels,
    int32_t resolution,
    float sample_rate) {
  Quiesce();
  num_channels_ = num_channels;
  buffer_channel_ = 0;
  since_service_ = 0;
  in_flight_[0] = in_flight_[1] = false;
  parameters_ = NULL;
  has_snapshot_[0] = has_snapshot_[1] = false;
#if CLOUDS_PVOC_DEFER_BLOCKS
  defer_head_ = 0;
  defer_count_ = 0;
  defer_clock_ = 0;
#endif

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

#if CLOUDS_PVOC_WORKER
  if (!worker_started_) {
    worker_started_ = true;
    worker_ok_ = StartWorker();
  }
  if (worker_ok_) {
    // Post both channels as soon as their hops are ready, back to back.
    //
    // Spreading them existed only to keep two transforms out of one audio
    // block; with neither on the audio thread there is nothing to keep apart,
    // and the stride was costing real budget.  Both channels' hops complete
    // on the same call and each window then has one hop before Process()
    // overwrites it, so a stride of 4 left channel 1 with 4 of its 8 blocks
    // already spent before the worker even saw it.  `make
    // test-clouds-pvoc-defer` measures exactly that: half the transforms
    // start missing at a deferral of 5, the rest at 9.
    //
    // Ascending order within a hop still holds, which matters for the shared
    // ifft residue -- a stride of 1 delays channel 1 by one call, it does not
    // reorder the pair.
    service_stride_ = 1;
  }
#endif

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
  parameters_ = &parameters;
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

// Take the knobs as they stand now, to be used by the transform whenever it
// actually runs.  Called at the point the scheduler picks a channel, so the
// values are the ones the synchronous path would have transformed with.
void PhaseVocoder::Snapshot(int32_t i) {
  if (parameters_ != NULL) {
    snapshot_[i] = *parameters_;
    has_snapshot_[i] = true;
  } else {
    has_snapshot_[i] = false;
  }
}

// Every call site has already established stft_[i].pending() > 0 -- the
// scheduler in NextChannel(), or the catch-up valve -- so the snapshot path
// uses BufferReady(), which does not re-read ready_.  See the note in
// stft.cc: that read is the audio thread's to make, not the worker's.
void PhaseVocoder::RunTransform(int32_t i) {
  if (has_snapshot_[i]) {
    stft_[i].BufferReady(snapshot_[i]);
  } else {
    stft_[i].Buffer();
  }
  in_flight_[i] = false;
}

#if CLOUDS_PVOC_DEFER_BLOCKS
// How the deferral actually played out, for the test to report.  A transform
// that the catch-up valve had to claw back ran on the audio thread after all,
// which is the outcome the worker has to avoid to be worth anything.
uint32_t g_pvoc_defer_ontime = 0;
uint32_t g_pvoc_defer_forced = 0;
uint32_t g_pvoc_defer_max_lag = 0;

// Drop channel `i`'s queued transform, if it has one, so the caller can run it
// itself.  At most one entry can match: in_flight_ stops a second being
// queued while the first is outstanding.
void PhaseVocoder::CancelDeferred(int32_t i) {
  size_t kept = 0;
  int32_t channel[kDeferQueueSize];
  uint32_t due[kDeferQueueSize];
  for (size_t n = 0; n < defer_count_; ++n) {
    const size_t s = (defer_head_ + n) % kDeferQueueSize;
    if (defer_channel_[s] == i) {
      ++g_pvoc_defer_forced;
      continue;
    }
    channel[kept] = defer_channel_[s];
    due[kept] = defer_due_[s];
    ++kept;
  }
  for (size_t n = 0; n < kept; ++n) {
    defer_channel_[n] = channel[n];
    defer_due_[n] = due[n];
  }
  defer_head_ = 0;
  defer_count_ = kept;
}
#endif  // CLOUDS_PVOC_DEFER_BLOCKS

int32_t PhaseVocoder::NextChannel() {
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
  //
  // A channel already picked but not yet transformed is skipped rather than
  // picked again: stft_[i].pending() only falls when the transform completes,
  // so without in_flight_ a deferred channel would be handed out on every
  // call until it ran.
  int32_t chosen = -1;
  for (int32_t n = 0; n < num_channels_; ++n) {
    const int32_t i = buffer_channel_ + n < num_channels_
        ? buffer_channel_ + n
        : buffer_channel_ + n - num_channels_;
    if (!stft_[i].pending() || in_flight_[i]) {
      continue;
    }
    if (i != 0 && since_service_ < service_stride_) {
      break;      // ready, but not yet far enough from the last transform
    }
    chosen = i;
    buffer_channel_ = i + 1 < num_channels_ ? i + 1 : 0;
    since_service_ = 0;
    break;
  }
  ++since_service_;
  return chosen;
#else
  return -1;
#endif  // CLOUDS_PVOC_ROUND_ROBIN
}

void PhaseVocoder::Buffer() {
#if CLOUDS_PVOC_WORKER
  if (worker_ok_) {
    BufferWorker();
    return;
  }
#endif
#if CLOUDS_PVOC_ROUND_ROBIN
#if CLOUDS_PVOC_DEFER_BLOCKS
  // Run whatever has come due.  Ascending call order is preserved because the
  // queue is a FIFO and every entry gets the same delay, which matters for
  // the same reason the round-robin advances in ascending channel order: the
  // top bins of the shared ifft scratch carry over from the last transform.
  ++defer_clock_;
  while (defer_count_ &&
         (int32_t)(defer_clock_ - defer_due_[defer_head_]) >= 0) {
    const int32_t i = defer_channel_[defer_head_];
    const uint32_t lag =
        defer_clock_ - (defer_due_[defer_head_] - CLOUDS_PVOC_DEFER_BLOCKS);
    if (lag > g_pvoc_defer_max_lag) {
      g_pvoc_defer_max_lag = lag;
    }
    ++g_pvoc_defer_ontime;
    defer_head_ = (defer_head_ + 1) % kDeferQueueSize;
    --defer_count_;
    RunTransform(i);
  }
#endif

  const int32_t i = NextChannel();
  if (i >= 0) {
    Snapshot(i);
    // The channels share the fft_/ifft_ scratch pair, so one channel must run
    // to completion before another starts.  It does -- transforms are handed
    // out one at a time and run one at a time, never overlapped.
#if CLOUDS_PVOC_DEFER_BLOCKS
    if (defer_count_ < kDeferQueueSize) {
      defer_channel_[(defer_head_ + defer_count_) % kDeferQueueSize] = i;
      defer_due_[(defer_head_ + defer_count_) % kDeferQueueSize] =
          defer_clock_ + CLOUDS_PVOC_DEFER_BLOCKS;
      ++defer_count_;
      in_flight_[i] = true;
    } else {
      RunTransform(i);        // queue full: the instrument gives up, not the audio
    }
#else
    RunTransform(i);
#endif
  }

  // Deferring assumes one call is enough to catch up, i.e. that a channel is
  // never more than one hop behind when its turn comes.  That holds for the
  // block sizes this port uses, and the static assert in the header pins it.
  // If it is ever false anyway -- Process() handed more than kMaxBlockSize,
  // say -- the deferred frame's analysis window is about to be overwritten,
  // so take upstream's spike rather than the corruption.
  for (int32_t j = 0; j < num_channels_; ++j) {
    while (stft_[j].pending() > 1) {
      if (!in_flight_[j]) {
        // Never scheduled, so there is no snapshot to honour and the live
        // knobs are what the unforked path would have used.  Without this the
        // transform would run against whatever this channel was last
        // scheduled with, which could be many hops old.
        Snapshot(j);
      }
#if CLOUDS_PVOC_DEFER_BLOCKS
      CancelDeferred(j);
#endif
      RunTransform(j);
    }
  }
#else
  for (int32_t j = 0; j < num_channels_; ++j) {
    stft_[j].Buffer();
  }
#endif  // CLOUDS_PVOC_ROUND_ROBIN
}

}  // namespace clouds
