/*
 * Rings resonator port for drumlogue
 *
 * Port of Mutable Instruments Rings (physical modelling resonator)
 * to the drumlogue User Oscillator API.
 *
 * Rings runs at 48 kHz natively — matching drumlogue's sample rate exactly,
 * so no sample-rate conversion is needed (unlike Elements at 24 kHz).
 *
 * Output: Rings produces stereo (out + aux). We interleave them into
 * the Q31 output buffer as L/R pairs (same pattern as Elements/modal-strike).
 *
 * Models:
 *   0 = Modal resonator (struck object)
 *   1 = Sympathetic strings
 *   2 = Inharmonic string (with dispersion)
 *   3 = FM voice
 *   4 = Sympathetic strings (quantized chords)
 *   5 = String + reverb
 *
 * Modulation (drumlogue only):
 *   Two LFOs, one destination each, in the same layout the Plaits and
 *   Elements ports use.  Besides the four continuous knobs they can reach
 *   Note (transposes the sounding pitch, including a running arpeggio) and
 *   Chord (steps along the chord list, so a slow LFO is a chord sequencer).
 *
 * Arpeggiator (drumlogue only):
 *   Rings is monophonic here (one held note), so the built-in arpeggiator
 *   builds a note sequence from either the selected Chord's tones or plain
 *   octaves of the root, then re-strums the resonator step-by-step in one of
 *   eight patterns, tempo-synced to the drumlogue's clock.  Host tempo is
 *   delivered by the drumlogue adapter through a reserved OSC_PARAM index
 *   (see kTempoParamIndex); on platforms that never send it the arp falls
 *   back to 120 BPM.
 */

#include "userosc.h"
#include "stmlib/dsp/dsp.h"

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

#include "stmlib/utils/random.h"
#include "rings/dsp/part.h"
#include "rings/resources.h"

using namespace rings;

/* --- Static allocations --- */

static Part part_;
static uint16_t reverb_buffer_[32768];

static rings::Patch patch_;
static rings::PerformanceState performance_state_;

alignas(16) static float in_buffer_[kMaxBlockSize];
alignas(16) static float out_buffer_[kMaxBlockSize];
alignas(16) static float aux_buffer_[kMaxBlockSize];

static bool gate_ = false;

/* ======================================================================
 * Note changes wait for the next strike
 *
 * Part reads its pitch the way the module reads its V/Oct jack: the voice
 * struck last follows it continuously, and only a strike moves the next note
 * onto a voice of its own.  So a pitch change that came without a strike went
 * straight into the note still ringing.  Measured on the String model, at
 * Polyphony 1 and 2 alike, the ringing pitch fell 44-49 dB within one 80 ms
 * window and the new pitch took its place, with nothing struck:
 *
 *   - Base Note turned, or motion-sequenced, between steps.  The wrapper
 *     re-pitches a gate-driven note as soon as Base Note moves, so that a
 *     sustained oscillator follows the knob;
 *   - a note-on while the gate was still up (MIDI legato, overlapping gates),
 *     which retuned without striking, since a strike was the gate's rising
 *     edge;
 *   - an arpeggiator rest step, which handed back the root.
 *
 * Now the pitch is held from one strike to the next: a change arriving
 * between strikes waits for the next one, and every note-on strikes.  At
 * Polyphony 2-4 that strike puts the new note on the next voice, so what is
 * ringing rings on at its own pitch.  At Polyphony 1 there is one resonator,
 * and the strike that brings a new note retunes it, as on the module.
 *
 * Two kinds of change still reach a ringing note.  One arriving within
 * kStrikeGrace after a strike is taken as that strike's pitch: a host may
 * deliver a step's note just after its gate rather than before.  And small
 * continuous moves -- pitch bend, a host's glide -- pass through, while a
 * jump of kNoteJump or more in one block waits for the strike; 0.4 semitone
 * is what Rings' own NoteFilter treats as a new note.  LFO -> Note is
 * applied after all of this and modulates every note as before.
 * ==================================================================== */
static const uint32_t kStrikeGrace = 480;     /* 10 ms at 48 kHz */
static const float    kNoteJump    = 0.4f;    /* semitones per block */
static volatile uint32_t note_on_count_ = 0;  /* incremented by OSC_NOTEON */
static uint32_t note_on_seen_   = 0;          /* note-ons already struck */
static float    held_note_      = 60.0f;      /* before LFO -> Note */
static float    host_note_prev_ = 60.0f;
static uint32_t since_strike_   = kStrikeGrace; /* samples; stops at the grace */
static bool     strike_held_    = false;      /* see change_pending in OSC_CYCLE */
static float    strike_held_note_ = 60.0f;

/* User-facing parameter storage */
uint16_t p_values[k_num_user_osc_param_id] = {0};
float shape = 0, shiftshape = 0;
float shape_lfo = 0;

/* ======================================================================
 * Modulation
 *
 * Two LFOs, laid out the way the Plaits and Elements ports do it so the
 * panel behaves the same across the family: LFO1 arrives from outside (the
 * host's shape LFO on prologue, the wrapper's own oscillator on drumlogue,
 * which is why it has a Rate but no Depth) and LFO2 is generated here and
 * has both.  Each picks one destination.
 *
 * Two of the destinations are particular to this engine.  `Note` transposes
 * the sounding pitch, and is applied after the arpeggiator so that a running
 * pattern is transposed as a whole rather than having its steps rewritten
 * underneath it.  `Chord` is the odd one: it is not a continuous quantity but
 * an index into a table of string tunings, so the modulation is rounded and
 * the LFO walks the chord list rather than sliding through it -- which turns
 * a slow LFO into a chord sequencer and a fast one into something closer to a
 * broken arpeggio.  It is clamped, not wrapped, so Chord itself sets where in
 * the list the sweep sits; put it mid-list for a symmetric sweep.
 *
 * The clamp is not politeness.  performance_state.chord indexes the chord
 * table with no bounds check between here and the subscript, so an unclamped
 * modulated value would be a wild read on the audio thread -- the same shape
 * of bug as the Polyphony race above, arrived at from a different direction.
 * ==================================================================== */

enum LfoTarget {
  LfoTargetPosition,
  LfoTargetStructure,
  LfoTargetBrightness,
  LfoTargetDamping,
  LfoTargetNote,
  LfoTargetChord,
  LfoTargetLfo2Frequency,
  LfoTargetLfo2Depth
};

/* Semitones of pitch modulation at full LFO swing.  A whole tone is vibrato,
 * 12 is an octave sweep, and the useful settings are far enough apart that a
 * fixed value could only suit one of them; the panel owns this.  Defaults to
 * 2, which is what it was fixed at before the parameter existed.  Range is
 * checked in OSC_PARAM rather than trusted, because it scales a value that
 * ends up as a lookup-table index. */
#define kLfoNoteSemitonesMax 24
static uint16_t lfo_note_semitones_ = 2;

/* LFO1's depth, 0-100.  On prologue there is no such parameter -- LFO1 is the
 * host's shape LFO and arrives at whatever the panel sends -- so the default
 * is full scale and that platform simply never writes it. */
static uint16_t lfo1_depth_value = 100;

static float lfo2 = 0.0f;
static float lfo2_phase = 0.0f;
static uint16_t lfo1_shape_value = 0;
static uint16_t lfo2_shape_value = 0;
static uint16_t lfo2_target_ = 0;

/* Square, as a transfer curve: full scale with the sign of its input.  LFO1
 * arrives as a cosine, so this is a 50% square at its rate and phase, lined
 * up with LFO2's Square below.  Zero stays zero, so Rate 0 still means no
 * modulation.  Same definition as macro-oscillator2.cc. */
static inline float lfo_square(float x) {
  return x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f);
}

/* Transfer curve for LFO1.  Same six shapes as the other ports. */
static inline float apply_lfo1_shape(float x) {
  switch (lfo1_shape_value) {
    default:
    case 0: /* Cosine: pass-through */
      return x;
    case 1: /* Triangle: S-curve */
      { float ax = x < 0.f ? -x : x;
        float s = ax * (2.0f - ax);
        return x < 0.f ? -s : s; }
    case 2: /* Ramp Up: quadratic */
      return x < 0.f ? -(x * x) : (x * x);
    case 3: /* Ramp Down: inverse quadratic */
      { if (x > 0.f) { float s = 1.0f - x; return 1.0f - s * s; }
        if (x < 0.f) { float s = 1.0f + x; return -(1.0f - s * s); }
        return 0.f; }
    case 4: /* Fat Sine: soft-clip */
      return clip1m1f(x * (1.5f - 0.5f * x * x));
    case 5: /* Square */
      return lfo_square(x);
  }
}

/* A target value out of range simply matches nothing, so an unknown selector
 * is silently no modulation rather than an out-of-bounds anything. */
static inline float get_lfo_value(enum LfoTarget target) {
  return (p_values[k_user_osc_param_id4] == (uint16_t)target ? shape_lfo : 0.0f) +
         (lfo2_target_ == (uint16_t)target ? lfo2 : 0.0f);
}

/* Model and Polyphony are latched here and applied on the audio thread.
 *
 * OSC_PARAM runs on the drumlogue's control thread, and Part::set_polyphony()
 * / Part::set_model() re-seat state that Part::Process() is reading on the
 * audio thread.  Polyphony is the dangerous one, because Process() reads it
 * twice and derives an array index from the second read:
 *
 *     for (voice = 0; voice < polyphony_; ++voice)      // part.cc
 *         ...
 *         num_strings = 2 * kMaxPolyphony / polyphony_; // RenderStringVoice
 *         i = voice + string * polyphony_;
 *         String& s = string_[i];                       // string_[8]
 *
 * Those agree only as long as polyphony_ holds still.  Lower it between the
 * loop bound and the divide — one parameter push while a note is sounding —
 * and `i` runs past the end of string_[]: at polyphony_ 4 -> 1, voice 3 with
 * num_strings 8 indexes string_[10] and *writes* to it.  That is a wild write
 * on the audio thread, into an address space shared with every other loaded
 * unit, and it is what took the audio engine down when Rings was selected.
 *
 * Latching the request and applying it at the top of OSC_CYCLE, before
 * Part::Process() is entered, means the pair can only change between blocks.
 * This is the same treatment clouds-granular.cc gives Mode and Quality, and
 * for the same reason. */
static volatile int32_t pending_model_     = RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED;
static volatile int32_t pending_polyphony_ = 1;

/* What the Part is currently configured for; audio thread only. */
static uint16_t model_value = RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED;
static uint16_t polyphony_value = 1;
/* Whether a chord is selected, as applied to the Part.  Chord Off is a
 * negative Chord, which arrives here as a uint16 two's complement (the
 * wrapper's k_route_osc passes the panel value through as it is).  Turning
 * a chord on or off can change how many voices the Part renders -- see
 * "Struck chords" in eurorack-opt/rings/dsp/part.cc -- so it is latched and
 * bridged like Model and Polyphony.  Which chord, once one is on, is not: it
 * only retunes, and goes straight through. */
static bool chord_on_value = false;

static inline int32_t chord_param(void) {
  return (int32_t)(int16_t)p_values[k_user_osc_param_id3];
}

/* The last chord the panel selected.  While a bridge fades out a chord that
 * has just been turned off, the Part still plays one -- and it has to be
 * that one, not whatever -1 clamps to: clamped, every struck voice jumped to
 * Oct at the start of the fade, a click of 7.8 times the programme on
 * Modal. */
static int32_t chord_last_ = 0;

/* ======================================================================
 * Model / Polyphony / Chord transitions
 *
 * Changing Model or Polyphony used to stop the sound dead: Part
 * re-initialised every string, mode set or FM voice and the new
 * configuration started from silence.  The forked Part
 * (eurorack-opt/rings/dsp/part.cc) now keeps what is already ringing
 * whenever it can -- any change among the four string models, and polyphony
 * changes for the string and FM models -- and says so through
 * Part::KeepsStateFor().  Turning Chord on or off is the same kind of change
 * (see "Struck chords" in part.cc), and goes through the same path.
 *
 * That alone is not seamless.  Even where the strings carry on, what is heard
 * of them changes at once: the models mix and scale their output differently,
 * a polyphony change regroups the strings into voices, and a change can stop
 * strings that were ringing -- leaving Sympathetic String for String drops
 * the seven sympathetic strings, and lowering Polyphony on String or FM drops
 * the voices above the new count, which may be the ones that are sounding.
 * And a change Part cannot carry across -- between the modal, string and FM
 * engines, or of modal polyphony -- starts from silence.
 *
 * So every change made while something is sounding is bridged here, using
 * the output's last 85 ms, which are kept in a history buffer:
 *
 *   1. Freeze that history and crossfade the old configuration into it over
 *      2 ms.  The change waits for this, so it lands 2 ms late.
 *   2. Apply the change.
 *   3. Hand back to the new configuration over 24 ms, with the frozen tail
 *      making up whatever the new configuration does not carry on.  Where
 *      Part kept everything, that is nothing and this is a crossfade.  Where
 *      it kept part of the sound or none of it, the tail stays for the part
 *      that was lost and dies away under the new configuration (faster at
 *      low Damping, slower at high).
 *
 * Step 3 works from levels rather than from what Part says it kept, because
 * Part can only say what it kept, not how much of what was audible that was.
 * The live signal and the tail are each measured over about 10 ms, and the
 * tail's weight is whatever brings the sum back to the tail's level, on an
 * equal-power basis since the two are not in phase:
 *
 *       tail weight = sqrt(1 - (live weight)^2 * P_live / P_tail)
 *
 * which is the plain equal-power crossfade when the new configuration is as
 * loud as the old, and leaves the tail alone when it is silent.  When the new
 * configuration is louder -- String + Reverb to Sympathetic String is 8 dB --
 * it is brought in at the old level and let rise to its own over about a
 * third of a second, rather than stepping up; a strike ends that at once.
 *
 * The frozen history is played by two grain heads half a grain apart, with
 * triangular windows that sum to one.  Each grain starts where its next few
 * milliseconds best match what the other head is about to play, searched
 * over a random window of the history, so the crossover between the two
 * heads is in phase -- a sustained tone continues as a tone, without the
 * dips of up to 10 dB that random starts put in it every 20 ms.  The first
 * grain is placed the same way against the last samples before the freeze,
 * so step 1 continues the waveform rather than crossfading into a copy of it
 * at an unrelated phase.  Candidates much louder than the material they are
 * matched against score lower, so a strike's attack caught in the history is
 * not replayed as a phantom pluck.
 *
 * Changes in quick succession -- the Model knob swept through its range --
 * are covered too: a dying tail keeps dying under later changes, and a change
 * arriving during step 3 turns the crossfade round where it stands.
 *
 * Cost: a tail that is playing costs a grain search every 1024 samples, all
 * in one render (about 6k multiply-adds), plus about forty instructions a
 * sample; keeping the history costs about a hundred per render.  The search
 * is sized by that one render
 * rather than by the average: at 1024 candidates every 4th, matched over all
 * 128 samples, it put the 99th-percentile render under QEMU at the deadline
 * while the Model knob was being turned, against 25-70% without it.  Every
 * 16th of 1024, refined to the sample around the best, matched on every
 * fourth sample -- still six taps a period at 2 kHz -- is a tenth of that
 * work, put the 99th percentile back within a few points of where it was
 * without any bridge, and measured no worse on the transitions themselves.
 * (A narrower window, 768, was worse on a struck chord by 3-5 dB of dip.)
 * When nothing is sounding, a change is applied at once.
 * ==================================================================== */

#define kTailHistory 4096          /* 85 ms at 48 kHz; power of two */
#define kTailGrain   2048          /* grain length; heads run half a grain apart */
#define kTailHalf    (kTailGrain / 2)
#define kTailXfadeBlocks 4         /* step 1: 4 x 24 samples = 2 ms */
#define kTailHandbackBlocks 48     /* step 3: 24 ms */
#define kTailMatch   128           /* span compared when placing a grain... */
#define kTailMatchStride 4         /* ...every fourth sample of it */
#define kTailSearch  1024          /* span of candidate starts: periods to 47 Hz */
#define kTailSearchStep 16         /* coarse step, refined to 1 around the best */

static_assert(kTailHistory - kTailGrain >= kTailSearch,
              "the grain search window has to fit the history");

/* The output is recorded continuously into tail_hist_[0].  A freeze copies
 * it, oldest sample first, into one of the other two, which the bridge and
 * the decay tail below play from.  Copying rather than switching which
 * buffer records is what keeps a full 85 ms of history behind every freeze,
 * however soon after the last one it comes. */
alignas(16) static float tail_hist_[3][kTailHistory];

struct TailPlayer {
  const float* hist;    /* frozen history, oldest sample first; NULL when idle */
  uint32_t pos[2];      /* position within each head's grain */
  uint32_t start[2];    /* each grain's start in the history */
  float gain;
  float decay;          /* per sample */
};

enum BridgeMode {
  BRIDGE_OFF,
  BRIDGE_IN,            /* step 1: what is playing -> frozen history */
  BRIDGE_READY,         /* step 1 done: apply the change */
  BRIDGE_OUT            /* step 3: frozen history -> what is playing */
};

/* The bridge's place in its crossfade, 0 (not heard) to 1 (all that is
 * heard), moved per sample.  Kept as a position rather than a count of blocks
 * so that a change arriving while the bridge is still handing back -- a knob
 * turned through several models in quick succession -- simply turns it round
 * from where it is: no new freeze, nothing dropped from the output. */
static float bridge_x_ = 0.0f;

/* The bridge is frozen at each change and covers it (steps 1 and 3).  The
 * decay tail is what a change leaves behind that the new configuration did
 * not carry on; it dies away under everything after it, which is why it is a
 * separate player from the bridge. */
static TailPlayer tail_bridge_;
static TailPlayer tail_decay_;
static BridgeMode bridge_mode_  = BRIDGE_OFF;
static uint32_t tail_write_    = 0;     /* write position in tail_hist_[0] */
static uint32_t tail_rng_      = 0x2545F491u;
static float    out_level_     = 0.0f;  /* recent output peak: is anything sounding? */

/* The gains applied at the end of the last block, to the live signal (the
 * crossfade alone) and to the bridge.  Every block ramps from these to its
 * own targets, so a change of plan -- a turn-round, the end of a step --
 * can never put a step in either gain. */
static float mix_live_   = 1.0f;
static float mix_bridge_ = 0.0f;

/* Step 3's level measurements: smoothed power per sample, about 10 ms. */
static float pow_live_   = 0.0f;
static float pow_bridge_ = 0.0f;
static const float kPowSmooth = 0.05f;   /* per 24-sample block */
/* Live power, as a fraction of the tail's, from which the live signal is
 * taken to carry the whole sound.  Two estimates 10 ms long of two signals
 * that beat are never equal, and without this a configuration that carried
 * everything on still left the tail behind at -11 dB, a frozen ghost of the
 * note under the note itself. */
static const float kCarriedAll = 0.7f;
/* The tail weight above which the live signal is taken to carry none of the
 * sound, and the tail is left to die away at the rate its damping sets.
 * Below it the live signal carries part of the note, and a tail left
 * alongside it beats against it: each new grain lands at a different phase
 * to the same partials in the live strings, which measured as a tremolo of
 * 10 dB on a Polyphony 3 -> 1 change in Sympathetic String.  There it fades
 * over kTailPartialTau instead.  That is a compromise, measured both ways:
 * at 30 ms the beating was gone but the partial changes fell by up to
 * 8.5 dB, as they did before any of this; at 150 ms the worst fall is 6 dB
 * and the worst dip 10, against 34 and 148 dB before. */
static const float kTailWhole = 0.9f;
static const float kTailPartialTau = 0.15f;   /* seconds */

/* Level match: the live signal's attenuation, and how fast it relaxes back
 * to unity -- about 100 ms, or 5 ms once a strike has been played. */
static float live_gain_  = 1.0f;
/* A strike during step 3.  A new note is not the old sound carrying on, so
 * from then on the live level stops being measured (it would read as the
 * old sound having been kept, and duck the tail under the strike), the live
 * signal comes in at full weight, and the level match lets go. */
static bool  strike_seen_ = false;
static const float kLiveRelax       = 0.0050f;  /* per block: tau 100 ms */
/* No more than 12 dB of it.  The largest level difference between two
 * configurations is 8 dB; past that the tail is not the old sound but the
 * last of it, measured against a new configuration that is simply louder,
 * and matching it would fade the new one in from nearly nothing. */
static const float kLiveMatchMin    = 0.25f;
static const float kLiveRelaxStrike = 0.0952f;  /* per block: tau 5 ms */

static inline uint32_t tail_rand(uint32_t n) {
  tail_rng_ = tail_rng_ * 1664525u + 1013904223u;
  return (tail_rng_ >> 8) % n;
}

/* How well the kTailMatch samples at a stand in for those at b, whose energy
 * is eb.  Correlation over the larger of the two energies rather than over
 * their geometric mean: 1 for an identical segment, and lower for a segment
 * at the right phase but the wrong level, by the level ratio, in either
 * direction.  That is what keeps an attack out -- seen from the decaying
 * material after it, an attack is the same waveform, louder. */
static inline float tail_match(const float* a, const float* b, float eb) {
  float ab = 0.0f, aa = 0.0f;
  for (int i = 0; i < kTailMatch; i += kTailMatchStride) {
    ab += a[i] * b[i];
    aa += a[i] * a[i];
  }
  const float norm = aa > eb ? aa : eb;
  return ab / (norm + 1.0e-20f);
}

/* The s in [lo, hi] whose next kTailMatch samples best match those at t:
 * every kTailSearchStep-th candidate, then every one around the best. */
static uint32_t tail_search(const float* h, const float* t, int lo, int hi) {
  float eb = 0.0f;
  for (int i = 0; i < kTailMatch; i += kTailMatchStride) eb += t[i] * t[i];
  int best = lo;
  float best_score = -1.0e30f;
  for (int s = lo; s <= hi; s += kTailSearchStep) {
    const float score = tail_match(h + s, t, eb);
    if (score > best_score) { best_score = score; best = s; }
  }
  const int f_lo = best - (kTailSearchStep - 1) < lo ? lo : best - (kTailSearchStep - 1);
  const int f_hi = best + (kTailSearchStep - 1) > hi ? hi : best + (kTailSearchStep - 1);
  for (int s = f_lo; s <= f_hi; ++s) {
    const float score = tail_match(h + s, t, eb);
    if (score > best_score) { best_score = score; best = s; }
  }
  return (uint32_t)best;
}

/* Where head k's next grain starts: matched against what the other head --
 * half a grain in, at the top of its window -- plays next, so the two are in
 * phase while one fades out and the other in.  The search window is placed
 * at random, so that successive grains do not keep returning to one spot. */
static uint32_t tail_place_grain(const TailPlayer& t, int k, int ahead) {
  const int j = 1 - k;
  const float* next = t.hist + t.start[j] + t.pos[j] + ahead;
  const int lo = (int)tail_rand(kTailHistory - kTailGrain - kTailSearch + 1);
  return tail_search(t.hist, next, lo, lo + kTailSearch);
}

static void tail_reset(void) {
  std::fill(&tail_hist_[0][0], &tail_hist_[0][0] + 3 * kTailHistory, 0.0f);
  tail_bridge_.hist = NULL;
  tail_decay_.hist  = NULL;
  bridge_mode_   = BRIDGE_OFF;
  bridge_x_      = 0.0f;
  tail_write_    = 0;
  out_level_     = 0.0f;
  mix_live_      = 1.0f;
  mix_bridge_    = 0.0f;
  pow_live_      = 0.0f;
  pow_bridge_    = 0.0f;
  live_gain_     = 1.0f;
  strike_seen_   = false;
}

/* Step 1: freeze the history into the bridge and start crossfading into it. */
static void tail_freeze(void) {
  TailPlayer& t = tail_bridge_;
  float* frozen = tail_hist_[tail_decay_.hist == tail_hist_[1] ? 2 : 1];
  const float* live = tail_hist_[0];
  std::copy(live + tail_write_, live + kTailHistory, frozen);
  std::copy(live, live + tail_write_, frozen + (kTailHistory - tail_write_));
  t.hist = frozen;

  /* Head 1 is at the top of its window and is what the crossfade lands on,
   * so it starts at the point in the history whose preceding samples best
   * match the last ones recorded: playing on from there continues the
   * waveform.  It has half a grain left to play, which bounds how recent
   * that point can be. */
  const int c_hi = kTailHistory - kTailHalf - kTailMatch;
  const uint32_t c = tail_search(frozen, frozen + kTailHistory - kTailMatch,
                                 c_hi - kTailSearch, c_hi) + kTailMatch;
  t.start[1] = c - kTailHalf;
  t.pos[1]   = kTailHalf;
  t.start[0] = 0;
  t.pos[0]   = 0;
  t.start[0] = tail_place_grain(t, 0, 0);
  t.gain   = 1.0f;
  /* tau = 0.1 s at Damping 0 to 0.4 s at 100%, i.e. -60 dB in 0.7 to 2.8 s.
   * Applied from step 2 on. */
  const float tau = 0.1f + 0.3f * patch_.damping;
  t.decay  = 1.0f - 1.0f / (tau * 48000.0f);
  bridge_mode_   = BRIDGE_IN;
  bridge_x_      = 0.0f;
}

/* Step 2 has just been applied.  The bridge now stands for everything that
 * was sounding, an older decay tail included -- that is in its history, and
 * has been faded out of the output by step 1 -- so the older tail goes.
 *
 * `kept` is Part's own account of the change, and is used only if a strike
 * comes before step 3 has measured anything: kept means the live signal is
 * taken to carry the old sound on, otherwise to carry none of it. */
static bool tail_kept_ = false;
static void tail_after_change(bool kept) {
  tail_decay_.hist = NULL;
  bridge_mode_ = BRIDGE_OUT;
  pow_live_    = -1.0f;   /* first block of step 3 seeds the measurements */
  pow_bridge_  = -1.0f;
  strike_seen_ = false;
  tail_kept_   = kept;
}

/* Crossfade curve: smoothstep, so the weight starts and ends with zero slope.
 * A linear ramp leaves a corner in the waveform at each end of the fade --
 * no step, but a kink, and on a smooth modal tone a visible one.  Step 1
 * uses it as an amplitude crossfade, which is right because the frozen tail
 * is placed in phase with what it replaces. */
static inline float tail_curve(float x) {
  return x * x * (3.0f - 2.0f * x);
}

static inline float tail_sample(TailPlayer& t) {
  float s = 0.0f;
  for (int k = 0; k < 2; ++k) {
    uint32_t p = t.pos[k];
    const float w = (p < kTailHalf ? (float)p : (float)(kTailGrain - p)) *
                    (1.0f / kTailHalf);
    s += w * t.hist[t.start[k] + p];
    if (++p >= kTailGrain) {
      /* Head 0 is placed before head 1 has played this sample, so it
       * matches against head 1's next one; head 1, after head 0 has. */
      p = 0;
      t.pos[k] = 0;
      t.start[k] = tail_place_grain(t, k, k == 0 ? 1 : 0);
    }
    t.pos[k] = p;
  }
  return s;
}

/* Mix the tails into one block of mono output, record the result, and track
 * the output level.  `strike` is true on a block that struck a note. */
static void tail_process(float* mono, size_t size, bool strike) {
  const float inv_size = 1.0f / (float)size;
  /* Step 3 applies the level match itself, as part of the crossfade. */
  const bool match_in_mix = bridge_mode_ == BRIDGE_OUT;
  if (strike && match_in_mix) strike_seen_ = true;

  /* Level match, on the live signal alone. */
  if (live_gain_ < 1.0f && !match_in_mix) {
    const float target = live_gain_ +
        (1.0f - live_gain_) * (strike ? kLiveRelaxStrike : kLiveRelax);
    const float target_c = target > 0.9999f ? 1.0f : target;
    const float step = (target_c - live_gain_) * inv_size;
    float g = live_gain_;
    for (size_t i = 0; i < size; ++i) {
      g += step;
      mono[i] *= g;
    }
    live_gain_ = target_c;
  }

  TailPlayer& d = tail_decay_;
  if (d.hist) {
    float g = d.gain;
    for (size_t i = 0; i < size; ++i) {
      g *= d.decay;
      mono[i] += tail_sample(d) * g;
    }
    d.gain = g;
    if (g < 1.0e-4f) {
      d.hist = NULL;
    }
  }

  if (bridge_mode_ != BRIDGE_OFF) {
    TailPlayer& t = tail_bridge_;
    alignas(16) float tail[kMaxBlockSize];
    const bool decaying = bridge_mode_ == BRIDGE_OUT;
    float tg = t.gain;
    for (size_t i = 0; i < size; ++i) {
      if (decaying) tg *= t.decay;
      tail[i] = tail_sample(t) * tg;
    }
    t.gain = tg;

    /* This block's target gains. */
    float live_target, bridge_target, match_target = live_gain_;
    float x = bridge_x_;
    if (bridge_mode_ == BRIDGE_IN) {
      x += (float)size / (float)(kTailXfadeBlocks * kMaxBlockSize);
      x = x > 1.0f ? 1.0f : x;
      bridge_target = tail_curve(x);
      live_target = 1.0f - bridge_target;
    } else if (bridge_mode_ == BRIDGE_READY) {
      live_target = 0.0f;
      bridge_target = 1.0f;
    } else {
      x -= (float)size / (float)(kTailHandbackBlocks * kMaxBlockSize);
      x = x < 0.0f ? 0.0f : x;
      float pl = 0.0f, pt = 0.0f;
      for (size_t i = 0; i < size; ++i) {
        pl += mono[i] * mono[i];
        pt += tail[i] * tail[i];
      }
      pl *= inv_size;
      pt *= inv_size;
      if (pow_bridge_ < 0.0f) {
        pow_bridge_ = pt;
        pow_live_ = strike_seen_ ? (tail_kept_ ? pt : 0.0f) : pl;
      } else {
        pow_bridge_ += kPowSmooth * (pt - pow_bridge_);
        if (!strike_seen_) pow_live_ += kPowSmooth * (pl - pow_live_);
      }
      /* Equal-power fade-in of the live signal, sin(pi/2 * (1 - x)), by a
       * polynomial accurate to 0.1%.  A strike comes in at full weight. */
      const float u = strike_seen_ ? 1.0f : 1.0f - x;
      const float u2 = u * u;
      live_target = u * (1.5707963f + u2 * (-0.6459641f + u2 * 0.0796926f));
      live_target = live_target > 1.0f ? 1.0f : live_target;
      const float pb = pow_bridge_ + 1.0e-12f;
      match_target = (pow_live_ > pb && !strike_seen_)
          ? sqrtf(pb / pow_live_) : 1.0f;
      match_target = match_target < kLiveMatchMin ? kLiveMatchMin : match_target;
      const float carried = live_target * live_target * match_target *
                            match_target * pow_live_ / (pb * kCarriedAll);
      bridge_target = carried < 1.0f ? sqrtf(1.0f - carried) : 0.0f;
    }
    bridge_x_ = x;

    /* Outside step 3 the level match has been applied to mono already. */
    const float lw0 = match_in_mix ? mix_live_ * live_gain_ : mix_live_;
    const float lw1 = match_in_mix ? live_target * match_target : live_target;
    const float ls = (lw1 - lw0) * inv_size;
    const float bs = (bridge_target - mix_bridge_) * inv_size;
    float lw = lw0;
    float bw = mix_bridge_;
    for (size_t i = 0; i < size; ++i) {
      lw += ls;
      bw += bs;
      mono[i] = mono[i] * lw + tail[i] * bw;
    }
    mix_live_   = live_target;
    mix_bridge_ = bridge_target;
    if (match_in_mix) live_gain_ = match_target;

    if (bridge_mode_ == BRIDGE_IN && x >= 1.0f) {
      bridge_mode_ = BRIDGE_READY;
    } else if (bridge_mode_ == BRIDGE_OUT && x <= 0.0f) {
      /* Step 3 is done.  What the tail is still making up for -- strings or
       * voices the change stopped, or a whole engine that starts from
       * silence -- carries on as the decay tail, at the weight it had. */
      if (bridge_target > 1.0e-3f) {
        tail_decay_ = t;
        tail_decay_.gain = t.gain * bridge_target;
        if (bridge_target < kTailWhole) {
          tail_decay_.decay = 1.0f - 1.0f / (kTailPartialTau * 48000.0f);
        }
      }
      t.hist       = NULL;
      bridge_mode_ = BRIDGE_OFF;
      mix_live_    = 1.0f;
      mix_bridge_  = 0.0f;
    }
  }

  /* Record.  size and kTailHistory are multiples of four, so a group of four
   * never straddles the wrap. */
  float* h = tail_hist_[0];
  float peak = 0.0f;
#ifdef __ARM_NEON
  float32x4_t vpeak = vdupq_n_f32(0.0f);
  for (size_t i = 0; i < size; i += 4) {
    const float32x4_t m = vld1q_f32(mono + i);
    vst1q_f32(h + tail_write_, m);
    tail_write_ = (tail_write_ + 4u) & (kTailHistory - 1u);
    vpeak = vmaxq_f32(vpeak, vabsq_f32(m));
  }
  float32x2_t p2 = vpmax_f32(vget_low_f32(vpeak), vget_high_f32(vpeak));
  peak = vget_lane_f32(vpmax_f32(p2, p2), 0);
#else
  for (size_t i = 0; i < size; ++i) {
    h[tail_write_] = mono[i];
    tail_write_ = (tail_write_ + 1u) & (kTailHistory - 1u);
    const float a = mono[i] < 0.0f ? -mono[i] : mono[i];
    if (a > peak) peak = a;
  }
#endif
  /* Decays about 60 dB over the history's length, so "silent" means the
   * whole history is. */
  out_level_ = peak > out_level_ * 0.96f ? peak : out_level_ * 0.96f;
}

/* Rings reads its lookup tables with stmlib's Interpolate(table, x, N), which
 * touches table[floor(x*N)] *and the element after it*.  The tables have N+1
 * entries, so x must stay below 1.0: at exactly 1.0 the second read is one
 * past the end.  Three of them are reachable from the panel — lut_stiffness
 * and lut_4_decades from Structure and Damping in Resonator::ComputeFilters(),
 * lut_fm_frequency_quantizer from Structure again in FMVoice::Process() — and
 * Structure or Damping at 100% is one knob position, not a corner case.
 *
 * The overrun is benign arithmetically (the interpolation weight on the stray
 * element is zero, since an integral index of N means a fractional part of
 * zero) and the tables happen to be followed by more .rodata, so it reads
 * neighbouring table data rather than faulting.  It is still a read past the
 * end of an array on the audio thread, and the cost of not doing it is one
 * 4096th of a knob's travel. */
static const float kLutSafeMax = 1.0f - 1.0f / 4096.0f;

static inline float clip_lut01f(float x) {
  x = clip01f(x);
  return (x > kLutSafeMax) ? kLutSafeMax : x;
}

/* ======================================================================
 * Arpeggiator
 * ==================================================================== */

/* Reserved OSC_PARAM index used by the drumlogue adapter to forward the
 * host tempo (integer BPM).  Kept high so it never collides with a real
 * per-oscillator parameter index. */
static const uint16_t kTempoParamIndex = 63;

/* Ascending chord tones (semitones from root) for the arpeggiator, one row
 * per Chord value.  A negative entry is an unused slot: no chord tone sits
 * below its own root, so the sign is free to mean "row ends here".  Six
 * slots, because the last two are scales as much as chords and the arp walks
 * them whole: Hirajoshi's five tones, and the Mystic chord as the scale it
 * comes from (Prometheus: 0 2 4 6 9 10), which is how it sounds as a
 * sequence -- voiced in fourths it spans over two octaves and the octave
 * repeats would interleave with it.  The rows before them keep their four.
 *
 * These are float, not the integer semitones they were, because the last two
 * chords are not in twelve-tone equal temperament -- Just7's seventh is 31
 * cents flat and every step of Slendro is 2.4 semitones.  The arpeggiator
 * re-strums the same resonator the sympathetic strings are tuned on, so an
 * integer table here would have had the arp playing a rounded version of the
 * chord the strings are ringing at: a third of a semitone out, held against a
 * resonator, which is a beat rather than a rounding error.  See the chord
 * table in eurorack-opt/rings/dsp/part.cc, which these mirror. */
#define kArpChordTones 6
static const float kArpChordIntervals[kNumChords][kArpChordTones] = {
  {  0.0f, 12.0f,  -1.0f, -1.0f, -1.0f, -1.0f },  /* Oct     */
  {  0.0f,  7.0f,  12.0f, -1.0f, -1.0f, -1.0f },  /* 5th     */
  {  0.0f,  5.0f,   7.0f, -1.0f, -1.0f, -1.0f },  /* sus4    */
  {  0.0f,  3.0f,   7.0f, -1.0f, -1.0f, -1.0f },  /* min     */
  {  0.0f,  3.0f,   7.0f, 10.0f, -1.0f, -1.0f },  /* min7    */
  {  0.0f,  3.0f,   7.0f, 14.0f, -1.0f, -1.0f },  /* min9    */
  {  0.0f,  3.0f,   7.0f, 17.0f, -1.0f, -1.0f },  /* min11   */
  {  0.0f,  4.0f,   7.0f,  9.0f, -1.0f, -1.0f },  /* 69      */
  {  0.0f,  4.0f,   7.0f, 14.0f, -1.0f, -1.0f },  /* Maj9    */
  {  0.0f,  4.0f,   7.0f, 11.0f, -1.0f, -1.0f },  /* Maj7    */
  {  0.0f,  4.0f,   7.0f, -1.0f, -1.0f, -1.0f },  /* Maj     */
  {  0.0f,  5.0f,  10.0f, 15.0f, -1.0f, -1.0f },  /* 4ths    */
  {  0.0f,  3.86f,  7.02f, 9.69f, -1.0f, -1.0f }, /* Just7   */
  {  0.0f,  2.4f,   4.8f,  7.2f, -1.0f, -1.0f },  /* Slendro */
  {  0.0f,  2.0f,   3.0f,  7.0f,  8.0f, -1.0f },  /* Hirajoshi */
  {  0.0f,  2.0f,   4.0f,  6.0f,  9.0f, 10.0f },  /* Mystic  */
};

/* Step length as a fraction of a quarter-note beat, per Arp Rate index. */
static const float kArpBeatsPerStep[6] = {
  1.0f,        /* 1/4  */
  0.5f,        /* 1/8  */
  1.0f / 3.0f, /* 1/8T */
  0.25f,       /* 1/16 */
  1.0f / 6.0f, /* 1/16T */
  0.125f,      /* 1/32 */
};

static const int kArpMaxNotes = 4 * kArpChordTones; /* 4 octaves of the longest row */

/* Arp parameters (drumlogue custom OSC_PARAM indices 10-13) */
static uint16_t arp_mode_    = 0;   /* 0 = Off, 1..8 = patterns */
static uint16_t arp_source_  = 0;   /* 0 = Chord tones, 1 = Octaves */
static uint16_t arp_rate_    = 3;   /* index into kArpBeatsPerStep */
static uint16_t arp_octaves_ = 1;   /* 1..4 */
static uint16_t arp_bpm_     = 120; /* host tempo, forwarded by the adapter */

/* Arp runtime state */
static uint32_t arp_accum_       = 0;      /* samples accumulated toward next step */
static int32_t  arp_pos_         = -1;     /* position in the step sequence (-1 = rearm) */
static int8_t   arp_seq_[2 * kArpMaxNotes] = {0}; /* step order: note-list index, or -1 for a rest */
static int32_t  arp_seq_len_     = 0;
static int32_t  arp_cached_n_    = -1;     /* note count the sequence was built for */
static uint16_t arp_cached_mode_ = 0xFFFF; /* pattern the sequence was built for */

static void arp_reset(void) {
  arp_accum_       = 0;
  arp_pos_         = -1;
  arp_seq_len_     = 0;
  arp_cached_n_    = -1;
  arp_cached_mode_ = 0xFFFF;
}

/* Build the list of note pitches (MIDI-ish float) for the current root. */
static int build_arp_notes(float root, float *out) {
  int n = 0;
  int octs = arp_octaves_;
  if (octs < 1) octs = 1;
  if (octs > 4) octs = 4;

  if (arp_source_ == 1) {
    /* Octaves only */
    for (int o = 0; o < octs && n < kArpMaxNotes; ++o)
      out[n++] = root + 12.0f * o;
  } else if (performance_state_.chord < 0) {
    /* Chord tones with Chord Off: the root is the only one, so this is the
     * Octaves source by another name. */
    for (int o = 0; o < octs && n < kArpMaxNotes; ++o)
      out[n++] = root + 12.0f * o;
  } else {
    /* Chord tones */
    int ci = (int)performance_state_.chord;
    if (ci > kNumChords - 1) ci = kNumChords - 1;
    for (int o = 0; o < octs; ++o) {
      for (int k = 0; k < kArpChordTones && n < kArpMaxNotes; ++k) {
        float iv = kArpChordIntervals[ci][k];
        if (iv < 0.0f) continue;
        out[n++] = root + iv + 12.0f * (float)o;
      }
    }
  }
  if (n < 1) { out[0] = root; n = 1; }
  return n;
}

/* Build the step-order sequence for the given pattern over n notes.
 * Entries are indices into the note list, or -1 for a silent rest step. */
static void build_arp_sequence(int mode, int n) {
  int len = 0;
#define ARP_PUSH(x) do { if (len < (int)sizeof(arp_seq_)) arp_seq_[len++] = (int8_t)(x); } while (0)
  switch (mode) {
    case 1: /* Up */
      for (int i = 0; i < n; ++i) ARP_PUSH(i);
      break;
    case 2: /* Down */
      for (int i = n - 1; i >= 0; --i) ARP_PUSH(i);
      break;
    case 3: /* Up-Down (no repeated endpoints) */
      for (int i = 0; i < n; ++i) ARP_PUSH(i);
      for (int i = n - 2; i >= 1; --i) ARP_PUSH(i);
      break;
    case 4: /* Down-Up (no repeated endpoints) */
      for (int i = n - 1; i >= 0; --i) ARP_PUSH(i);
      for (int i = 1; i <= n - 2; ++i) ARP_PUSH(i);
      break;
    case 5: /* Up-Pause-Down */
      for (int i = 0; i < n; ++i) ARP_PUSH(i);
      ARP_PUSH(-1);
      for (int i = n - 1; i >= 0; --i) ARP_PUSH(i);
      break;
    case 6: /* Up-Down-Pause */
      for (int i = 0; i < n; ++i) ARP_PUSH(i);
      for (int i = n - 2; i >= 1; --i) ARP_PUSH(i);
      ARP_PUSH(-1);
      break;
    case 7: /* Down-Pause-Up */
      for (int i = n - 1; i >= 0; --i) ARP_PUSH(i);
      ARP_PUSH(-1);
      for (int i = 0; i < n; ++i) ARP_PUSH(i);
      break;
    case 8: /* Down-Up-Pause */
      for (int i = n - 1; i >= 0; --i) ARP_PUSH(i);
      for (int i = 1; i <= n - 2; ++i) ARP_PUSH(i);
      ARP_PUSH(-1);
      break;
    default:
      ARP_PUSH(0);
      break;
  }
#undef ARP_PUSH
  if (len < 1) { arp_seq_[0] = 0; len = 1; }
  arp_seq_len_ = len;
}

/* Advance the arpeggiator by one render block.  Returns true and sets
 * *out_note when a fresh strum should fire this block; false leaves the
 * previous note ringing (rest step or mid-step). */
static bool arp_process(float root, float *out_note) {
  float notes[kArpMaxNotes];
  int n = build_arp_notes(root, notes);

  if (n != arp_cached_n_ || (int)arp_mode_ != (int)arp_cached_mode_) {
    build_arp_sequence(arp_mode_, n);
    arp_cached_n_    = n;
    arp_cached_mode_ = arp_mode_;
    if (arp_pos_ >= arp_seq_len_) arp_pos_ = -1;
  }

  float bpm = (arp_bpm_ >= 20) ? (float)arp_bpm_ : 120.0f;
  float beats = kArpBeatsPerStep[arp_rate_ < 6 ? arp_rate_ : 3];
  uint32_t step_samples = (uint32_t)((60.0f / bpm) * 48000.0f * beats);
  if (step_samples < 1) step_samples = 1;

  bool do_strum = false;
  if (arp_pos_ < 0) {
    /* First block after note-on: fire step 0 immediately. */
    arp_pos_   = 0;
    arp_accum_ = 0;
    do_strum   = true;
  } else {
    arp_accum_ += kMaxBlockSize;
    if (arp_accum_ >= step_samples) {
      arp_accum_ -= step_samples;
      arp_pos_    = (arp_pos_ + 1) % arp_seq_len_;
      do_strum    = true;
    }
  }

  int8_t idx = arp_seq_[arp_pos_];
  if (idx >= 0 && idx < n)
    *out_note = notes[idx];

  /* Rest steps (idx < 0) hold the previous note and skip the strum. */
  return do_strum && idx >= 0;
}

/* ======================================================================
 * OSC API Implementation
 * ==================================================================== */

void OSC_INIT(uint32_t platform, uint32_t api)
{
  (void)platform;
  (void)api;

  /* Rings excites itself from stmlib::Random -- Plucker for the internal
   * exciter, String::Process for the dispersion noise -- and that generator's
   * state is a static nothing here was seeding, so the unit rendered
   * differently depending on how much had been drawn from it before.  Seeded
   * here the sequence restarts with the unit, as modal-strike.cc already
   * does.  Consecutive notes still differ from each other; what becomes
   * repeatable is a unit load, which is what makes the rate-0 modulation
   * check in test_drmlgunit.c able to compare two renders at all. */
  stmlib::Random::Seed(0x82eef2a3);

  part_.Init(reverb_buffer_);
  /* Default to the quantized sympathetic-strings model so the Chord
   * parameter is effective out of the box.  Chord only affects this model
   * in the Rings DSP (Modal/String/FM ignore it), so defaulting to Modal
   * made the Chord knob appear to do nothing. */
  model_value        = RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED;
  polyphony_value    = 1;
  pending_model_     = model_value;
  pending_polyphony_ = polyphony_value;
  part_.set_model(static_cast<ResonatorModel>(model_value));
  part_.set_polyphony(polyphony_value);
  /* Chord defaults to Off, as the panel does. */
  p_values[k_user_osc_param_id3] = (uint16_t)(int16_t)-1;
  chord_on_value = false;
  chord_last_    = 0;
  part_.set_chord_enabled(false);

  patch_.structure  = 0.5f;
  patch_.brightness = 0.5f;
  patch_.damping    = 0.5f;
  patch_.position   = 0.5f;

  performance_state_.strum            = false;
  performance_state_.internal_exciter = true;
  performance_state_.internal_strum   = true;
  performance_state_.internal_note    = false;
  performance_state_.tonic            = 0.0f;
  performance_state_.note             = 69.0f;
  performance_state_.fm               = 0.0f;
  performance_state_.chord            = -1;

  note_on_seen_   = note_on_count_;
  since_strike_   = kStrikeGrace;
  strike_held_    = false;

  arp_mode_    = 0;
  arp_source_  = 0;
  arp_rate_    = 3;
  arp_octaves_ = 1;
  arp_bpm_     = 120;
  arp_reset();

  shape_lfo        = 0.0f;
  lfo2             = 0.0f;
  lfo2_phase       = 0.0f;
  lfo1_shape_value = 0;
  lfo2_shape_value = 0;
  lfo2_target_     = 0;
  lfo1_depth_value = 100;
  lfo_note_semitones_ = 2;

  std::fill(&in_buffer_[0], &in_buffer_[kMaxBlockSize], 0.0f);
  tail_reset();

  /* Part::Init() leaves the part dirty, and Part::ConfigureResonators() —
   * which for the string models re-initializes all eight strings, clearing
   * ~96 KB of delay line — runs from inside Part::Process().  Left alone that
   * lands on the first audio block after the unit is selected, on top of the
   * page faults a freshly dlopen()ed unit already pays there.  Same shape as
   * the Clouds first-Prepare() problem, same treatment: spend it here, on the
   * control thread, by rendering one block into the scratch buffers and
   * throwing it away.  The parameter pushes that follow are what would
   * otherwise dirty it again, and they carry the defaults set just above, so
   * the latch below sees no change and costs nothing. */
  part_.Process(performance_state_, patch_,
                in_buffer_, out_buffer_, aux_buffer_, kMaxBlockSize);
  std::fill(&out_buffer_[0], &out_buffer_[kMaxBlockSize], 0.0f);
  std::fill(&aux_buffer_[0], &aux_buffer_[kMaxBlockSize], 0.0f);
}

void OSC_CYCLE(const user_osc_param_t *const params, int32_t *yn, const uint32_t frames)
{
  (void)frames;

  /* Shape first, then depth: the shapes are transfer curves, so scaling the
   * input would bend the curve rather than turn the modulation down. */
  shape_lfo = apply_lfo1_shape(q31_to_f32(params->shape_lfo)) *
              (lfo1_depth_value * 0.01f);

  /* LFO2.  Every shape is derived from the one phase accumulator, so
   * switching shape mid-cycle continues rather than jumps.
   *
   * The cosine is computed from that phase rather than taken from stmlib's
   * CosineOscillator, which is what all three ports used to do.  That class
   * is a two-pole resonator, and InitApproximate() sets its coefficient to
   * 2 - 32*freq^2 -- which at freq 0 is exactly 2, a double pole at z = 1.
   * There the recursion stops oscillating and starts integrating: it ramps
   * linearly from whatever state the last non-zero rate left it in, about
   * 1.7 per thousand blocks measured.  Rate 0 is not an exotic setting, it
   * is where the knob starts.
   *
   * Left in, that is a crash and not just a wrong LFO.  With Note as the
   * destination the ramp carried `note` to about -3500 semitones, and
   * SemitonesToRatio() indexes a 257-entry table straight off the value with
   * no check of its own: ASan put the read 3392 entries before the start of
   * lut_pitch_ratio_high, and `make test-arm` segfaulted on 9 runs in 30.
   * A cosine of a bounded phase cannot do that at any rate, including zero.
   * macro-oscillator2.cc and modal-strike.cc have since had the same
   * substitution; the comment there tells the same story from their side. */
  { const float freq =
        clip01f(p_values[k_user_osc_param_id5] * 0.01f +
                get_lfo_value(LfoTargetLfo2Frequency)) / 600.f;
    const float depth =
        clip01f(p_values[k_user_osc_param_id6] * 0.01f +
                get_lfo_value(LfoTargetLfo2Depth));
    if (freq <= 0.0f) {
      /* Rate 0 means no modulation, not modulation parked somewhere.  A
       * stopped phase accumulator still has a value -- cos(0) is 1 -- and
       * reporting that would make Depth a DC offset whenever Rate is at its
       * end stop, which is where it starts.  The phase is left where it is
       * rather than rewound: Rate can itself be modulated (LfoTargetLfo2-
       * Frequency), and rewinding would re-trigger the shape every time the
       * effective rate crossed zero. */
      lfo2 = 0.0f;
    } else {
      lfo2_phase += freq;
      if (lfo2_phase >= 1.0f) lfo2_phase -= (float)(int)lfo2_phase;
      const float cosine = cosf(2.0f * 3.1415926535f * lfo2_phase); /* [-1, 1] */
      float raw;
      switch (lfo2_shape_value) {
        default:
        case 0: /* Cosine */
          raw = cosine;
          break;
        case 1: /* Triangle */
          raw = (lfo2_phase < 0.5f) ? (4.0f * lfo2_phase - 1.0f)
                                    : (3.0f - 4.0f * lfo2_phase);
          break;
        case 2: /* Ramp Up */
          raw = 2.0f * lfo2_phase - 1.0f;
          break;
        case 3: /* Ramp Down */
          raw = 1.0f - 2.0f * lfo2_phase;
          break;
        case 4: /* Fat Sine */
          raw = clip1m1f(cosine * (1.5f - 0.5f * cosine * cosine));
          break;
        case 5: /* Square: the cosine's sign */
          raw = (lfo2_phase < 0.25f || lfo2_phase >= 0.75f) ? 1.0f : -1.0f;
          break;
      }
      lfo2 = raw * depth;
    }
  }

  /* Apply latched engine reconfiguration here, on the audio thread, before
   * anything reads polyphony_ or model_.  See pending_* above.  A change the
   * Part cannot carry across while something is sounding first crossfades
   * into a frozen tail and is applied when that is done; see "Model /
   * Polyphony transitions" above. */
  {
    const int32_t want_model     = pending_model_;
    const int32_t want_polyphony = pending_polyphony_;
    const bool    want_chord     = chord_param() >= 0;
    const bool change = want_model != (int32_t)model_value ||
                        want_polyphony != (int32_t)polyphony_value ||
                        want_chord != chord_on_value;
    /* Asked before the change is applied: Part compares against what it
     * last configured, which the setters do not touch. */
    const bool keeps = part_.KeepsStateFor(
        static_cast<ResonatorModel>(want_model), want_polyphony, want_chord);
    if (change && bridge_mode_ == BRIDGE_OUT) {
      bridge_mode_ = BRIDGE_IN;           /* turn round; see bridge_x_ */
    } else if (change && bridge_mode_ == BRIDGE_OFF && out_level_ > 1.0e-4f) {
      tail_freeze();
    }
    if (change && bridge_mode_ != BRIDGE_IN) {
      if (want_model != (int32_t)model_value) {
        model_value = (uint16_t)want_model;
        part_.set_model(static_cast<ResonatorModel>(want_model));
      }
      if (want_polyphony != (int32_t)polyphony_value) {
        polyphony_value = (uint16_t)want_polyphony;
        part_.set_polyphony(want_polyphony);
      }
      if (want_chord != chord_on_value) {
        chord_on_value = want_chord;
        part_.set_chord_enabled(want_chord);
      }
    }
    if (bridge_mode_ == BRIDGE_READY) {
      /* The change was applied just above -- or undone while it waited, in
       * which case the bridge simply hands back. */
      tail_after_change(keeps || !change);
    }
  }
  const bool change_pending = bridge_mode_ == BRIDGE_IN;

  /* Pitch from adapter (note.fraction encoding).  What sounds is held_note_,
   * set below; see "Note changes wait for the next strike" at the top. */
  const float host_note =
      ((float)(params->pitch >> 8)) +
      ((params->pitch & 0xFF) * k_note_mod_fscale);

  /* Patch parameters.  Structure and Damping index lookup tables one element
   * wider than their own top value; see kLutSafeMax.  The clips also absorb
   * the modulation, so an LFO cannot push a knob past its own range. */
  patch_.position   = clip01f(shape + get_lfo_value(LfoTargetPosition));
  patch_.structure  = clip_lut01f(shiftshape + get_lfo_value(LfoTargetStructure));
  patch_.brightness = clip01f(p_values[k_user_osc_param_id1] * 0.01f +
                              get_lfo_value(LfoTargetBrightness));
  patch_.damping    = clip_lut01f(p_values[k_user_osc_param_id2] * 0.01f +
                                  get_lfo_value(LfoTargetDamping));

  /* Chord from param, plus modulation.  Rounded, because this indexes a table
   * of tunings rather than naming a quantity: the LFO steps along the chord
   * list.  The clamp is load-bearing — see the modulation notes at the top.
   *
   * Off is -1, and is what the Part was configured with rather than what the
   * panel says, so that the configuration being faded out by a bridge keeps
   * sounding as it did.  Modulation never reaches Off and does nothing while
   * Chord is Off: going on and off reconfigures the Part, which an LFO would
   * then do at its own rate, and Off means no chord. */
  if (chord_on_value) {
    int32_t chord = chord_param();
    if (chord < 0) chord = chord_last_;
    else chord_last_ = chord;
    const float chord_mod = get_lfo_value(LfoTargetChord);
    if (chord_mod != 0.0f)
      chord += (int32_t)lrintf(chord_mod * (float)(kNumChords - 1));
    if (chord < 0) chord = 0;
    if (chord >= kNumChords) chord = kNumChords - 1;
    performance_state_.chord = chord;
  } else {
    performance_state_.chord = -1;
  }

  /* Strike detection.  When the arpeggiator is running it drives both the
   * pitch and the strike timing.  Otherwise every note-on strikes, counted
   * rather than read off the gate: a note-on with the gate already up is a
   * new note too, and one whose note-off arrived before this block still
   * gets its strike. */
  const uint32_t note_ons = note_on_count_;
  const bool note_on = note_ons != note_on_seen_;
  note_on_seen_ = note_ons;
  bool strum;
  float strike_note = host_note;
  if (arp_mode_ != 0 && gate_) {
    strike_note = held_note_;  /* a rest step leaves the ringing note alone */
    strum = arp_process(host_note, &strike_note);
  } else {
    strum = note_on;
    if (!gate_) arp_pos_ = -1; /* rearm the arp for the next note-on */
  }

  /* A strike that arrives while a change is waiting for step 1 of its
   * bridge is held until the change is in, at most 2 ms.  Struck now, it
   * would sound in the configuration being left and then be frozen into the
   * tail -- so a Model change and a note on the same sequencer step, which is
   * what motion-sequencing Model is, played the note on the old model and
   * left the new one unstruck. */
  if (change_pending) {
    if (strum) {
      strike_held_      = true;
      strike_held_note_ = strike_note;
    }
    strum = false;
  } else if (strike_held_) {
    strike_held_ = false;
    strum        = true;
    strike_note  = strike_held_note_;
  }

  /* Hold the pitch between strikes. */
  if (strum) {
    held_note_    = strike_note;
    since_strike_ = 0;
  } else if (since_strike_ < kStrikeGrace) {
    held_note_ = strike_note;  /* the strike just played, its pitch late */
  } else {
    const float step = host_note - host_note_prev_;
    if (step > -kNoteJump && step < kNoteJump) held_note_ += step;
  }
  host_note_prev_ = host_note;
  if (since_strike_ < kStrikeGrace) since_strike_ += kMaxBlockSize;
  performance_state_.note  = held_note_;
  performance_state_.strum = strum;

  /* Pitch modulation goes on last, after the arpeggiator has chosen its step.
   * Modulating the root the arp is built from instead would rewrite the
   * pattern's intervals under it; this transposes the whole thing, which is
   * what a pitch LFO is for.
   *
   * Then the result is pinned to the MIDI range, which is not about musical
   * sense — nothing here can leave it — but about what happens downstream if
   * something ever does.  Part turns this into string frequencies through
   * SemitonesToRatio(), which indexes a 257-entry table off the value with no
   * check of its own, so a wild note is a wild read on the audio thread.  A
   * diverging LFO reached -3500 semitones during development and that is
   * exactly how it presented.  Two compares to make the table index a
   * property of this line rather than of everything upstream of it. */
  performance_state_.note +=
      get_lfo_value(LfoTargetNote) * (float)lfo_note_semitones_;
  if (!(performance_state_.note > 0.0f)) performance_state_.note = 0.0f;
  else if (performance_state_.note > 127.0f) performance_state_.note = 127.0f;

  /* Clear input (internal exciter mode) */
  std::fill(&in_buffer_[0], &in_buffer_[kMaxBlockSize], 0.0f);

  /* Output gain: +3 dB overall, with an extra +6 dB for the sympathetic-
   * string models (1 and 4), which are inherently quieter than the
   * Modal/String/FM/Reverb models.
   *
   * The +6 dB goes in ahead of Rings' own output limiter, the +3 dB after it.
   * That limiter holds each channel to about 0.68 (8 of 10 Vpp on the
   * module), so +3 dB after it peaks at 0.95 and the mono mix below can never
   * reach full scale.  Both used to come after it, and with the extra +6 dB a
   * loud sympathetic chord -- Sympathetic Quantized, the default model, at
   * Polyphony 4, or Sympathetic at Polyphony 1 -- went up to 1.9 and was
   * hard-clipped by the conversion below: measured with a note every 250 ms,
   * 1-1.7% of samples sat at full scale, some 44 clipped peaks a second, each
   * a corner in the waveform.  Ahead of the limiter the boost is the same for
   * everything below its threshold and is limited, not clipped, above it.
   * The other models' signal path is unchanged bit for bit. */
  const bool sympathetic =
      model_value == RESONATOR_MODEL_SYMPATHETIC_STRING ||
      model_value == RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED;
  const float out_gain = 1.4125375f;                     /* +3 dB */
  part_.set_output_boost(sympathetic ? 1.9952623f : 1.0f); /* +6 dB */

  /* Process Rings */
  part_.Process(
      performance_state_, patch_,
      in_buffer_, out_buffer_, aux_buffer_,
      kMaxBlockSize);

  /* Mix stereo to mono: out + aux, except for String + Reverb, which is
   * out - aux.  Rings negates that model's aux after its reverb (upstream
   * part.cc), so on a stereo pair it is a sign convention, but summed to mono
   * it cancelled the dry string: the model cross-mixes the two channels by
   * Position first, so out + aux there is (2 * Position - 1) * (L - R) --
   * exactly zero at the default Position of 50%, leaving only the reverb's
   * stereo difference.  That is why the arpeggiator seemed not to work on
   * this model: its strums were there, but only as reverb.  out - aux is
   * the dry string plus the full reverb. */
  static_assert(kMaxBlockSize % 4 == 0, "the mono stage works in groups of four");
  const float aux_sign =
      (model_value == RESONATOR_MODEL_STRING_AND_REVERB) ? -1.0f : 1.0f;
  alignas(16) float mono[kMaxBlockSize];
#ifdef __ARM_NEON
  {
    const float32x4_t vgain = vdupq_n_f32(0.5f * out_gain);
    const float32x4_t vsign = vdupq_n_f32(aux_sign);
    for (size_t i = 0; i < kMaxBlockSize; i += 4) {
      float32x4_t l = vld1q_f32(out_buffer_ + i);
      float32x4_t r = vld1q_f32(aux_buffer_ + i);
      vst1q_f32(mono + i, vmulq_f32(vmlaq_f32(l, r, vsign), vgain));
    }
  }
#else
  for (size_t i = 0; i < kMaxBlockSize; ++i) {
    mono[i] = (out_buffer_[i] + aux_sign * aux_buffer_[i]) * 0.5f * out_gain;
  }
#endif

  tail_process(mono, kMaxBlockSize, strum);

  /* To Q31.  The adapter expects exactly kMaxBlockSize mono samples. */
#ifdef __ARM_NEON
  {
    const float32x4_t vscale = vdupq_n_f32(2147483648.0f);
    const float32x4_t vmin = vdupq_n_f32(-1.0f);
    const float32x4_t vmax = vdupq_n_f32(1.0f);
    for (size_t i = 0; i < kMaxBlockSize; i += 4) {
      float32x4_t m = vmaxq_f32(vminq_f32(vld1q_f32(mono + i), vmax), vmin);
      vst1q_s32(yn + i, vcvtq_s32_f32(vmulq_f32(m, vscale)));
    }
  }
#else
  for (size_t i = 0; i < kMaxBlockSize; ++i) {
    yn[i] = f32_to_q31(mono[i]);
  }
#endif
}

void OSC_NOTEON(const user_osc_param_t *const params)
{
  (void)params;
  gate_ = true;
  arp_pos_   = -1; /* restart the arp sequence from the first step */
  arp_accum_ = 0;
  note_on_count_ = note_on_count_ + 1; /* a strike, in the next block */
}

void OSC_NOTEOFF(const user_osc_param_t *const params)
{
  (void)params;
  gate_ = false;
}

/* Neutral state, on the audio thread between blocks.  See OSC_RESET in
 * drumlogue/userosc.h for why it is not done where unit_reset() is called --
 * Part::Init() re-seats the very buffers Part::Process() reads, which is the
 * race that took the audio engine down when Polyphony was written from the
 * control thread.
 *
 * Part::Init() is the expensive part: it clears the reverb buffer and
 * re-initializes eight strings, around 96 KB, and one block is not enough
 * time for it. That is a single over-budget block on a unit swap, which is
 * what the SDK has in mind when it says delay lines may be "set to be
 * cleared" rather than cleared at once -- and it is the same cost the model
 * and polyphony latches already pay when either changes.
 *
 * Init() also puts Part back to its own defaults for model and polyphony, so
 * both are re-applied through the latch rather than left to drift out of step
 * with what the panel says. Parameters themselves are untouched. */
void OSC_RESET(void)
{
  gate_          = false;
  note_on_seen_  = note_on_count_;
  since_strike_  = kStrikeGrace;
  strike_held_   = false;
  shape_lfo      = 0.0f;
  lfo2           = 0.0f;
  lfo2_phase     = 0.0f;
  arp_reset();

  performance_state_.strum = false;

  part_.Init(reverb_buffer_);
  part_.set_model(static_cast<ResonatorModel>(model_value));
  part_.set_polyphony(polyphony_value);
  part_.set_chord_enabled(chord_on_value);

  std::fill(&in_buffer_[0],  &in_buffer_[kMaxBlockSize],  0.0f);
  std::fill(&out_buffer_[0], &out_buffer_[kMaxBlockSize], 0.0f);
  std::fill(&aux_buffer_[0], &aux_buffer_[kMaxBlockSize], 0.0f);
  tail_reset();
}

void OSC_PARAM(uint16_t index, uint16_t value)
{
  switch (index) {
    case k_user_osc_param_id1:
    case k_user_osc_param_id2:
    case k_user_osc_param_id3:
    case k_user_osc_param_id4:
    case k_user_osc_param_id5:
    case k_user_osc_param_id6:
      p_values[index] = value;
      break;

    case k_user_osc_param_shape:
      shape = param_val_to_f32(value);
      break;

    case k_user_osc_param_shiftshape:
      shiftshape = param_val_to_f32(value);
      break;

    /* Custom params passed by the wrapper.  Both reconfigure the Part, so
     * they are only latched here and applied by OSC_CYCLE on the audio
     * thread — see pending_* at the top of this file. */
    case 8: /* Model (0-5) */
      if (value < RESONATOR_MODEL_LAST)
        pending_model_ = value;
      break;

    case 9: /* Polyphony (1-4) */
      if (value >= 1 && value <= kMaxPolyphony)
        pending_polyphony_ = value;
      break;

    case 10: /* Arp Mode (0 = Off, 1..8 patterns) */
      arp_mode_ = value;
      arp_cached_mode_ = 0xFFFF; /* force sequence rebuild */
      if (value == 0) arp_pos_ = -1;
      break;

    case 11: /* Arp Source (0 = Chord tones, 1 = Octaves) */
      arp_source_ = value;
      arp_cached_n_ = -1; /* note count may change -> rebuild */
      break;

    case 12: /* Arp Rate (index into division table) */
      arp_rate_ = value;
      break;

    case 13: /* Arp Octaves (1..4) */
      arp_octaves_ = value;
      arp_cached_n_ = -1; /* note count changes -> rebuild */
      break;

    case 14: /* LFO1 Shape (0-5) */
      lfo1_shape_value = value;
      break;

    case 15: /* LFO2 Target (0-5; LFO1's two extra targets are its own) */
      lfo2_target_ = value;
      break;

    case 16: /* LFO2 Shape (0-5) */
      lfo2_shape_value = value;
      break;

    case 17: /* LFO1 Depth (0-100) */
      lfo1_depth_value = value > 100 ? 100 : value;
      break;

    case 18: /* Note Range, in semitones at full LFO swing */
      lfo_note_semitones_ =
          value > kLfoNoteSemitonesMax ? kLfoNoteSemitonesMax : value;
      break;

    case kTempoParamIndex: /* Host tempo (integer BPM), forwarded by adapter */
      if (value > 0) arp_bpm_ = value;
      break;

    default:
      break;
  }
}

#ifdef RINGS_ARP_TEST
/* Test-only hooks: expose arp internals to the native host harness so the
 * step ordering and timing can be verified without decoding audio.  Never
 * compiled into firmware (RINGS_ARP_TEST is a test build flag only). */
extern "C" {
int   rings_arp_test_pos(void)     { return (int)arp_pos_; }
int   rings_arp_test_seq_len(void) { return (int)arp_seq_len_; }
int   rings_arp_test_seq(int i)    { return (i >= 0 && i < arp_seq_len_) ? (int)arp_seq_[i] : -99; }
float rings_arp_test_note(void)    { return performance_state_.note; }
bool  rings_arp_test_strum(void)   { return performance_state_.strum; }
}
#endif
