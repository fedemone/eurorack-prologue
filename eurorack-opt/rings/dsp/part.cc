// Copyright 2015 Olivier Gillet.
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
// Original: eurorack/rings/dsp/part.cc at 58b9125.
// See eurorack-opt/README.md for what changed and why, and for how to re-sync
// this file if the submodule moves.  Goes with the forked part.h and string.h.
//
// Changes:
//
//  1. ConfigureResonators() keeps what is already ringing.  Upstream
//     re-initialises every string, mode set or FM voice on any Model or
//     Polyphony change, so the sound stopped dead and started again from
//     nothing.  Now a change within the string models, or a polyphony change
//     for the string and FM models, carries on from the current state; see
//     the comment on ConfigureResonators().  Part::KeepsStateFor() says in
//     advance which changes do, so the caller can bridge the ones that cannot.
//
//  2. The String + Reverb model's reverb rings out after a change to String
//     instead of stopping (Process(), at the end).
//
//  3. set_output_boost(): a caller's extra gain goes ahead of the limiter
//     (Process(), at the end), so the limiter holds it.
//
//  4. The sympathetic strings retune no faster than kSympatheticGlideMax per
//     block (RenderStringVoice()).  Upstream lets them jump almost at once;
//     on a string that is still ringing that scrubs its delay line through a
//     few hundred samples inside one 24-sample block, a chirp that measured as
//     a click on most note changes in the sympathetic models at polyphony 4.
//
//  5. Five chords added to the end of each table, and the tables sized with
//     kNumChords instead of a literal 11 -- the rest of this note.
//
//  6. A string carried across a change but due to be retuned by more than
//     kCarryRatio is cleared instead (RenderStringVoice()).  Dragged there by
//     the glide it chirps -- reading its delay line through hundreds of
//     samples in a few milliseconds -- which measured as a click 23 times the
//     surrounding programme on a Polyphony 4 -> 1 change in Sympathetic
//     Quantized.  The caller's bridge makes up the energy that is lost.
//
//  7. Struck chords: with a chord selected, every model but Sympathetic
//     Quantized strikes a group of voices at once, one chord tone each.  See
//     "Struck chords" below.  A negative chord is Off, and Off is also what
//     Sympathetic Quantized does without a chord: it tunes its sympathetic
//     strings from Structure, as Sympathetic String does.
//
// The literal was a hazard, not just a style point.  performance_state.chord
// reaches this table through Part::Process() with no bounds check between the
// panel and the subscript, so the only thing keeping the read in range is that
// the table happens to be as long as the parameter's range.  Upstream wrote
// that length twice -- once as kNumChords in performance_state.h, once as 11
// here -- and nothing tied them together.  Raising the parameter's range
// without the dimension following would index a row past the end and read
// whatever the next polyphony block holds, on the audio thread.  Naming the
// constant in the dimension is what makes the two move together.
//
// The first three additions are deliberately not more triads.  Upstream's
// eleven cover the common tonal ground already, and a resonator is an
// unusually good place to hear things that ordinary keyboards cannot play:
// every string is tuned independently and rings sympathetically, so intervals
// that are merely out of tune on a piano lock or beat audibly here.  So the
// new rows are one modern-tonal, one microtonal-consonant and one
// non-Western, and two more after them that are rare in any chord list:
//
//   Quartal (4ths)  Stacked perfect fourths rather than stacked thirds.  Has
//                   no third at all, so it reads as neither major nor minor --
//                   open and unresolved.  Distinct from the existing sus4,
//                   which is a triad with the third displaced; this is a fifth
//                   in the bass and then fourths all the way up.
//
//   Just7           A just-intoned dominant seventh: harmonics 4:5:6:7 of the
//                   root, i.e. 0, 3.86, 7.02 and 9.69 semitones.  The seventh
//                   is the septimal one, 31 cents flat of the tempered minor
//                   seventh.  Tempered, that interval beats; justly tuned, it
//                   locks into the root's overtone series and the beating
//                   stops.  On sympathetic strings the difference is not
//                   subtle -- the chord fuses into one tone.
//
//   Slendro         The Javanese gamelan pentatonic, taken as its usual
//                   approximation of five equal steps to the octave (2.4
//                   semitones each).  Every interval is foreign to twelve-tone
//                   equal temperament, which is the point: struck and left to
//                   ring it is recognisably gong-like.
//
//   Hirajoshi       The Japanese pentatonic 0-2-3-7-8, as a koto is tuned to
//   (Hirajo)        it.  Two semitone pairs, so on sympathetic strings the
//                   neighbours beat against each other; the arpeggiator walks
//                   it as the scale it is.
//
//   Mystic          Scriabin's "Prometheus" chord, C F# Bb E A D: fourths,
//                   but a tritone, a diminished and an augmented fourth among
//                   them, so it is whole-tone coloured and never resolves.
//                   Distinct from 4ths, whose fourths are all perfect.
//
// Only Just7 and Slendro need the table's float type to mean anything -- the
// fractional entries upstream already has are detunings of a semitone or two,
// used to make near-unison strings beat, whereas these are the actual tuning.
// That has a consequence outside this file: the arpeggiator in
// rings-resonator.cc walks the same intervals and had an integer table, which
// would have put the arp a third of a semitone out against the strings it is
// strumming.  It is float there now for the same reason.
//
// -----------------------------------------------------------------------------
//
// Group of voices.

#include "rings/dsp/part.h"

#include "stmlib/dsp/units.h"

#include "rings/resources.h"

namespace rings {

using namespace std;
using namespace stmlib;

namespace {

// FORK: how long the String + Reverb model takes to let its reverb's input
// in when it is switched to with something ringing: 30 ms.
const int32_t kReverbFadeInSamples = 1440;

}  // namespace

void Part::Init(uint16_t* reverb_buffer) {
  active_voice_ = 0;
  
  fill(&note_[0], &note_[kMaxPolyphony], 0.0f);
  
  bypass_ = false;
  polyphony_ = 1;
  model_ = RESONATOR_MODEL_MODAL;
  dirty_ = true;
  configured_ = false;
  configured_model_ = model_;
  configured_polyphony_ = polyphony_;
  reverb_tail_ = 0;
  reverb_stale_ = false;
  reverb_fade_in_ = kReverbFadeInSamples;
  output_boost_ = 1.0f;
  requested_polyphony_ = 1;
  chord_enabled_ = false;
  groups_ = 1;
  group_size_ = 1;
  fill(&chord_offset_[0], &chord_offset_[kMaxPolyphony], 0.0f);
  carry_check_ = false;
  
  for (int32_t i = 0; i < kMaxPolyphony; ++i) {
    excitation_filter_[i].Init();
    plucker_[i].Init();
    dc_blocker_[i].Init(1.0f - 10.0f / kSampleRate);
  }
  
  reverb_.Init(reverb_buffer);
  limiter_.Init();

  note_filter_.Init(
      kSampleRate / kMaxBlockSize,
      0.001f,  // Lag time with a sharp edge on the V/Oct input or trigger.
      0.010f,  // Lag time after the trigger has been received.
      0.050f,  // Time to transition from reactive to filtered.
      0.004f); // Prevent a sharp edge to partly leak on the previous voice.
}

namespace {

// FORK: the three engines Part switches between.  State only carries across
// a change inside one of them.
enum ResonatorFamily { FAMILY_MODAL, FAMILY_STRING, FAMILY_FM };

inline ResonatorFamily family_of(ResonatorModel model) {
  return model == RESONATOR_MODEL_MODAL ? FAMILY_MODAL
      : (model == RESONATOR_MODEL_FM_VOICE ? FAMILY_FM : FAMILY_STRING);
}

// How many of string_[] RenderStringVoice() runs.  Voice v uses strings
// v + k * polyphony for k < num_strings, so the set is always a prefix: all
// eight for the sympathetic models (num_strings = 8 / polyphony), the first
// `polyphony` for the others (num_strings = 1).
inline int32_t rendered_strings(ResonatorModel model, int32_t polyphony) {
  return (model == RESONATOR_MODEL_SYMPATHETIC_STRING ||
          model == RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED)
      ? kNumStrings : polyphony;
}

// The String + Reverb tail: four seconds, of which the last one fades the
// reverb time and the level to zero, so what is left in the reverb at the end
// is negligible rather than frozen.
const int32_t kReverbTailSamples = 4 * 48000;
const int32_t kReverbFadeSamples = 48000;

// Fastest a sympathetic string may retune, as the per-block coefficient of
// String::set_frequency(): 2.2 ms time constant, settled within about 7 ms.
// Upstream's glide here is SemitonesToRatio((brightness - 1) * 36), about
// 0.88 at default Brightness -- a near-instant jump -- so this only caps the
// bright end; darker settings were already slower and are left alone.
//
// It is the sympathetic strings that need it.  They ring longest (damping
// 0.7 + 0.27 * Damping) and they are the ones retuned while still sounding:
// at polyphony 4 every new note lands on a voice whose strings are ringing a
// note or chord from four notes ago.  Each retune then reads its delay line
// through a few hundred samples within one block -- a chirp.  Measured on the
// ARM build, with a note change every 250 ms that retuned without striking
// (as LFO -> Note and Chord changes do, and legato note-ons did until the
// wrapper made every note-on strike; a fresh strike partly masks it)
// through the reverb and the compressor, this took the clicks found at note
// changes from 15 to none in Sympathetic Quantized at polyphony 4 and from 7
// to none in Sympathetic at polyphony 1.  The main string is struck as it
// retunes, which hides its own jump; gliding it as well measured no better.  A side effect: the arpeggiator's steps,
// which were partly smeared by those chirps, now read at the right pitch on
// every step in both models.
const float kSympatheticGlideMax = 0.2f;

// FORK: furthest a string carried across a reconfiguration may be retuned
// rather than cleared: two semitones, as a frequency ratio.  Within that the
// glide is a short portamento; past it, see the note at the top of the file.
const float kCarryRatio = 1.122462f;

// FORK: struck chords -- the intervals the voices of a group are tuned to,
// one row per chord, root first.  A group of two takes the first two, so the
// second entry is each chord's defining interval rather than its next tone
// up: the seventh of min7, the ninth of min9, the septimal seventh of Just7.
// Chords of fewer than four tones double one at the octave.  Upstream's
// sympathetic voicings put the struck string an octave below the root and
// spread six or seven others over two octaves; four struck voices cannot do
// that and stay a chord, so these are closer, and the five-tone chords (the
// ninths, the eleventh, 6/9) leave out the fifth, as a pianist would.
const float kStruckChords[kNumChords][kMaxPolyphony] = {
  { 0.0f, 12.0f,  24.0f, -12.0f },  // Oct
  { 0.0f,  7.0f,  12.0f,  19.0f },  // 5th
  { 0.0f,  5.0f,   7.0f,  12.0f },  // sus4
  { 0.0f,  3.0f,   7.0f,  12.0f },  // min
  { 0.0f, 10.0f,   3.0f,   7.0f },  // min7
  { 0.0f, 14.0f,   3.0f,  10.0f },  // min9
  { 0.0f, 17.0f,   3.0f,  10.0f },  // min11
  { 0.0f,  9.0f,   4.0f,  14.0f },  // 69
  { 0.0f, 14.0f,   4.0f,  11.0f },  // Maj9
  { 0.0f, 11.0f,   4.0f,   7.0f },  // Maj7
  { 0.0f,  4.0f,   7.0f,  12.0f },  // Maj
  { 0.0f,  5.0f,  10.0f,  15.0f },  // 4ths
  { 0.0f,  9.69f,  3.86f,  7.02f }, // Just7
  { 0.0f,  7.2f,   2.4f,   4.8f },  // Slendro
  { 0.0f,  8.0f,   2.0f,   7.0f },  // Hirajoshi
  { 0.0f,  6.0f,  10.0f,  16.0f },  // Mystic
};

// FORK: how a polyphony and a chord on/off become groups.  A group takes as
// many voices as the polyphony leaves spare out of kMaxPolyphony, so a chord
// is four voices at Polyphony 1, two at Polyphony 2, and nothing at 3 or 4,
// where every voice already carries a note of its own.  The voices rendered
// never exceed kMaxPolyphony, so a chord costs no more than Polyphony 4 of
// the same model already does.  Sympathetic Quantized plays chords on its
// sympathetic strings instead, and is laid out exactly as without one.
inline void chord_layout(ResonatorModel model, int32_t polyphony,
                         bool chord_enabled, int32_t* voices,
                         int32_t* group_size) {
  int32_t size = 1;
  if (chord_enabled && model != RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED) {
    size = kMaxPolyphony / polyphony;
  }
  *voices = polyphony * size;
  *group_size = size;
}

}  // namespace

bool Part::KeepsStateFor(ResonatorModel model, int32_t polyphony,
                         bool chord_enabled) const {
  if (!configured_) {
    return true;  // Nothing has rendered since Init(): nothing to keep.
  }
  if (family_of(model) != family_of(configured_model_)) {
    return false;
  }
  // A modal resonator keeps its modes' state privately, and the modes past a
  // lower resolution would freeze rather than decay -- to come back, ringing,
  // the next time the resolution went up.  So a modal change in the number
  // of voices rendered (which is a resolution change) starts from silence.
  int32_t voices, group_size;
  chord_layout(model,
               std::max(std::min(polyphony, kMaxPolyphony),
                        static_cast<int32_t>(1)),
               chord_enabled, &voices, &group_size);
  return family_of(model) != FAMILY_MODAL || voices == configured_polyphony_;
}

// FORK: keep what is already ringing.
//
// Upstream re-initialised everything here on any change: all eight strings
// (clearing ~96 KB of delay line), or every voice's 64-mode resonator, or every
// FM voice.  Within a family that is not necessary:
//
//  - Strings.  The four string models share string_[] and differ only in how
//    many strings each voice runs, how they are tuned, and whether dispersion
//    is on.  A string rendered both before and after the change keeps its
//    delay line and is retuned to its new voice and model like any note
//    change.  A string that starts being rendered, or stops, is cleared: one
//    that starts would otherwise resume whatever it held when it was last
//    rendered, perhaps long ago, and one that stops would hold its sound
//    frozen until then.
//  - FM voices.  Existing voices carry on; voices new to the polyphony start
//    clean, for the same reason.
//  - Modal.  See KeepsStateFor().
//
// A change between families starts from silence as before;
// KeepsStateFor() is how the caller finds out beforehand.
void Part::ConfigureResonators() {
  if (!dirty_) {
    return;
  }

  const bool keep = configured_ &&
      KeepsStateFor(model_, requested_polyphony_, chord_enabled_);
  const int32_t old_polyphony = keep ? configured_polyphony_ : 0;

  // Struck chords: how many voices to render, in groups of how many.  New
  // voices take their group's note -- they are silent until struck, and a
  // strike sets it anyway -- which is upstream's set_polyphony() set-up,
  // moved here because this is where the voice count is now decided.
  {
    int32_t voices, group_size;
    chord_layout(model_, requested_polyphony_, chord_enabled_,
                 &voices, &group_size);
    const int32_t previous = configured_ ? configured_polyphony_ : 1;
    polyphony_ = voices;
    group_size_ = group_size;
    groups_ = voices / group_size;
    for (int32_t i = previous; i < polyphony_; ++i) {
      note_[i] = note_[i % groups_ < previous ? i % groups_ : 0];
    }
  }

  switch (model_) {
    case RESONATOR_MODEL_MODAL:
      {
        int32_t resolution = 64 / polyphony_ - 4;
        for (int32_t i = 0; i < polyphony_; ++i) {
          if (!keep) {
            resonator_[i].Init();
          }
          resonator_[i].set_resolution(resolution);
        }
      }
      break;
    
    case RESONATOR_MODEL_SYMPATHETIC_STRING:
    case RESONATOR_MODEL_STRING:
    case RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED:
    case RESONATOR_MODEL_STRING_AND_REVERB:
      {
        float lfo_frequencies[kNumStrings] = {
          0.5f, 0.4f, 0.35f, 0.23f, 0.211f, 0.2f, 0.171f
        };
        bool has_dispersion = model_ == RESONATOR_MODEL_STRING || \
            model_ == RESONATOR_MODEL_STRING_AND_REVERB;
        int32_t was = keep
            ? rendered_strings(configured_model_, configured_polyphony_) : 0;
        int32_t now = rendered_strings(model_, polyphony_);
        for (int32_t i = 0; i < kNumStrings; ++i) {
          if (i < was && i < now) {
            string_[i].set_dispersion_enabled(has_dispersion);
          } else {
            string_[i].Init(has_dispersion);
          }
          if (!keep) {
            float f_lfo = float(kMaxBlockSize) / float(kSampleRate);
            f_lfo *= lfo_frequencies[i];
            lfo_[i].Init<COSINE_OSCILLATOR_APPROXIMATE>(f_lfo);
          }
        }
        for (int32_t i = old_polyphony; i < polyphony_; ++i) {
          plucker_[i].Init();
        }
      }
      break;
    
    case RESONATOR_MODEL_FM_VOICE:
      {
        for (int32_t i = old_polyphony; i < polyphony_; ++i) {
          fm_voice_[i].Init();
        }
      }
      break;
    
    default:
      break;
  }

  // The String + Reverb model's reverb.  Leaving it for String, whose strings
  // carry on, it rings out too (see the end of Process()): String plus the
  // reverb costs what String + Reverb itself did, about 17k instructions per
  // render more than String alone.  Into the other models it would raise the
  // unit's load above anything it otherwise reaches -- a sympathetic model at
  // polyphony 1 to ~109k, against 93k at most -- for four seconds, so there
  // its contents freeze (the caller's crossfade fades what was heard of them)
  // and are cleared before the String + Reverb model is next heard.
  const bool leaving_reverb = configured_ &&
      configured_model_ == RESONATOR_MODEL_STRING_AND_REVERB &&
      model_ != RESONATOR_MODEL_STRING_AND_REVERB;
  if (model_ == RESONATOR_MODEL_STRING_AND_REVERB) {
    if (reverb_stale_) {
      reverb_.Clear();
      reverb_stale_ = false;
    }
    // Entered from another model with the reverb silent: let its input in
    // gradually, see reverb_fade_in_.  Not when the reverb is still ringing
    // out from the last visit, which it then simply carries on from.
    if (configured_ && configured_model_ != RESONATOR_MODEL_STRING_AND_REVERB &&
        reverb_tail_ <= 0) {
      reverb_fade_in_ = 0;
    }
    reverb_tail_ = 0;
  } else if (leaving_reverb && model_ == RESONATOR_MODEL_STRING) {
    reverb_tail_ = kReverbTailSamples;
  } else if (leaving_reverb || (reverb_tail_ > 0 && model_ != RESONATOR_MODEL_STRING)) {
    reverb_stale_ = true;
    reverb_tail_ = 0;
  }

  if (active_voice_ >= groups_) {
    active_voice_ = 0;
  }
  carry_check_ = keep && family_of(model_) == FAMILY_STRING;
  configured_model_ = model_;
  configured_polyphony_ = polyphony_;
  configured_ = true;
  dirty_ = false;
}

#ifdef BRYAN_CHORDS

// Chord table by Bryan Noll:
float chords[kMaxPolyphony][kNumChords][8] = {
  {
    { -12.0f, -0.01f, 0.0f,  0.01f, 0.02f, 11.98f, 11.99f, 12.0f }, // OCT
    { -12.0f, -5.0f,  0.0f,  6.99f, 7.0f,  11.99f, 12.0f,  19.0f }, // 5
    { -12.0f, -5.0f,  0.0f,  5.0f,  7.0f,  11.99f, 12.0f,  17.0f }, // sus4
    { -12.0f, -5.0f,  0.0f,  3.0f,  7.0f,   3.01f, 12.0f,  19.0f }, // m 
    { -12.0f, -5.0f,  0.0f,  3.0f,  7.0f,   3.01f, 10.0f,  19.0f }, // m7
    { -12.0f, -5.0f,  0.0f,  3.0f, 14.0f,   3.01f, 10.0f,  19.0f }, // m9
    { -12.0f, -5.0f,  0.0f,  3.0f,  7.0f,   3.01f, 10.0f,  17.0f }, // m11
    { -12.0f, -5.0f,  0.0f,  2.0f,  7.0f,   9.0f,  16.0f,  19.0f }, // 69
    { -12.0f, -5.0f,  0.0f,  4.0f,  7.0f,  11.0f,  14.0f,  19.0f }, // M9
    { -12.0f, -5.0f,  0.0f,  4.0f,  7.0f,  11.0f,  10.99f, 19.0f }, // M7
    { -12.0f, -5.0f,  0.0f,  4.0f,  7.0f,  11.99f, 12.0f,  19.0f }, // M
    // Added for the drumlogue port -- see the fork note at the top.
    { -12.0f, -5.0f,  0.0f,  5.0f, 10.0f,  15.0f,  20.0f,  20.01f }, // 4ths
    { -12.0f, -4.98f, 0.0f,  3.86f, 7.02f,  9.69f, 12.0f,  15.86f }, // Just7
    { -12.0f, -4.8f,  0.0f,  2.4f,  4.8f,   7.2f,   9.6f,  12.0f },  // Slendro
    { -12.0f, -5.0f,  0.0f,  2.0f,  3.0f,   7.0f,   8.0f,  12.0f },  // Hirajoshi
    { -12.0f, -6.0f,  0.0f,  6.0f, 10.0f,  14.0f,  16.0f,  21.0f }   // Mystic
  },
  {
    { -12.0f, 0.0f,  0.01f, 12.0f }, // OCT
    { -12.0f, 6.99f, 7.0f,  12.0f }, // 5
    { -12.0f, 5.0f,  7.0f,  12.0f }, // sus4
    { -12.0f, 3.0f, 11.99f, 12.0f }, // m 
    { -12.0f, 3.0f, 10.0f,  12.0f }, // m7
    { -12.0f, 3.0f, 10.0f,  14.0f }, // m9
    { -12.0f, 3.0f, 10.0f,  17.0f }, // m11
    { -12.0f, 2.0f,  9.0f,  16.0f }, // 69
    { -12.0f, 4.0f, 11.0f,  14.0f }, // M9
    { -12.0f, 4.0f,  7.0f,  11.0f }, // M7
    { -12.0f, 4.0f,  7.0f,  12.0f }, // M
    // Added for the drumlogue port -- see the fork note at the top.
    { -12.0f, 5.0f,   10.0f, 15.0f }, // 4ths
    { -12.0f, 3.86f,  7.02f,  9.69f }, // Just7
    { -12.0f, 2.4f,   7.2f,   9.6f },  // Slendro
    { -12.0f, 2.0f,   7.0f,   8.0f },  // Hirajoshi
    { -12.0f, 6.0f,  10.0f,  16.0f },  // Mystic
  },
  {
    { 0.0f, -12.0f },
    { 0.0f, 2.0f },
    { 0.0f, 3.0f },
    { 0.0f, 4.0f },
    { 0.0f, 5.0f },
    { 0.0f, 7.0f },
    { 0.0f, 9.0f },
    { 0.0f, 10.0f },
    { 0.0f, 11.0f },
    { 0.0f, 12.0f },
    { -12.0f, 12.0f },
    // Added for the drumlogue port -- see the fork note at the top.  Two
    // strings is not enough to state a chord, so these are the defining
    // interval of each: the fourth, the septimal seventh, the slendro fifth,
    // hirajoshi's minor sixth over the root, the mystic chord's tritone.
    { 0.0f, 5.0f },
    { 0.0f, 9.69f },
    { 0.0f, 7.2f },
    { 0.0f, 8.0f },
    { 0.0f, 6.0f }
  },
  {
    { 0.0f, -12.0f },
    { 0.0f, 2.0f },
    { 0.0f, 3.0f },
    { 0.0f, 4.0f },
    { 0.0f, 5.0f },
    { 0.0f, 7.0f },
    { 0.0f, 9.0f },
    { 0.0f, 10.0f },
    { 0.0f, 11.0f },
    { 0.0f, 12.0f },
    { -12.0f, 12.0f },
    // Added for the drumlogue port -- see the fork note at the top.  Two
    // strings is not enough to state a chord, so these are the defining
    // interval of each: the fourth, the septimal seventh, the slendro fifth,
    // hirajoshi's minor sixth over the root, the mystic chord's tritone.
    { 0.0f, 5.0f },
    { 0.0f, 9.69f },
    { 0.0f, 7.2f },
    { 0.0f, 8.0f },
    { 0.0f, 6.0f }
  }
};

#else

// Original chord table
float chords[kMaxPolyphony][kNumChords][8] = {
  {
    { -12.0f, 0.0f, 0.01f, 0.02f, 0.03f, 11.98f, 11.99f, 12.0f },
    { -12.0f, 0.0f, 3.0f,  3.01f, 7.0f,  9.99f,  10.0f,  19.0f },
    { -12.0f, 0.0f, 3.0f,  3.01f, 7.0f,  11.99f, 12.0f,  19.0f },
    { -12.0f, 0.0f, 3.0f,  3.01f, 7.0f,  13.99f, 14.0f,  19.0f },
    { -12.0f, 0.0f, 3.0f,  3.01f, 7.0f,  16.99f, 17.0f,  19.0f },
    { -12.0f, 0.0f, 6.98f, 6.99f, 7.0f,  12.00f, 18.99f, 19.0f },
    { -12.0f, 0.0f, 3.99f, 4.0f,  7.0f,  16.99f, 17.0f,  19.0f },
    { -12.0f, 0.0f, 3.99f, 4.0f,  7.0f,  13.99f, 14.0f,  19.0f },
    { -12.0f, 0.0f, 3.99f, 4.0f,  7.0f,  11.99f, 12.0f,  19.0f },
    { -12.0f, 0.0f, 3.99f, 4.0f,  7.0f,  10.99f, 11.0f,  19.0f },
    { -12.0f, 0.0f, 4.99f, 5.0f,  7.0f,  11.99f, 12.0f,  17.0f },
    // Added for the drumlogue port -- see the fork note at the top.  This
    // table is unreachable (rings/dsp/dsp.h defines BRYAN_CHORDS
    // unconditionally), but it is dimensioned by kNumChords like the other, so
    // leaving it short would be an out-of-bounds read rather than a fallback.
    { -12.0f, -5.0f,  0.0f,  5.0f, 10.0f,  15.0f,  20.0f,  20.01f },
    { -12.0f, -4.98f, 0.0f,  3.86f, 7.02f,  9.69f, 12.0f,  15.86f },
    { -12.0f, -4.8f,  0.0f,  2.4f,  4.8f,   7.2f,   9.6f,  12.0f },
    { -12.0f, -5.0f,  0.0f,  2.0f,  3.0f,   7.0f,   8.0f,  12.0f },
    { -12.0f, -6.0f,  0.0f,  6.0f, 10.0f,  14.0f,  16.0f,  21.0f }
  },
  { 
    { -12.0f, 0.0f, 0.01f, 12.0f },
    { -12.0f, 3.0f, 7.0f,  10.0f },
    { -12.0f, 3.0f, 7.0f,  12.0f },
    { -12.0f, 3.0f, 7.0f,  14.0f },
    { -12.0f, 3.0f, 7.0f,  17.0f },
    { -12.0f, 7.0f, 12.0f, 19.0f },
    { -12.0f, 4.0f, 7.0f,  17.0f },
    { -12.0f, 4.0f, 7.0f,  14.0f },
    { -12.0f, 4.0f, 7.0f,  12.0f },
    { -12.0f, 4.0f, 7.0f,  11.0f },
    { -12.0f, 5.0f, 7.0f,  12.0f },
    { -12.0f, 5.0f,   10.0f, 15.0f },
    { -12.0f, 3.86f,  7.02f,  9.69f },
    { -12.0f, 2.4f,   7.2f,   9.6f },
    { -12.0f, 2.0f,   7.0f,   8.0f },
    { -12.0f, 6.0f,  10.0f,  16.0f },
  },
  {
    { 0.0f, -12.0f },
    { 0.0f, 0.01f },
    { 0.0f, 2.0f },
    { 0.0f, 3.0f },
    { 0.0f, 4.0f },
    { 0.0f, 5.0f },
    { 0.0f, 7.0f },
    { 0.0f, 10.0f },
    { 0.0f, 11.0f },
    { 0.0f, 12.0f },
    { -12.0f, 12.0f },
    { 0.0f, 5.0f },
    { 0.0f, 9.69f },
    { 0.0f, 7.2f },
    { 0.0f, 8.0f },
    { 0.0f, 6.0f }
  },
  {
    { 0.0f, -12.0f },
    { 0.0f, 0.01f },
    { 0.0f, 2.0f },
    { 0.0f, 3.0f },
    { 0.0f, 4.0f },
    { 0.0f, 5.0f },
    { 0.0f, 7.0f },
    { 0.0f, 10.0f },
    { 0.0f, 11.0f },
    { 0.0f, 12.0f },
    { -12.0f, 12.0f },
    { 0.0f, 5.0f },
    { 0.0f, 9.69f },
    { 0.0f, 7.2f },
    { 0.0f, 8.0f },
    { 0.0f, 6.0f }
  }
};

#endif  // BRYAN_CHORDS

void Part::ComputeSympatheticStringsNotes(
    float tonic,
    float note,
    float parameter,
    float* destination,
    size_t num_strings) {
  float notes[9] = {
      tonic,
      note - 12.0f,
      note - 7.01955f,
      note,
      note + 7.01955f,
      note + 12.0f,
      note + 19.01955f,
      note + 24.0f,
      note + 24.0f };
  const float detunings[4] = {
      0.013f,
      0.011f,
      0.007f,
      0.017f
  };
  
  if (parameter >= 2.0f) {
    // Quantized chords
    int32_t chord_index = parameter - 2.0f;
    const float* chord = chords[polyphony_ - 1][chord_index];
    for (size_t i = 0; i < num_strings; ++i) {
      destination[i] = chord[i] + note;
    }
    return;
  }

  size_t num_detuned_strings = (num_strings - 1) >> 1;
  size_t first_detuned_string = num_strings - num_detuned_strings;
  
  for (size_t i = 0; i < first_detuned_string; ++i) {
    float note = 3.0f;
    if (i != 0) {
      note = parameter * 7.0f;
      parameter += (1.0f - parameter) * 0.2f;
    }
    
    MAKE_INTEGRAL_FRACTIONAL(note);
    note_fractional = Squash(note_fractional);

    float a = notes[note_integral];
    float b = notes[note_integral + 1];
    
    note = a + (b - a) * note_fractional;
    destination[i] = note;
    if (i + first_detuned_string < num_strings) {
      destination[i + first_detuned_string] = destination[i] + detunings[i & 3];
    }
  }
}

void Part::RenderModalVoice(
    int32_t voice,
    const PerformanceState& performance_state,
    const Patch& patch,
    float frequency,
    float filter_cutoff,
    size_t size) {
  // Internal exciter is a pulse, pre-filter.
  if (performance_state.internal_exciter &&
      is_active(voice) &&
      performance_state.strum) {
    resonator_input_[0] += 0.25f * SemitonesToRatio(
        filter_cutoff * filter_cutoff * 24.0f) / filter_cutoff;
  }
  
  // Process through filter.
  excitation_filter_[voice].Process<FILTER_MODE_LOW_PASS>(
      resonator_input_, resonator_input_, size);

  Resonator& r = resonator_[voice];
  r.set_frequency(frequency);
  r.set_structure(patch.structure);
  r.set_brightness(patch.brightness * patch.brightness);
  r.set_position(patch.position);
  r.set_damping(patch.damping);
  r.Process(resonator_input_, out_buffer_, aux_buffer_, size);
}

void Part::RenderFMVoice(
    int32_t voice,
    const PerformanceState& performance_state,
    const Patch& patch,
    float frequency,
    float filter_cutoff,
    size_t size) {
  FMVoice& v = fm_voice_[voice];
  if (performance_state.internal_exciter &&
      is_active(voice) &&
      performance_state.strum) {
    v.TriggerInternalEnvelope();
  }

  v.set_frequency(frequency);
  v.set_ratio(patch.structure);
  v.set_brightness(patch.brightness);
  v.set_feedback_amount(patch.position);
  v.set_position(/*patch.position*/ 0.0f);
  v.set_damping(patch.damping);
  v.Process(resonator_input_, out_buffer_, aux_buffer_, size);
}

void Part::RenderStringVoice(
    int32_t voice,
    const PerformanceState& performance_state,
    const Patch& patch,
    float frequency,
    float filter_cutoff,
    size_t size) {
  // Compute number of strings and frequency.
  int32_t num_strings = 1;
  float frequencies[kNumStrings];

  if (model_ == RESONATOR_MODEL_SYMPATHETIC_STRING ||
      model_ == RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED) {
    num_strings = 2 * kMaxPolyphony / polyphony_;
    // FORK: with Chord off (negative) the quantized model has no chord to
    // tune to, and tunes from Structure like the unquantized one.
    float parameter = (model_ == RESONATOR_MODEL_SYMPATHETIC_STRING ||
                       performance_state.chord < 0)
        ? patch.structure
        : 2.0f + min(performance_state.chord, kNumChords - 1);
    ComputeSympatheticStringsNotes(
        performance_state.tonic + performance_state.fm,
        performance_state.tonic + voice_note(voice) + performance_state.fm,
        parameter,
        frequencies,
        num_strings);
    for (int32_t i = 0; i < num_strings; ++i) {
      frequencies[i] = SemitonesToRatio(frequencies[i] - 69.0f) * a3;
    }
  } else {
    frequencies[0] = frequency;
  }

  const bool active = is_active(voice);
  if (active) {
    const float gain = 1.0f / Sqrt(static_cast<float>(num_strings) * 2.0f);
    for (size_t i = 0; i < size; ++i) {
      resonator_input_[i] *= gain;
    }
  }

  // Process external input.
  excitation_filter_[voice].Process<FILTER_MODE_LOW_PASS>(
      resonator_input_, resonator_input_, size);

  // Add noise burst.
  if (performance_state.internal_exciter) {
    if (active && performance_state.strum) {
      plucker_[voice].Trigger(frequency, filter_cutoff * 8.0f, patch.position);
    }
    plucker_[voice].Process(noise_burst_buffer_, size);
    for (size_t i = 0; i < size; ++i) {
      resonator_input_[i] += noise_burst_buffer_[i];
    }
  }
  dc_blocker_[voice].Process(resonator_input_, size);
  
  fill(&out_buffer_[0], &out_buffer_[size], 0.0f);
  fill(&aux_buffer_[0], &aux_buffer_[size], 0.0f);
  
  float structure = patch.structure;
  float dispersion = structure < 0.24f
      ? (structure - 0.24f) * 4.166f
      : (structure > 0.26f ? (structure - 0.26f) * 1.35135f : 0.0f);
  
  for (int32_t string = 0; string < num_strings; ++string) {
    int32_t i = voice + string * polyphony_;
    String& s = string_[i];
    float lfo_value = lfo_[i].Next();
    
    float brightness = patch.brightness;
    float damping = patch.damping;
    float position = patch.position;
    // FORK: in a struck chord the main strings are retuned while they ring,
    // whenever the chord changes, so they glide like sympathetic strings do
    // -- except on the block that strikes them, where the strike covers it.
    float glide = (group_size_ > 1 && !(active && performance_state.strum))
        ? kSympatheticGlideMax : 1.0f;
    float string_index = static_cast<float>(string) / static_cast<float>(num_strings);
    const float* input = resonator_input_;
    
    if (model_ == RESONATOR_MODEL_STRING_AND_REVERB) {
      damping *= (2.0f - damping);
    }
    
    // When the internal exciter is used, string 0 is the main
    // source, the other strings are vibrating by sympathetic resonance.
    // When the internal exciter is not used, all strings are vibrating
    // by sympathetic resonance.
    if (string > 0 && performance_state.internal_exciter) {
      brightness *= (2.0f - brightness);
      brightness *= (2.0f - brightness);
      damping = 0.7f + patch.damping * 0.27f;
      float amount = (0.5f - fabs(0.5f - patch.position)) * 0.9f;
      position = patch.position + lfo_value * amount;
      glide = min(SemitonesToRatio((brightness - 1.0f) * 36.0f),
                  kSympatheticGlideMax);
      input = sympathetic_resonator_input_;
    }
    
    // FORK: a string carried across a reconfiguration that would now have to
    // travel further than kCarryRatio is cleared and tuned outright; see the
    // note at the top of the file.  A string the reconfiguration has just
    // initialised lands here too, and is tuned outright instead of gliding up
    // from Init()'s 220 Hz.  Not the strings of a voice this block strikes:
    // the strike retunes them anyway, and covers the jump, as it always has.
    // Checked, they were measured against the new note's tuning rather than
    // the old one's, so a note arriving with a Model change cleared the
    // sympathetic strings it would have set ringing -- 8 dB down on the
    // struck note.
    const bool struck_now = active && performance_state.strum;
    if (carry_check_ && !struck_now) {
      const float ratio = frequencies[string] / s.frequency();
      if (ratio > kCarryRatio || ratio < 1.0f / kCarryRatio) {
        s.Init(model_ == RESONATOR_MODEL_STRING ||
               model_ == RESONATOR_MODEL_STRING_AND_REVERB);
        glide = 1.0f;
      }
    }

    s.set_dispersion(dispersion);
    s.set_frequency(frequencies[string], glide);
    s.set_brightness(brightness);
    s.set_position(position);
    s.set_damping(damping + string_index * (0.95f - damping));
    s.Process(input, out_buffer_, aux_buffer_, size);
    
    if (string == 0) {
      // Was 0.1f, Ben Wilson -> 0.2f
      float gain = 0.2f / static_cast<float>(num_strings);
      for (size_t i = 0; i < size; ++i) {
        float sum = out_buffer_[i] - aux_buffer_[i];
        sympathetic_resonator_input_[i] = gain * sum;
      }
    }
  }
}

const int32_t kPingPattern[] = {
  1, 0, 2, 1, 0, 2, 1, 0
};

void Part::Process(
    const PerformanceState& performance_state,
    const Patch& patch,
    const float* in,
    float* out,
    float* aux,
    size_t size) {

  // Copy inputs to outputs when bypass mode is enabled.
  if (bypass_) {
    copy(&in[0], &in[size], &out[0]);
    copy(&in[0], &in[size], &aux[0]);
    return;
  }
  
  ConfigureResonators();
  
  note_filter_.Process(
      performance_state.note,
      performance_state.strum);

  // FORK: voices are allocated by group, which is a voice of its own unless
  // a chord is struck; see "Struck chords".
  if (performance_state.strum) {
    for (int32_t v = active_voice_; v < polyphony_; v += groups_) {
      note_[v] = note_filter_.stable_note();
    }
    if (groups_ > 1 && groups_ & 1) {
      active_voice_ = kPingPattern[step_counter_ % 8];
      step_counter_ = (step_counter_ + 1) % 8;
    } else {
      active_voice_ = (active_voice_ + 1) % groups_;
    }
  }
  
  for (int32_t v = active_voice_; v < polyphony_; v += groups_) {
    note_[v] = note_filter_.note();
  }

  // FORK: the chord tones for this block.  They apply to every group, not
  // only the one struck last, so a chord change retunes what is ringing --
  // as it does the sympathetic strings of Sympathetic Quantized.
  if (group_size_ > 1 && performance_state.chord >= 0) {
    const float* chord = kStruckChords[min(performance_state.chord,
                                           kNumChords - 1)];
    for (int32_t k = 0; k < group_size_; ++k) {
      chord_offset_[k] = chord[k];
    }
  } else {
    fill(&chord_offset_[0], &chord_offset_[kMaxPolyphony], 0.0f);
  }
  // FORK: a struck chord is louder than one note of it, but not by its
  // number of voices: each is scaled by group_size_ to the -1/4, half the
  // equal-power compensation.  Measured against the same note without a
  // chord, that puts min7 at +3 dB on Modal and -2 to -5 dB on String, FM
  // and String + Reverb.  Sympathetic String comes out lower still, because
  // a voice of a struck chord has one sympathetic string instead of seven.
  const float voice_gain = group_size_ > 1
      ? 1.0f / Sqrt(Sqrt(static_cast<float>(group_size_))) : 1.0f;
  
  fill(&out[0], &out[size], 0.0f);
  fill(&aux[0], &aux[size], 0.0f);
  for (int32_t voice = 0; voice < polyphony_; ++voice) {
    // Compute MIDI note value, frequency, and cutoff frequency for excitation
    // filter.
    float cutoff = patch.brightness * (2.0f - patch.brightness);
    const bool active = is_active(voice);
    float note = voice_note(voice) + performance_state.tonic + performance_state.fm;
    float frequency = SemitonesToRatio(note - 69.0f) * a3;
    float filter_cutoff_range = performance_state.internal_exciter
      ? frequency * SemitonesToRatio((cutoff - 0.5f) * 96.0f)
      : 0.4f * SemitonesToRatio((cutoff - 1.0f) * 108.0f);
    float filter_cutoff = min(active
      ? filter_cutoff_range
      : (10.0f / kSampleRate), 0.499f);
    float filter_q = performance_state.internal_exciter ? 1.5f : 0.8f;

    // Process input with excitation filter. Inactive voices receive silence.
    excitation_filter_[voice].set_f_q<FREQUENCY_DIRTY>(filter_cutoff, filter_q);
    if (active) {
      copy(&in[0], &in[size], &resonator_input_[0]);
    } else {
      fill(&resonator_input_[0], &resonator_input_[size], 0.0f);
    }
    
    if (model_ == RESONATOR_MODEL_MODAL) {
      RenderModalVoice(
          voice, performance_state, patch, frequency, filter_cutoff, size);
    } else if (model_ == RESONATOR_MODEL_FM_VOICE) {
      RenderFMVoice(
          voice, performance_state, patch, frequency, filter_cutoff, size);
    } else {
      RenderStringVoice(
          voice, performance_state, patch, frequency, filter_cutoff, size);
    }
    
    if (polyphony_ == 1) {
      // Send the two sets of harmonics / pickups to individual outputs.
      for (size_t i = 0; i < size; ++i) {
        out[i] += out_buffer_[i];
        aux[i] += aux_buffer_[i];
      }
    } else {
      // Dispatch odd/even voices to individual outputs.
      float* destination = voice & 1 ? aux : out;
      if (group_size_ > 1) {
        // FORK: a struck chord's voices are summed as one voice at
        // Polyphony 1 is -- both pickups added -- not as upstream sums a
        // polyphonic voice, with the second pickup subtracted.  On a string
        // the two pickups are close to in phase, so the difference cancels:
        // measured, a struck chord on String came out 10.7 dB below the same
        // note without one, and Sympathetic String 14.3 dB.
        for (size_t i = 0; i < size; ++i) {
          destination[i] += (out_buffer_[i] + aux_buffer_[i]) * voice_gain;
        }
      } else {
        for (size_t i = 0; i < size; ++i) {
          destination[i] += out_buffer_[i] - aux_buffer_[i];
        }
      }
    }
  }
  carry_check_ = false;
  
  if (model_ == RESONATOR_MODEL_STRING_AND_REVERB) {
    for (size_t i = 0; i < size; ++i) {
      float l = out[i];
      float r = aux[i];
      out[i] = l * patch.position + (1.0f - patch.position) * r;
      aux[i] = r * patch.position + (1.0f - patch.position) * l;
    }
    reverb_.set_amount(0.1f + patch.damping * 0.5f);
    reverb_.set_diffusion(0.625f);
    reverb_.set_time(0.35f + 0.63f * patch.damping);
    // FORK: entered with a note ringing, the reverb's input used to step from
    // nothing to the whole string between two samples -- a step its delay
    // taps then played back as a tick about 48 ms later, after the caller's
    // bridge had finished.  Brought up over kReverbFadeInSamples instead.
    // (Not the wet/dry amount: ramped, it moved the dry string's duck past
    // the end of the bridge, where nothing makes up for it -- the bridge only
    // covers what happens while it plays.)
    // A strike ends it: the attack covers the onset, and fading it out of
    // the reverb would take the loudest part of the note out of the tail.
    float input_gain = 0.2f;
    if (performance_state.strum) {
      reverb_fade_in_ = kReverbFadeInSamples;
    }
    if (reverb_fade_in_ < kReverbFadeInSamples) {
      input_gain *= static_cast<float>(reverb_fade_in_) /
          static_cast<float>(kReverbFadeInSamples);
      reverb_fade_in_ += static_cast<int32_t>(size);
    }
    reverb_.set_input_gain(input_gain);
    reverb_.set_lp(0.3f + patch.brightness * 0.6f);
    reverb_.Process(out, aux, size);
    for (size_t i = 0; i < size; ++i) {
      aux[i] = -aux[i];
    }
  } else if (reverb_tail_ > 0) {
    // FORK: the String + Reverb model's reverb ringing out after a change to
    // String, fed silence, with the patch still steering it.
    // In its own model the reverb's right channel leaves negated, so a mono
    // mix takes out - aux there; here it is added as is to a model mixed as
    // out + aux, which is the same sum.  Limiter gain and the caller's own
    // make-up differ between the models, so the tail is held at its level.
    float fade = reverb_tail_ < kReverbFadeSamples
        ? static_cast<float>(reverb_tail_) / static_cast<float>(kReverbFadeSamples)
        : 1.0f;
    fill(&out_buffer_[0], &out_buffer_[size], 0.0f);
    fill(&aux_buffer_[0], &aux_buffer_[size], 0.0f);
    reverb_.set_amount(0.1f + patch.damping * 0.5f);
    reverb_.set_time((0.35f + 0.63f * patch.damping) * fade);
    reverb_.set_lp(0.3f + patch.brightness * 0.6f);
    reverb_.Process(out_buffer_, aux_buffer_, size);
    float gain = fade *
        model_gains_[RESONATOR_MODEL_STRING_AND_REVERB] /
        (model_gains_[model_] * output_boost_);
    for (size_t i = 0; i < size; ++i) {
      out[i] += out_buffer_[i] * gain;
      aux[i] += aux_buffer_[i] * gain;
    }
    reverb_tail_ -= static_cast<int32_t>(size);
  }
  
  // Apply limiter to string output.  FORK: with the caller's boost, which
  // the limiter therefore holds; see set_output_boost().
  limiter_.Process(out, aux, size, model_gains_[model_] * output_boost_);
}

/* static */
float Part::model_gains_[] = {
  1.4f,  // RESONATOR_MODEL_MODAL
  1.0f,  // RESONATOR_MODEL_SYMPATHETIC_STRING
  1.4f,  // RESONATOR_MODEL_STRING
  0.7f,  // RESONATOR_MODEL_FM_VOICE,
  1.0f,  // RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED
  1.4f,  // RESONATOR_MODEL_STRING_AND_REVERB
};

}  // namespace rings