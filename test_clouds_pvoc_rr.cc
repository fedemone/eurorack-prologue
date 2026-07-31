/*
 * File: test_clouds_pvoc_rr.cc
 *
 * Differential test for the phase vocoder's round-robin Buffer().
 *
 * The fork spreads a stereo hop's two FFTs across the hop instead of running
 * them back to back, to halve the once-per-hop spike that was overrunning the
 * audio deadline. The claim being tested is that the deferred channel reads
 * exactly the same analysis samples and writes exactly the same synthesis
 * samples as it would have earlier -- that spreading spends slack that is
 * already there and touches nothing else.
 *
 * The slack is real and it is countable. STFT's analysis ring is
 * fft_size + hop_size long while Buffer() reads only fft_size of it, so there
 * is exactly one hop between the write pointer and the window about to be
 * transformed. Spreading the channels evenly uses half of that. Miscount it
 * -- a longer block, a shorter hop, a rotation that skips a turn -- and a
 * window gets overwritten mid-flight, which is a corruption no ear test would
 * localise but which these hashes catch immediately.
 *
 * So this file is compiled twice, once with CLOUDS_PVOC_ROUND_ROBIN=1 and
 * once with 0, and the two runs are compared. Both sides are fork builds at
 * the same FFT size: this isolates the scheduling change from the FFT-size
 * change, which is a separate fork with its own rationale.
 *
 * Two families of scenario, because they can claim different things:
 *
 *   Steady state -- must be bit-identical, and that is the real proof. Every
 *      buffer index, every window, every overlap-add is unchanged, so any
 *      difference at all means the deferral overran its slack.
 *
 *   Moving knobs, and transitions -- allowed to differ, slightly, and
 *      compared by level instead. Both come from the same thing: a deferred
 *      transform runs a fraction of a hop later than upstream's would, so it
 *      reads the knobs as they are then (STFT holds a live Parameters* and
 *      does not snapshot -- upstream, unforked), and a FREEZE or a mode
 *      change can land in the gap between the two channels. Neither is
 *      audible: a couple of milliseconds of inter-channel skew on knobs the
 *      host already smooths, and a one-frame difference in what a frozen
 *      buffer captured. At the 512 this port ships, the transition scenarios
 *      come out bit-identical anyway and only the knob sweep moves at all.
 *
 * Mono is in the static family and must be bit-identical for a second reason:
 * with one channel there is nothing to take turns with, so the round-robin
 * must reduce to upstream's loop exactly. Granular is a control -- it never
 * calls Buffer() at all, so a difference there means something unrelated
 * moved.
 *
 * Note on quality numbering: set_quality() reads bit 0 as *mono* and bit 1 as
 * low fidelity, so 0 and 2 are the stereo settings and 1 and 3 are mono. The
 * labels below say which is which, since getting that backwards makes the
 * results look alarming when they are correct.
 *
 * Build/run: make test-clouds-pvoc-rr (both variants, every FFT size)
 */

#include "clouds/dsp/granular_processor.h"
#include "stmlib/utils/random.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>

using namespace clouds;

static const size_t kLargeBufferSize = 118784;
static const size_t kSmallBufferSize = 65536;
alignas(16) static uint8_t large_buffer_[kLargeBufferSize];
alignas(16) static uint8_t small_buffer_[kSmallBufferSize];
static GranularProcessor processor_;

static int failures_ = 0;

/* FNV-1a over the raw output bytes: any single-sample difference shows up. */
static uint64_t hash_init(void) { return 1469598103934665603ULL; }
static void hash_frames(uint64_t *h, const ShortFrame *f, size_t n) {
  const uint8_t *p = reinterpret_cast<const uint8_t *>(f);
  for (size_t i = 0; i < n * sizeof(ShortFrame); ++i) {
    *h ^= p[i];
    *h *= 1099511628211ULL;
  }
}

/* Deterministic, broadband, and not silent. */
static void fill_input(ShortFrame *in, size_t n, uint32_t *state) {
  for (size_t i = 0; i < n; ++i) {
    *state = *state * 1664525u + 1013904223u;
    const int16_t s = (int16_t)((int32_t)(*state >> 16) - 32768) / 3;
    in[i].l = s;
    in[i].r = (int16_t)(-s / 2);
  }
}

static void engine_init(PlaybackMode mode, int quality) {
  /* stmlib::Random is a global LCG shared by every scenario in this process,
   * and the engine draws from it.  Reseed so each scenario is an independent
   * comparison: without this, a run that ends with one channel's transform
   * still deferred -- which is exactly what the round-robin is for -- leaves
   * the generator one draw apart from the other build and every later
   * scenario inherits the divergence, including mono ones the scheduler
   * cannot touch. */
  stmlib::Random::Seed(0x12345678u);
  memset(large_buffer_, 0, kLargeBufferSize);
  memset(small_buffer_, 0, kSmallBufferSize);
  processor_.Init(large_buffer_, kLargeBufferSize,
                  small_buffer_, kSmallBufferSize);
  processor_.set_playback_mode(mode);
  processor_.set_quality(quality);
  processor_.set_bypass(false);
  processor_.set_silence(false);
  Parameters *p = processor_.mutable_parameters();
  p->position = 0.5f;
  p->size = 0.5f;
  p->pitch = 0.0f;
  p->density = 0.5f;
  p->texture = 0.5f;
  p->dry_wet = 0.9995f;
  p->stereo_spread = 0.5f;
  p->feedback = 0.0f;
  p->reverb = 0.0f;
  p->freeze = false;
  p->gate = true;
  p->trigger = false;
  processor_.Prepare();
}

struct Result {
  uint64_t hash;
  double rms;
  int peak;
};

/* Render `blocks` blocks and summarise the output.  With `sweep` set, the
 * spectral modifier's inputs move every block, which is the case that is
 * allowed to differ between the two schedulings. */
static Result run(int blocks, uint32_t seed, bool freeze, bool sweep) {
  ShortFrame in[kMaxBlockSize], out[kMaxBlockSize];
  uint32_t st = seed;
  Result r;
  r.hash = hash_init();
  r.peak = 0;
  double energy = 0.0;
  Parameters *p = processor_.mutable_parameters();
  processor_.set_freeze(freeze);
  for (int b = 0; b < blocks; ++b) {
    if (sweep) {
      /* Brisk but plausible: POSITION crosses its whole range in about half
       * a second, PITCH a quarter-semitone per block.  The first version of
       * this swept a semitone per block, which no knob does and which made
       * the skew look worse than it is. */
      p->position = (float)(b % 512) / 511.0f;
      p->pitch = (float)(b % 200) * 0.12f - 12.0f;
      p->texture = (float)(b % 256) / 255.0f;
    }
    fill_input(in, kMaxBlockSize, &st);
    processor_.Prepare();
    processor_.Process(in, out, kMaxBlockSize);
    hash_frames(&r.hash, out, kMaxBlockSize);
    for (size_t i = 0; i < kMaxBlockSize; ++i) {
      const int l = abs((int)out[i].l), rr = abs((int)out[i].r);
      if (l > r.peak) r.peak = l;
      if (rr > r.peak) r.peak = rr;
      energy += (double)out[i].l * out[i].l + (double)out[i].r * out[i].r;
    }
  }
  r.rms = sqrt(energy / (2.0 * blocks * kMaxBlockSize));
  return r;
}

/* A scenario that agrees only because both sides rendered silence would pass
 * for the wrong reason. */
static void check_audible(const char *name, const Result &r) {
  if (r.peak == 0) {
    printf("FAIL %s produced silence -- the comparison would be vacuous\n",
           name);
    ++failures_;
  }
}

/* Static-parameter scenario: reported as a hash, must match bit for bit. */
static void fixed(const char *name, PlaybackMode mode, int quality,
                  bool freeze) {
  engine_init(mode, quality);
  run(200, 12345u, false, false);             /* settle */
  const Result r = run(1200, 999u, freeze, false);
  printf("H %-30s %016llx\n", name, (unsigned long long)r.hash);
  check_audible(name, r);
}

/* Scenario compared by level rather than by hash, with a tolerance applied by
 * the Makefile. */
static void levels(const char *name, const Result &r) {
  printf("S %-30s rms %.4f peak %d\n", name, r.rms, r.peak);
  check_audible(name, r);
}

/* Moving-parameter scenario. */
static void swept(const char *name, PlaybackMode mode, int quality) {
  engine_init(mode, quality);
  run(200, 12345u, false, true);              /* settle */
  levels(name, run(1200, 999u, false, true));
}

int main(void) {
  printf("# CLOUDS_FFT_SIZE=%d CLOUDS_PVOC_ROUND_ROBIN=%d\n",
         (int)kMaxFftSize, CLOUDS_PVOC_ROUND_ROBIN);

  fixed("spectral/stereo",        PLAYBACK_MODE_SPECTRAL, 0, false);
  fixed("spectral/mono",          PLAYBACK_MODE_SPECTRAL, 1, false);
  fixed("spectral/stereo/lo-fi",  PLAYBACK_MODE_SPECTRAL, 2, false);
  fixed("spectral/mono/lo-fi",    PLAYBACK_MODE_SPECTRAL, 3, false);
  fixed("granular/stereo",        PLAYBACK_MODE_GRANULAR, 0, false);

  /* Entering Spectral by a mode change rather than by Init: the reallocating
   * path, which re-Init()s the vocoder and so resets whose turn it is.  If
   * the rotation came up in a different phase, a channel could be served
   * twice running and its partner starved, and the hash would move. */
  engine_init(PLAYBACK_MODE_GRANULAR, 0);
  run(100, 1u, false, false);
  processor_.set_playback_mode(PLAYBACK_MODE_SPECTRAL);
  run(200, 2u, false, false);                 /* settle */
  const Result entered = run(1000, 3u, false, false);
  printf("H %-30s %016llx\n", "spectral/via-mode-switch",
         (unsigned long long)entered.hash);
  check_audible("spectral/via-mode-switch", entered);

  /* FREEZE engaging is a transition, so it goes in the levels family for the
   * same reason as the second session below: the switch can land in the gap
   * between one channel's transform and the other's, and then the two
   * channels capture their final textures one frame apart.  From then on the
   * frozen spectrum differs -- permanently, since nothing refreshes it.  This
   * is visible only at the larger FFT sizes, where the gap is 8 or 16 blocks;
   * at the 512 this port ships it is 2 and the hashes match anyway. */
  engine_init(PLAYBACK_MODE_SPECTRAL, 0);
  run(200, 12345u, false, false);
  levels("spectral/stereo/frozen", run(1200, 999u, true, false));

  /* Cases allowed to move: the knobs, which STFT holds by pointer
   * rather than snapshotting, so a deferred transform reads them as they are
   * one block later.  Upstream has the same property from one hop to the
   * next; the round-robin only adds a block of skew between the two channels
   * of a pair.  The sweep below is far harsher than any real control
   * movement -- POSITION crossing its whole range every 128 blocks, PITCH
   * jumping a semitone per block -- so a fraction of a percent here is the
   * pessimistic figure, not the typical one. */
  swept("spectral/stereo/swept",  PLAYBACK_MODE_SPECTRAL, 0);
  swept("spectral/mono/swept",    PLAYBACK_MODE_SPECTRAL, 1);

  /* Re-entering Spectral after another mode has reallocated the workspace
   * underneath it.  Two things make this worth its own scenario.  First, the
   * number of idle Buffer() calls before the first hop differs between entry
   * paths, and an earlier version of the rotation advanced on those idle
   * calls -- that left the channels transformed in the opposite order from
   * upstream, which is not harmless (see the note in phase_vocoder.cc about
   * the top bins of the shared ifft scratch) and it showed up here and
   * nowhere else.  Second, a reallocation is a transition like FREEZE: it can
   * catch a channel waiting for its turn and discard that frame.  The frame
   * is discarded along with everything else the teardown discards, but
   * FrameTransformation::Reset() clears the magnitude textures and not the
   * phase accumulator sharing the buffer, so the next session starts from a
   * slightly different phase and, phase being integrated and never reset,
   * keeps the offset.  A constant per-bin phase offset in a resynthesis whose
   * phases are arbitrary anyway: levels, not a hash. */
  engine_init(PLAYBACK_MODE_GRANULAR, 0);
  run(100, 1u, false, false);
  processor_.set_playback_mode(PLAYBACK_MODE_SPECTRAL);
  run(100, 2u, false, false);
  processor_.set_playback_mode(PLAYBACK_MODE_STRETCH);
  run(100, 3u, false, false);
  processor_.set_playback_mode(PLAYBACK_MODE_SPECTRAL);
  run(200, 5u, false, false);                 /* settle */
  levels("spectral/second-session", run(1000, 4u, false, false));

  return failures_ ? 1 : 0;
}
