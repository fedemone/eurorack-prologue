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

/* Transfer curve for LFO1.  Same five shapes as the other ports. */
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

/* ======================================================================
 * Model / Polyphony transitions
 *
 * Changing either used to stop the sound dead: Part re-initialised every
 * string, mode set or FM voice and the new configuration started from
 * silence.  The forked Part (eurorack-opt/rings/dsp/part.cc) now keeps what
 * is already ringing whenever it can -- any change among the four string
 * models, and polyphony changes for the string and FM models -- and says so
 * through Part::KeepsStateFor().
 *
 * That alone is not seamless.  Even where the strings carry on, what is heard
 * of them changes at once: the models mix and scale their output differently
 * (String + Reverb sends the dry string through its reverb's 65/35 mix, the
 * sympathetic models get 6 dB of make-up, a polyphony change regroups the
 * strings into voices and switches the pickup sum), and an instant change of
 * gain or mix on a sounding signal is a click.  And a change Part cannot carry
 * across -- between the modal, string and FM engines, or of modal polyphony --
 * still starts from silence.
 *
 * So every change made while something is sounding is bridged here, using
 * the output's last 85 ms, which are kept in a history buffer:
 *
 *   1. Freeze that history and crossfade the old configuration into it over
 *      2 ms.  The change waits for this, so it lands 2 ms late.
 *   2. Apply the change.
 *   3. Where Part kept the sound going, crossfade back from the frozen tail
 *      to it over 24 ms -- long enough to cover strings being retuned as they
 *      change voice or chord.  Where it could not, let the tail die away
 *      under the new configuration (faster at low Damping, slower at high).
 *
 * The frozen history is played by two grain heads half a grain apart, with
 * triangular windows that sum to exactly one, each grain starting at a random
 * point in the history: a granular freeze, so a sustained tone continues as a
 * tone rather than as a loop.  Changes in quick succession -- the Model knob
 * swept through its range -- are covered too: a dying tail keeps dying under
 * later changes, and a change arriving during step 3 turns the crossfade
 * round where it stands.  Measured with a note ringing, a sweep through all
 * six models at one step per 30 ms plays continuously, where it used to fall
 * silent at the first step.
 *
 * Cost, counted on the ARM build: about forty instructions per sample while a
 * tail plays -- 2.5k per 64-frame render, under 3% of what the default model
 * spends -- and about a hundred per render to keep the history.  When nothing
 * is sounding, a change is applied at once, as before.
 * ==================================================================== */

#define kTailHistory 4096          /* 85 ms at 48 kHz; power of two */
#define kTailGrain   2048          /* grain length; heads run half a grain apart */
#define kTailXfadeBlocks 4         /* step 1: 4 x 24 samples = 2 ms */
#define kTailHandbackBlocks 48     /* step 3, kept: 24 ms */

/* The output is recorded continuously into tail_hist_[0].  A freeze copies
 * it, oldest sample first, into one of the other two, which the bridge and
 * the decay tail below play from.  Copying rather than switching which
 * buffer records is what keeps a full 85 ms of history behind every freeze,
 * however soon after the last one it comes. */
alignas(16) static float tail_hist_[3][kTailHistory];

struct TailPlayer {
  const float* hist;    /* frozen history, or NULL when idle */
  uint32_t oldest;      /* its oldest sample */
  uint32_t pos[2];      /* position within each head's grain */
  uint32_t start[2];    /* each grain's start in the history */
  float gain;
  float decay;          /* per sample */
};

enum BridgeMode {
  BRIDGE_OFF,
  BRIDGE_IN,            /* step 1: what is playing -> frozen history */
  BRIDGE_READY,         /* step 1 done: apply the change */
  BRIDGE_OUT            /* step 3, kept: frozen history -> what is playing */
};

/* The bridge's place in its crossfade, 0 (not heard) to 1 (all that is
 * heard), moved per sample.  Kept as a position rather than a count of blocks
 * so that a change arriving while the bridge is still handing back -- a knob
 * turned through several models in quick succession -- simply turns it round
 * from where it is: no new freeze, nothing dropped from the output. */
static float bridge_x_ = 0.0f;

/* The bridge is frozen at each change and covers it (steps 1 and 3).  The
 * decay tail is what a change Part could not carry across leaves behind; it
 * dies away under everything after it, including later changes, which is why
 * it is a separate player: a later change that Part does carry across hands
 * back to the strings *and* this tail, not to the strings alone. */
static TailPlayer tail_bridge_;
static TailPlayer tail_decay_;
static BridgeMode bridge_mode_  = BRIDGE_OFF;
static uint32_t tail_write_    = 0;     /* write position in tail_hist_[0] */
static uint32_t tail_rng_      = 0x2545F491u;
static float    out_level_     = 0.0f;  /* recent output peak: is anything sounding? */

static inline uint32_t tail_pick_start(void) {
  tail_rng_ = tail_rng_ * 1664525u + 1013904223u;
  return (tail_rng_ >> 8) % (kTailHistory - kTailGrain + 1);
}

static void tail_reset(void) {
  std::fill(&tail_hist_[0][0], &tail_hist_[0][0] + 3 * kTailHistory, 0.0f);
  tail_bridge_.hist = NULL;
  tail_decay_.hist  = NULL;
  bridge_mode_   = BRIDGE_OFF;
  bridge_x_      = 0.0f;
  tail_write_    = 0;
  out_level_     = 0.0f;
}

/* Step 1: freeze the history into the bridge and start crossfading into it. */
static void tail_freeze(void) {
  TailPlayer& t = tail_bridge_;
  float* frozen = tail_hist_[tail_decay_.hist == tail_hist_[1] ? 2 : 1];
  const float* live = tail_hist_[0];
  std::copy(live + tail_write_, live + kTailHistory, frozen);
  std::copy(live, live + tail_write_, frozen + (kTailHistory - tail_write_));
  t.hist   = frozen;
  t.oldest = 0;
  t.pos[0] = 0;
  t.pos[1] = kTailGrain / 2;
  t.start[0] = tail_pick_start();
  t.start[1] = tail_pick_start();
  t.gain   = 1.0f;
  /* Used if this becomes the decay tail: tau = 0.1 s at Damping 0 to 0.4 s
   * at 100%, i.e. -60 dB in 0.7 to 2.8 s. */
  const float tau = 0.1f + 0.3f * patch_.damping;
  t.decay  = 1.0f - 1.0f / (tau * 48000.0f);
  bridge_mode_   = BRIDGE_IN;
  bridge_x_      = 0.0f;
}

/* Step 3: the change has been applied. */
static void tail_after_change(bool kept) {
  if (kept) {
    bridge_mode_   = BRIDGE_OUT;
  } else {
    /* The bridge becomes the decay tail.  Any earlier decay tail is in the
     * bridge's history already, so it is dropped rather than played twice. */
    tail_decay_       = tail_bridge_;
    tail_bridge_.hist = NULL;
    bridge_mode_      = BRIDGE_OFF;
  }
}

/* Crossfade curve: smoothstep, so the weight starts and ends with zero slope.
 * A linear ramp leaves a corner in the waveform at each end of the fade --
 * no step, but a kink, and on a smooth modal tone a visible one. */
static inline float tail_curve(float x) {
  return x * x * (3.0f - 2.0f * x);
}

static inline float tail_sample(TailPlayer& t) {
  float s = 0.0f;
  for (int k = 0; k < 2; ++k) {
    uint32_t p = t.pos[k];
    const float w = (p < kTailGrain / 2 ? (float)p : (float)(kTailGrain - p)) *
                    (2.0f / kTailGrain);
    s += w * t.hist[(t.oldest + t.start[k] + p) & (kTailHistory - 1)];
    if (++p >= kTailGrain) {
      p = 0;
      t.start[k] = tail_pick_start();
    }
    t.pos[k] = p;
  }
  return s;
}

/* Mix the tails into one block of mono output, record the result, and track
 * the output level. */
static void tail_process(float* mono, size_t size) {
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
  if (bridge_mode_ == BRIDGE_IN || bridge_mode_ == BRIDGE_OUT) {
    const bool in = bridge_mode_ == BRIDGE_IN;
    const float step = in ? 1.0f / (float)(kTailXfadeBlocks * kMaxBlockSize)
                          : -1.0f / (float)(kTailHandbackBlocks * kMaxBlockSize);
    float x = bridge_x_;
    for (size_t i = 0; i < size; ++i) {
      x += step;
      x = x > 1.0f ? 1.0f : (x < 0.0f ? 0.0f : x);
      const float w = tail_curve(x);
      mono[i] = mono[i] * (1.0f - w) + tail_sample(tail_bridge_) * w;
    }
    bridge_x_ = x;
    if (in && x >= 1.0f) {
      bridge_mode_ = BRIDGE_READY;
    } else if (!in && x <= 0.0f) {
      tail_bridge_.hist = NULL;
      bridge_mode_ = BRIDGE_OFF;
    }
  } else if (bridge_mode_ == BRIDGE_READY) {
    /* Only if a block runs before the change is applied. */
    for (size_t i = 0; i < size; ++i) mono[i] = tail_sample(tail_bridge_);
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
 * below its own root, so the sign is free to mean "row ends here".
 *
 * These are float, not the integer semitones they were, because the last two
 * chords are not in twelve-tone equal temperament -- Just7's seventh is 31
 * cents flat and every step of Slendro is 2.4 semitones.  The arpeggiator
 * re-strums the same resonator the sympathetic strings are tuned on, so an
 * integer table here would have had the arp playing a rounded version of the
 * chord the strings are ringing at: a third of a semitone out, held against a
 * resonator, which is a beat rather than a rounding error.  See the chord
 * table in eurorack-opt/rings/dsp/part.cc, which these mirror. */
static const float kArpChordIntervals[kNumChords][4] = {
  {  0.0f, 12.0f,  -1.0f, -1.0f },  /* Oct     */
  {  0.0f,  7.0f,  12.0f, -1.0f },  /* 5th     */
  {  0.0f,  5.0f,   7.0f, -1.0f },  /* sus4    */
  {  0.0f,  3.0f,   7.0f, -1.0f },  /* min     */
  {  0.0f,  3.0f,   7.0f, 10.0f },  /* min7    */
  {  0.0f,  3.0f,   7.0f, 14.0f },  /* min9    */
  {  0.0f,  3.0f,   7.0f, 17.0f },  /* min11   */
  {  0.0f,  4.0f,   7.0f,  9.0f },  /* 69      */
  {  0.0f,  4.0f,   7.0f, 14.0f },  /* Maj9    */
  {  0.0f,  4.0f,   7.0f, 11.0f },  /* Maj7    */
  {  0.0f,  4.0f,   7.0f, -1.0f },  /* Maj     */
  {  0.0f,  5.0f,  10.0f, 15.0f },  /* 4ths    */
  {  0.0f,  3.86f,  7.02f, 9.69f }, /* Just7   */
  {  0.0f,  2.4f,   4.8f,  7.2f },  /* Slendro */
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

static const int kArpMaxNotes = 16; /* 4 octaves * up to 4 chord tones */

/* Arp parameters (drumlogue custom OSC_PARAM indices 10-13) */
static uint16_t arp_mode_    = 0;   /* 0 = Off, 1..8 = patterns */
static uint16_t arp_source_  = 0;   /* 0 = Chord tones, 1 = Octaves */
static uint16_t arp_rate_    = 3;   /* index into kArpBeatsPerStep */
static uint16_t arp_octaves_ = 1;   /* 1..4 */
static uint16_t arp_bpm_     = 120; /* host tempo, forwarded by the adapter */

/* Arp runtime state */
static uint32_t arp_accum_       = 0;      /* samples accumulated toward next step */
static int32_t  arp_pos_         = -1;     /* position in the step sequence (-1 = rearm) */
static int8_t   arp_seq_[40]     = {0};    /* step order: note-list index, or -1 for a rest */
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
  } else {
    /* Chord tones */
    int ci = (int)performance_state_.chord;
    if (ci < 0) ci = 0;
    if (ci > kNumChords - 1) ci = kNumChords - 1;
    for (int o = 0; o < octs; ++o) {
      for (int k = 0; k < 4 && n < kArpMaxNotes; ++k) {
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
  performance_state_.chord            = 0;

  note_on_seen_   = note_on_count_;
  since_strike_   = kStrikeGrace;

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
    const bool change = want_model != (int32_t)model_value ||
                        want_polyphony != (int32_t)polyphony_value;
    /* Asked before the change is applied: Part compares against what it
     * last configured, which set_model()/set_polyphony() do not touch. */
    const bool keeps = part_.KeepsStateFor(
        static_cast<ResonatorModel>(want_model), want_polyphony);
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
    }
    if (bridge_mode_ == BRIDGE_READY) {
      /* The change was applied just above -- or undone while it waited, in
       * which case the bridge simply hands back. */
      tail_after_change(keeps || !change);
    }
  }

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
   * list.  The clamp is load-bearing — see the modulation notes at the top. */
  { int32_t chord = (int32_t)p_values[k_user_osc_param_id3];
    const float chord_mod = get_lfo_value(LfoTargetChord);
    if (chord_mod != 0.0f)
      chord += (int32_t)lrintf(chord_mod * (float)(kNumChords - 1));
    if (chord < 0) chord = 0;
    if (chord >= kNumChords) chord = kNumChords - 1;
    performance_state_.chord = chord;
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

  tail_process(mono, kMaxBlockSize);

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
  shape_lfo      = 0.0f;
  lfo2           = 0.0f;
  lfo2_phase     = 0.0f;
  arp_reset();

  performance_state_.strum = false;

  part_.Init(reverb_buffer_);
  part_.set_model(static_cast<ResonatorModel>(model_value));
  part_.set_polyphony(polyphony_value);

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

    case 14: /* LFO1 Shape (0-4) */
      lfo1_shape_value = value;
      break;

    case 15: /* LFO2 Target (0-5; LFO1's two extra targets are its own) */
      lfo2_target_ = value;
      break;

    case 16: /* LFO2 Shape (0-4) */
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
