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
static bool previous_gate_ = false;

/* User-facing parameter storage */
uint16_t p_values[k_num_user_osc_param_id] = {0};
float shape = 0, shiftshape = 0;
float shape_lfo = 0;

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

  previous_gate_ = false;

  arp_mode_    = 0;
  arp_source_  = 0;
  arp_rate_    = 3;
  arp_octaves_ = 1;
  arp_bpm_     = 120;
  arp_reset();

  std::fill(&in_buffer_[0], &in_buffer_[kMaxBlockSize], 0.0f);

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

  shape_lfo = q31_to_f32(params->shape_lfo);

  /* Apply latched engine reconfiguration here, on the audio thread, before
   * anything reads polyphony_ or model_.  See pending_* above. */
  {
    const int32_t want_model     = pending_model_;
    const int32_t want_polyphony = pending_polyphony_;
    if (want_model != (int32_t)model_value) {
      model_value = (uint16_t)want_model;
      part_.set_model(static_cast<ResonatorModel>(want_model));
    }
    if (want_polyphony != (int32_t)polyphony_value) {
      polyphony_value = (uint16_t)want_polyphony;
      part_.set_polyphony(want_polyphony);
    }
  }

  /* Pitch from adapter (note.fraction encoding) */
  performance_state_.note =
      ((float)(params->pitch >> 8)) +
      ((params->pitch & 0xFF) * k_note_mod_fscale);

  /* Patch parameters.  Structure and Damping index lookup tables one element
   * wider than their own top value; see kLutSafeMax. */
  patch_.position   = clip01f(shape);
  patch_.structure  = clip_lut01f(shiftshape);
  patch_.brightness = clip01f(p_values[k_user_osc_param_id1] * 0.01f);
  patch_.damping    = clip_lut01f(p_values[k_user_osc_param_id2] * 0.01f);

  /* Chord from param */
  performance_state_.chord = p_values[k_user_osc_param_id3];
  if (performance_state_.chord >= kNumChords)
    performance_state_.chord = kNumChords - 1;

  /* Gate / strum detection.  When the arpeggiator is running it drives both
   * the sounding pitch and the strum timing; otherwise strum follows the
   * gate edge as before. */
  if (arp_mode_ != 0 && gate_) {
    float arp_note = performance_state_.note;
    bool strum = arp_process(performance_state_.note, &arp_note);
    performance_state_.note  = arp_note;
    performance_state_.strum = strum;
    previous_gate_ = gate_;
  } else {
    performance_state_.strum = (gate_ && !previous_gate_);
    previous_gate_ = gate_;
    if (!gate_) arp_pos_ = -1; /* rearm the arp for the next note-on */
  }

  /* Clear input (internal exciter mode) */
  std::fill(&in_buffer_[0], &in_buffer_[kMaxBlockSize], 0.0f);

  /* Process Rings */
  part_.Process(
      performance_state_, patch_,
      in_buffer_, out_buffer_, aux_buffer_,
      kMaxBlockSize);

  /* Output gain: +3 dB overall, with an extra +6 dB for the sympathetic-
   * string models (1 and 4), which are inherently quieter than the
   * Modal/String/FM/Reverb models.  f32_to_q31 (and the NEON clamp) guard
   * against overshoot on the rare loud transient. */
  float out_gain = 1.4125375f; /* +3 dB */
  if (model_value == RESONATOR_MODEL_SYMPATHETIC_STRING ||
      model_value == RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED)
    out_gain *= 1.9952623f;    /* +6 dB */

  /* Mix stereo (out + aux) to mono Q31.
   * The adapter expects exactly kMaxBlockSize mono Q31 samples. */
#ifdef __ARM_NEON
  {
    const float32x4_t vgain = vdupq_n_f32(0.5f * out_gain);
    const float32x4_t vscale = vdupq_n_f32(2147483648.0f);
    const float32x4_t vmin = vdupq_n_f32(-1.0f);
    const float32x4_t vmax = vdupq_n_f32(1.0f);
    size_t i = 0;
    for (; i + 4 <= kMaxBlockSize; i += 4) {
      float32x4_t l = vld1q_f32(out_buffer_ + i);
      float32x4_t r = vld1q_f32(aux_buffer_ + i);
      float32x4_t m = vmulq_f32(vaddq_f32(l, r), vgain);
      m = vmaxq_f32(vminq_f32(m, vmax), vmin);
      int32x4_t q = vcvtq_s32_f32(vmulq_f32(m, vscale));
      vst1q_s32(yn + i, q);
    }
    for (; i < kMaxBlockSize; ++i) {
      yn[i] = f32_to_q31((out_buffer_[i] + aux_buffer_[i]) * 0.5f * out_gain);
    }
  }
#else
  for (size_t i = 0; i < kMaxBlockSize; ++i) {
    yn[i] = f32_to_q31((out_buffer_[i] + aux_buffer_[i]) * 0.5f * out_gain);
  }
#endif
}

void OSC_NOTEON(const user_osc_param_t *const params)
{
  (void)params;
  gate_ = true;
  arp_pos_   = -1; /* restart the arp sequence from the first step */
  arp_accum_ = 0;
}

void OSC_NOTEOFF(const user_osc_param_t *const params)
{
  (void)params;
  gate_ = false;
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
