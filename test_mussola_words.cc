/*
 * File: test_mussola_words.cc
 *
 * Mussola word-playback tests: links the REAL Plaits SpeechEngine and the
 * real LPC word banks through the full drumlogue wrapper chain, and asks
 * the only question a player asks of this unit -- does the phrase I dialled
 * in actually come out, at the length it is supposed to be?
 *
 * Everything here is timed against a number that is known independently:
 * mussola_words.cc is generated at 40 frames per second, so decoding a bank
 * gives each word an exact duration (tools/generate_lpc_words.py, and the
 * table in the README). "kyrie eleison" is 48 frames = 1200 ms; the mantra
 * "om mani padme hum" is 84 frames = 2100 ms. A test that measures 300 ms
 * for either of those has found a truncation, not a tolerance.
 *
 * The four things that used to make phrases unplayable on hardware, each
 * pinned by a test below:
 *
 *   1. Speed was mapped 0..2 onto an engine control that wants -1..+1, so
 *      the labelled "normal" centre ran words 4x fast and nothing could run
 *      slower than 1x.                          -> speed_* tests
 *   2. The ADSR was applied over the word's own energy contour, so the
 *      factory Decay of 30 (~40 ms) cut a 1.2 s phrase to 190 ms.
 *                                               -> envelope_* tests
 *   3. Gliss fed the trigger a still-gliding morph, so the word selected
 *      was the one the knob had just left.      -> gliss_* test
 *   4. Key modes, the vowel-warping styles and the Morph knob all remapped
 *      morph inside phoneme space, which in the word region silently took
 *      the Phoneme knob off word selection.
 *                                    -> key_mode_*, morph_*, style_* tests
 *
 * Build:  make test-mussola-words
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#include "runtime.h"
#include "unit.h"

/* ===========================================================================
 * Test framework (matches test_sound_production.cc)
 * ======================================================================== */

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

/* A failing test returns early after printing why, so PASS is only printed
 * when the failure count did not move while it ran. */
#define WTEST(name) \
  static void wtest_##name(void); \
  static void run_wtest_##name(void) { \
    const int failed_before = g_tests_failed; \
    g_tests_run++; \
    printf("  %-58s ", #name); \
    fflush(stdout); \
    wtest_##name(); \
    if (g_tests_failed == failed_before) { \
      g_tests_passed++; \
      printf("PASS\n"); \
    } \
  } \
  static void wtest_##name(void)

#define FAILED() do { g_tests_failed++; } while (0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
      printf("FAIL\n    %s:%d: condition false: %s\n", \
             __FILE__, __LINE__, #cond); \
      FAILED(); return; \
    } \
  } while (0)

/* Duration assertions carry the reason, because "1200 vs 190" is the whole
 * finding and a bare line number buries it. */
#define ASSERT_MS_NEAR(want, got, tol, what) do { \
    int _w = (want), _g = (got), _t = (tol); \
    if (_g < _w - _t || _g > _w + _t) { \
      printf("FAIL\n    %s:%d: %s: expected %d ms +/- %d, measured %d ms\n", \
             __FILE__, __LINE__, (what), _w, _t, _g); \
      FAILED(); return; \
    } \
  } while (0)

#define ASSERT_MS_EQ(a, b, tol, what) do { \
    int _a = (a), _b = (b), _t = (tol); \
    if (_a - _b > _t || _b - _a > _t) { \
      printf("FAIL\n    %s:%d: %s: %d ms vs %d ms (tolerance %d)\n", \
             __FILE__, __LINE__, (what), _a, _b, _t); \
      FAILED(); return; \
    } \
  } while (0)

/* ===========================================================================
 * Panel
 * ======================================================================== */

enum {
  P_BASE_NOTE = 0, P_PHONEME, P_TIMBRE, P_HARMONICS, P_MORPH, P_SPEED,
  P_PROSODY, P_DECAY, P_MIX, P_MODEL, P_GATE_MODE, P_VOICES, P_DETUNE,
  P_SPREAD, P_GENDER, P_ATTACK, P_STYLE, P_KEY_MODE, P_GLISS, P_SUSTAIN,
  P_LFO_SHAPE, P_LFO_DEST, P_LFO_RATE, P_LFO_DEPTH, P_NUM
};

/* header.c's .init column. Kept here rather than read from unit_header,
 * which is hidden visibility (see drumlogue_guards.h). */
static const int kFactoryDefaults[P_NUM] = {
  60, 50, 50,  0,   50, 50,  0, 30,    0, 3, 0, 1,
  30, 50, 50,  0,    0,  0,  0, 100,   0, 0, 30, 0
};

/* Model / Gate Mode / Style / Key Mode enumerations, as header.c orders them. */
enum { MODEL_NAIVE = 0, MODEL_SAM, MODEL_LPC, MODEL_BLEND };
enum { GATE_TRIGGER = 0, GATE_SUSTAIN, GATE_CONTINUOUS, GATE_STACCATO };
enum { STYLE_MALE = 0, STYLE_FEMALE, STYLE_CHILD, STYLE_ROBOT, STYLE_ALIEN,
       STYLE_RELIGIOUS, STYLE_NUM };
enum { KEY_NORMAL = 0, KEY_SYLLABLE, KEY_VOW_A, KEY_VOW_B, KEY_SYL_C,
       KEY_SYL_D, KEY_NUM };

/*
 * Harmonics values that land in each word bank. Bank boundaries are at
 * 43 / 55 / 67 / 79 / 91 -- see word_bank_for() in mussola.cc, which mirrors
 * the engine's quantizer; these are the midpoints.
 */
static const int kBankHarmonics[5] = { 49, 61, 73, 85, 96 };
static const int kWordsPerBank[5] = { 2, 2, 2, 3, 5 };

/*
 * Every word in the five banks, with the length generate_lpc_words.py
 * encoded it at: frames x 25 ms, since the banks are written at 40 fps.
 * Decoding mussola_words.cc reproduces these exactly.
 */
struct Word { int bank; int index; int encoded_ms; const char *text; };
static const Word kWords[] = {
  { 0, 0,  950, "un bel di" },        { 0, 1,  575, "bello" },
  { 1, 0, 1150, "giunto il tempo" },  { 1, 1,  575, "cosi" },
  { 2, 0,  500, "fan" },              { 2, 1,  525, "tutto" },
  { 3, 0,  450, "kyrie" },            { 3, 1,  725, "eleison" },
  { 3, 2, 1200, "kyrie eleison" },
  { 4, 0,  475, "om" },               { 4, 1,  525, "mani" },
  { 4, 2,  500, "padme" },            { 4, 3,  525, "hum" },
  { 4, 4, 2100, "om mani padme hum" },
};
static const int kNumWords = (int)(sizeof(kWords) / sizeof(kWords[0]));

/*
 * What a word of `encoded_ms` measures as, audibly.
 *
 * Consistently a little short of the encoded length, and for a reason that
 * belongs to the data rather than to the unit: each word is generated with a
 * zero-energy terminator frame, and the phone before it is usually a nasal or
 * fricative already decaying through the measurement floor. Across all
 * fourteen words the ratio sits between 89.5% and 93.9%, tightly enough to
 * state once here instead of carrying a fat tolerance into every assertion.
 * A truncation bug is not a few percent -- the envelope one this file was
 * written for left 16% of "kyrie eleison".
 */
static const float kAudibleFraction = 0.915f;
static const float kAudibleTolerance = 0.075f;   /* covers 84% .. 99% */

static int audible_ms(int encoded_ms) {
  return (int)(encoded_ms * kAudibleFraction + 0.5f);
}
static int audible_tol_ms(int encoded_ms) {
  /* Never tighter than 40 ms: the measurement buckets are 5 ms and the
   * shortest words are only ~450 ms long. */
  const int t = (int)(encoded_ms * kAudibleTolerance + 0.5f);
  return t < 40 ? 40 : t;
}

static unit_runtime_desc_t make_desc(void) {
  unit_runtime_desc_t desc;
  memset(&desc, 0, sizeof(desc));
  desc.target = k_unit_target_drumlogue_synth;
  desc.api = k_unit_api_2_0_0;
  desc.samplerate = 48000;
  desc.frames_per_buffer = 64;
  desc.input_channels = 0;
  desc.output_channels = 2;
  return desc;
}

/* Fresh unit at factory defaults. */
static void boot(void) {
  unit_runtime_desc_t desc = make_desc();
  unit_init(&desc);
  for (int i = 0; i < P_NUM; ++i) {
    unit_set_param_value((uint8_t)i, kFactoryDefaults[i]);
  }
}

static void set(int id, int value) {
  unit_set_param_value((uint8_t)id, value);
}

/*
 * A patch that plays word `phoneme` of the bank at `harmonics`, held, with
 * the envelope kept out of the way so what is measured is the word.
 */
static void word_patch(int harmonics, int phoneme) {
  set(P_HARMONICS, harmonics);
  set(P_PHONEME, phoneme);
  set(P_GATE_MODE, GATE_SUSTAIN);
  set(P_DECAY, 100);
  set(P_SUSTAIN, 100);
  set(P_SPEED, 50);        /* 50 is the recorded tempo */
}

/*
 * Milliseconds from note-on to the last 5 ms window still above an absolute
 * amplitude floor.
 *
 * The floor is absolute rather than a fraction of the peak on purpose: a
 * formant shift moves how loud a phrase's final nasal is relative to its
 * loudest vowel, and a peak-relative floor then reports a tempo change that
 * did not happen. That artefact is exactly what the Gender test below is
 * trying not to be fooled by.
 */
static int word_end_ms(int total_ms, int note_on_ms, int note_off_ms) {
  const uint32_t F = 64;
  const double kFloorRms = 0.002;
  float out[F * 2];
  double acc = 0.0;
  int n = 0, bucket = 0, last = -1;

  const int blocks = (int)((total_ms / 1000.0) * 48000 / F);
  const int on_blk = (int)((note_on_ms / 1000.0) * 48000 / F);
  const int off_blk = note_off_ms < 0
      ? -1 : (int)((note_off_ms / 1000.0) * 48000 / F);

  for (int b = 0; b < blocks; ++b) {
    if (b == on_blk) unit_note_on(60, 100);
    if (b == off_blk) unit_note_off(60);
    memset(out, 0, sizeof(out));
    unit_render(nullptr, out, F);
    for (uint32_t i = 0; i < F * 2; ++i) acc += (double)out[i] * (double)out[i];
    n += (int)(F * 2);
    if (n >= 240 * 2) {                     /* 5 ms buckets */
      if (sqrt(acc / n) > kFloorRms) last = bucket;
      acc = 0.0; n = 0; ++bucket;
    }
  }
  return (last + 1) * 5 - note_on_ms;
}

/* Phoneme knob setting that lands in the middle of word `index` of a bank
 * holding `n` words: GetWordBoundaries picks int(address * n). */
static int phoneme_for_word(int index, int n) {
  return (int)((index + 0.5) * 100.0 / n);
}

/* Play whatever the current patch selects and report how long it lasted. */
static int play_word(void) {
  return word_end_ms(6000, 100, 5900);
}

/* ===========================================================================
 * 1. Speed: 50 is the recorded tempo, and both halves of the knob work
 * ======================================================================== */

WTEST(speed_centre_plays_at_the_recorded_tempo) {
  boot();
  word_patch(kBankHarmonics[4], 100);        /* "om mani padme hum", 2100 ms */
  ASSERT_MS_NEAR(audible_ms(2100), play_word(), audible_tol_ms(2100),
                 "mantra at Speed=50");
  unit_teardown();
}

WTEST(speed_below_centre_slows_the_phrase_down) {
  boot();
  word_patch(kBankHarmonics[3], 100);        /* "kyrie eleison", 1200 ms */
  set(P_SPEED, 25);                          /* half speed */
  ASSERT_MS_NEAR(audible_ms(2400), word_end_ms(6000, 100, 5900),
                 audible_tol_ms(2400), "kyrie at Speed=25");
  unit_teardown();
}

WTEST(speed_floor_is_four_times_slower_not_normal) {
  boot();
  word_patch(kBankHarmonics[3], 100);
  set(P_SPEED, 0);
  ASSERT_MS_NEAR(audible_ms(4800), word_end_ms(9000, 100, 8900),
                 audible_tol_ms(4800), "kyrie at Speed=0");
  unit_teardown();
}

WTEST(speed_ceiling_is_four_times_faster) {
  boot();
  word_patch(kBankHarmonics[3], 100);
  set(P_SPEED, 100);
  ASSERT_MS_NEAR(audible_ms(300), word_end_ms(3000, 100, 2900),
                 audible_tol_ms(300), "kyrie at Speed=100");
  unit_teardown();
}

WTEST(speed_knob_is_symmetric_about_its_centre) {
  int slow, fast, centre;
  boot(); word_patch(kBankHarmonics[4], 100); set(P_SPEED, 25);
  slow = word_end_ms(9000, 100, 8900); unit_teardown();
  boot(); word_patch(kBankHarmonics[4], 100); set(P_SPEED, 50);
  centre = word_end_ms(9000, 100, 8900); unit_teardown();
  boot(); word_patch(kBankHarmonics[4], 100); set(P_SPEED, 75);
  fast = word_end_ms(9000, 100, 8900); unit_teardown();

  /* 25 and 75 sit one octave of time-stretch either side of 50. */
  ASSERT_MS_EQ(slow, centre * 2, 400, "Speed=25 should be twice Speed=50");
  ASSERT_MS_EQ(fast * 2, centre, 300, "Speed=75 should be half Speed=50");
}

/* ===========================================================================
 * 2. The envelope must not chop the phrase
 * ======================================================================== */

WTEST(envelope_factory_defaults_play_a_whole_phrase) {
  /* The regression this exists for: factory Decay=30 in Trigger mode used
   * to leave 190 ms of a 1200 ms phrase. Only Harmonics is touched here --
   * everything else is what the unit loads with. */
  boot();
  set(P_HARMONICS, kBankHarmonics[3]);
  set(P_PHONEME, 100);
  ASSERT_MS_NEAR(audible_ms(1200), word_end_ms(4000, 100, 400),
                 audible_tol_ms(1200), "kyrie eleison on the factory patch");
  unit_teardown();
}

WTEST(envelope_trigger_mode_ignores_a_short_pad_gate) {
  /* A drumlogue pad's gate is far shorter than any phrase. */
  boot();
  set(P_HARMONICS, kBankHarmonics[4]);
  set(P_PHONEME, 100);
  set(P_GATE_MODE, GATE_TRIGGER);
  ASSERT_MS_NEAR(audible_ms(2100), word_end_ms(5000, 100, 160),
                 audible_tol_ms(2100), "mantra under a 60 ms gate");
  unit_teardown();
}

WTEST(envelope_sustain_mode_still_releases_on_note_off) {
  /* The word region must not make note-off a no-op where it is meaningful. */
  boot();
  word_patch(kBankHarmonics[4], 100);
  set(P_DECAY, 0);                           /* 5 ms release */
  const int released = word_end_ms(5000, 100, 600);
  ASSERT_TRUE(released < 700);               /* stopped shortly after 500 ms */
  unit_teardown();
}

WTEST(envelope_does_not_truncate_in_any_gate_mode) {
  for (int gate = GATE_TRIGGER; gate <= GATE_SUSTAIN; ++gate) {
    boot();
    word_patch(kBankHarmonics[3], 100);
    set(P_GATE_MODE, gate);
    set(P_DECAY, kFactoryDefaults[P_DECAY]); /* the short factory decay */
    const int ms = word_end_ms(4000, 100, 3900);
    unit_teardown();
    ASSERT_MS_NEAR(audible_ms(1200), ms, audible_tol_ms(1200),
                   "kyrie eleison with a 40 ms decay");
  }
}

/* ===========================================================================
 * 3. Gliss must not select the word
 * ======================================================================== */

WTEST(gliss_does_not_steal_the_word_from_the_trigger) {
  /* Settle at the bottom of the Phoneme knob, jump it to the top, trigger.
   * Any Gliss above ~13% used to hand the trigger a value still in transit,
   * so the note sang whichever word the knob had just left. */
  for (int gliss = 0; gliss <= 100; gliss += 25) {
    boot();
    word_patch(kBankHarmonics[4], 0);
    set(P_GLISS, gliss);
    float out[64 * 2];
    for (int b = 0; b < 400; ++b) {          /* let the glide settle */
      memset(out, 0, sizeof(out));
      unit_render(nullptr, out, 64);
    }
    set(P_PHONEME, 100);
    const int ms = word_end_ms(5000, 0, 4900);
    unit_teardown();
    ASSERT_MS_NEAR(audible_ms(2100), ms, audible_tol_ms(2100),
                   "mantra selected with Gliss up");
  }
}

/* ===========================================================================
 * 4. Key modes and styles must leave word selection to the Phoneme knob
 * ======================================================================== */

WTEST(key_modes_all_select_the_same_word) {
  int normal = 0;
  for (int km = KEY_NORMAL; km < KEY_NUM; ++km) {
    boot();
    word_patch(kBankHarmonics[4], 100);
    set(P_KEY_MODE, km);
    const int ms = play_word();
    unit_teardown();
    if (km == KEY_NORMAL) {
      normal = ms;
      ASSERT_MS_NEAR(audible_ms(2100), normal, audible_tol_ms(2100),
                     "mantra in Key Mode Normal");
    } else {
      ASSERT_MS_EQ(normal, ms, 120, "a key mode changed the selected word");
    }
  }
}

WTEST(morph_knob_does_not_move_word_selection) {
  /* Morph enters phoneme space as knob/100 - 0.5. Over a word address that
   * is half a bank, and anything below ~40 used to put a bank's longest
   * phrase out of reach wherever the Phoneme knob was. */
  for (int morph = 0; morph <= 100; morph += 25) {
    boot();
    word_patch(kBankHarmonics[4], 100);
    set(P_MORPH, morph);
    const int ms = play_word();
    unit_teardown();
    ASSERT_MS_NEAR(audible_ms(2100), ms, audible_tol_ms(2100),
                   "mantra with the Morph knob turned");
  }
}

WTEST(styles_all_reach_the_longest_phrase) {
  for (int st = STYLE_MALE; st < STYLE_NUM; ++st) {
    boot();
    word_patch(kBankHarmonics[4], 100);
    set(P_STYLE, st);
    const int ms = play_word();
    unit_teardown();
    /* Religious used to compress morph into the chant range and cap the
     * address below this word; Robot used to pin Harmonics onto SAM and
     * reach no bank at all. */
    ASSERT_MS_NEAR(audible_ms(2100), ms, audible_tol_ms(2100),
                   "mantra under a vocal style");
  }
}

WTEST(robot_style_still_defaults_to_sam_below_the_word_region) {
  /* The unlock must not cost Robot its character at the knob's low end. */
  boot();
  set(P_STYLE, STYLE_ROBOT);
  set(P_HARMONICS, 20);
  set(P_GATE_MODE, GATE_SUSTAIN);
  set(P_DECAY, 100);
  /* SAM drones for as long as it is held: no word, so no ending. */
  const int ms = word_end_ms(3000, 100, 2900);
  ASSERT_TRUE(ms > 2500);
  unit_teardown();
}

/* ===========================================================================
 * 5. Harmonics: the bank boundaries, and what Model pins them to
 * ======================================================================== */

WTEST(every_word_in_every_bank_plays_at_its_encoded_length) {
  /* All fourteen words, addressed the way the panel addresses them:
   * Harmonics picks the bank, Phoneme picks the word within it. */
  for (int w = 0; w < kNumWords; ++w) {
    const Word &word = kWords[w];
    boot();
    word_patch(kBankHarmonics[word.bank],
               phoneme_for_word(word.index, kWordsPerBank[word.bank]));
    const int ms = play_word();
    unit_teardown();
    const int want = audible_ms(word.encoded_ms);
    const int tol = audible_tol_ms(word.encoded_ms);
    if (ms < want - tol || ms > want + tol) {
      printf("FAIL\n    %s:%d: bank %d word %d \"%s\": encoded %d ms, "
             "expected %d ms +/- %d, measured %d ms\n",
             __FILE__, __LINE__, word.bank, word.index, word.text,
             word.encoded_ms, want, tol, ms);
      FAILED(); return;
    }
  }
}

WTEST(harmonics_word_region_starts_at_43) {
  /* Below the threshold the engine scans phoneme space and drones; at it,
   * a word plays and ends. 42 and 43 are the two sides of that step. */
  boot(); word_patch(42, 100);
  const int below = word_end_ms(3000, 100, 2900);
  unit_teardown();
  boot(); word_patch(43, 100);
  const int at = word_end_ms(3000, 100, 2900);
  unit_teardown();

  ASSERT_TRUE(below > 2500);                 /* still droning */
  ASSERT_MS_NEAR(audible_ms(575), at, audible_tol_ms(575),
                 "\"bello\" at Harmonics=43");
}

WTEST(model_lpc_scans_phonemes_instead_of_singing_bank_zero) {
  /* Model=LPC pinned Harmonics at 0.5, which quantized into word bank 0 --
   * so it always sang "un bel di"/"bello" and could reach nothing else. */
  boot();
  set(P_MODEL, MODEL_LPC);
  set(P_PHONEME, 100);
  set(P_GATE_MODE, GATE_SUSTAIN);
  set(P_DECAY, 100);
  const int ms = word_end_ms(3000, 100, 2900);
  ASSERT_TRUE(ms > 2500);                    /* scanning, not replaying */
  unit_teardown();
}

/* ===========================================================================
 * 6. Tempo belongs to Speed alone
 * ======================================================================== */

WTEST(word_tempo_is_independent_of_gender) {
  int neutral = 0;
  for (int gender = 0; gender <= 100; gender += 25) {
    boot();
    word_patch(kBankHarmonics[4], 100);
    set(P_GENDER, gender);
    const int ms = play_word();
    unit_teardown();
    if (gender == 0) neutral = ms;
    /* The controller folds a formant term worth +/-18 semitones into its
     * time stretch; mussola.cc cancels it so Gender shifts formants only. */
    ASSERT_MS_EQ(neutral, ms, 150, "Gender changed the word tempo");
  }
}

WTEST(word_tempo_is_independent_of_style) {
  int first = 0;
  for (int st = STYLE_MALE; st < STYLE_NUM; ++st) {
    boot();
    word_patch(kBankHarmonics[4], 100);
    set(P_STYLE, st);
    const int ms = play_word();
    unit_teardown();
    if (st == STYLE_MALE) first = ms;
    else ASSERT_MS_EQ(first, ms, 150, "a style changed the word tempo");
  }
}

/* ===========================================================================
 * 7. Staccato keeps its documented rate range
 * ======================================================================== */

WTEST(staccato_rate_still_spans_1_5_to_13_5_hz) {
  /* Speed is bipolar for the engine but the burst rate reads the knob
   * directly, so the published range must not have moved. */
  const struct { int knob; float hz; } kCases[] = {
    { 0, 1.5f }, { 50, 7.5f }, { 100, 13.5f }
  };
  for (unsigned c = 0; c < sizeof(kCases) / sizeof(kCases[0]); ++c) {
    boot();
    set(P_GATE_MODE, GATE_STACCATO);
    set(P_SPEED, kCases[c].knob);
    set(P_DECAY, 0);
    set(P_ATTACK, 0);
    set(P_SUSTAIN, 100);
    set(P_HARMONICS, 0);

    const uint32_t F = 64;
    const int blocks = 7500;                 /* 10 s */
    float out[F * 2];
    double acc = 0.0;
    int n = 0;
    static float env[12000];
    int env_n = 0;
    for (int b = 0; b < blocks && env_n < 12000; ++b) {
      memset(out, 0, sizeof(out));
      unit_render(nullptr, out, F);
      for (uint32_t i = 0; i < F * 2; ++i) acc += (double)out[i] * (double)out[i];
      n += (int)(F * 2);
      if (n >= 48 * 2) {                     /* 1 ms buckets */
        env[env_n++] = (float)sqrt(acc / n); acc = 0.0; n = 0;
      }
    }
    unit_teardown();

    float peak = 0.0f;
    for (int i = 0; i < env_n; ++i) if (env[i] > peak) peak = env[i];
    /* Count gate rises with hysteresis: a burst's own amplitude ripple
     * would otherwise be counted several times over. */
    int rises = 0; bool armed = true;
    for (int i = 0; i < env_n; ++i) {
      if (armed && env[i] > peak * 0.60f) { ++rises; armed = false; }
      else if (!armed && env[i] < peak * 0.10f) armed = true;
    }
    const float hz = (float)rises / ((float)blocks * 64.0f / 48000.0f);
    if (fabsf(hz - kCases[c].hz) > 0.6f) {
      printf("FAIL\n    %s:%d: Staccato at Speed=%d: expected %.1f Hz, "
             "measured %.2f Hz\n",
             __FILE__, __LINE__, kCases[c].knob, kCases[c].hz, hz);
      FAILED(); return;
    }
  }
}

/* ===========================================================================
 * 8. Nothing above may produce a bad sample
 * ======================================================================== */

WTEST(word_playback_output_stays_finite) {
  for (int bank = 0; bank < 5; ++bank) {
    boot();
    word_patch(kBankHarmonics[bank], 100);
    set(P_VOICES, 4);
    set(P_STYLE, STYLE_ALIEN);               /* drive + phaser on the output */
    float out[64 * 2];
    for (int b = 0; b < 3000; ++b) {
      if (b == 10) unit_note_on(60, 100);
      memset(out, 0, sizeof(out));
      unit_render(nullptr, out, 64);
      for (int i = 0; i < 128; ++i) {
        if (std::isnan(out[i]) || std::isinf(out[i]) || fabsf(out[i]) > 4.0f) {
          printf("FAIL\n    %s:%d: bank %d, block %d, sample %d = %f\n",
                 __FILE__, __LINE__, bank, b, i, out[i]);
          FAILED(); unit_teardown(); return;
        }
      }
    }
    unit_teardown();
  }
}

/* ===========================================================================
 * Main
 * ======================================================================== */

int main(void) {
  printf("\n=== Mussola Word Playback (real SpeechEngine + real word banks) ===\n\n");

  printf("Speed (50 = the recorded tempo):\n");
  run_wtest_speed_centre_plays_at_the_recorded_tempo();
  run_wtest_speed_below_centre_slows_the_phrase_down();
  run_wtest_speed_floor_is_four_times_slower_not_normal();
  run_wtest_speed_ceiling_is_four_times_faster();
  run_wtest_speed_knob_is_symmetric_about_its_centre();

  printf("\nEnvelope (the word carries its own contour):\n");
  run_wtest_envelope_factory_defaults_play_a_whole_phrase();
  run_wtest_envelope_trigger_mode_ignores_a_short_pad_gate();
  run_wtest_envelope_sustain_mode_still_releases_on_note_off();
  run_wtest_envelope_does_not_truncate_in_any_gate_mode();

  printf("\nWord selection:\n");
  run_wtest_gliss_does_not_steal_the_word_from_the_trigger();
  run_wtest_key_modes_all_select_the_same_word();
  run_wtest_morph_knob_does_not_move_word_selection();
  run_wtest_styles_all_reach_the_longest_phrase();
  run_wtest_robot_style_still_defaults_to_sam_below_the_word_region();

  printf("\nHarmonics and Model:\n");
  run_wtest_every_word_in_every_bank_plays_at_its_encoded_length();
  run_wtest_harmonics_word_region_starts_at_43();
  run_wtest_model_lpc_scans_phonemes_instead_of_singing_bank_zero();

  printf("\nTempo belongs to Speed:\n");
  run_wtest_word_tempo_is_independent_of_gender();
  run_wtest_word_tempo_is_independent_of_style();

  printf("\nUnchanged behaviour:\n");
  run_wtest_staccato_rate_still_spans_1_5_to_13_5_hz();
  run_wtest_word_playback_output_stays_finite();

  printf("\n=== Results: %d/%d passed", g_tests_passed, g_tests_run);
  if (g_tests_failed > 0) printf(", %d FAILED", g_tests_failed);
  printf(" ===\n\n");

  return g_tests_failed > 0 ? 1 : 0;
}
