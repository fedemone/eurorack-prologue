/**
 *  @file header.c
 *  @brief drumlogue SDK unit header for Eurorack oscillator port
 *
 *  Following the logue-sdk v2.0 convention, the unit header is defined
 *  in a separate C file and placed in the .unit_header ELF section.
 *
 *  Per-oscillator parameter layouts via compile-time #ifdef:
 *
 *  Plaits oscillators (macro-oscillator2.cc):
 *    id 0:  Base Note   (0-127 MIDI) -> stored in wrapper (for gate trigger)
 *    id 1:  Shape       (0-100%)     -> k_user_osc_param_shape
 *    id 2:  ShiftShape  (0-100%)     -> k_user_osc_param_shiftshape
 *    id 3:  Param 1     (0-100%)     -> k_user_osc_param_id1 (bipolar)
 *    id 4:  Param 2     (0-100%)     -> k_user_osc_param_id2
 *    id 5:  LFO Target  (strings)    -> k_user_osc_param_id3
 *    id 6:  LFO1 Shape  (strings)    -> custom OSC_PARAM index 11
 *    id 7:  LFO1 Rate   (0-100%)     -> stored in wrapper (internal LFO1)
 *    id 8:  LFO2 Rate   (0-100%)     -> k_user_osc_param_id4
 *    id 9:  LFO2 Depth  (0-100%)     -> k_user_osc_param_id5
 *    id 10: LFO2 Target (strings)    -> k_user_osc_param_id6
 *    id 11: LFO2 Shape  (strings)    -> custom OSC_PARAM index 12
 *    id 12: Gate Mode   (strings)    -> custom OSC_PARAM index 13
 *    id 13: LFO1 Depth  (0-100%)     -> custom OSC_PARAM index 14
 *    id 14: Pitch Range (0-24 st)    -> custom OSC_PARAM index 15
 *
 *  Elements oscillators (modal-strike.cc):
 *    id 0:  Base Note   (0-127 MIDI) -> stored in wrapper (for gate trigger)
 *    id 1:  Position    (0-100%)     -> k_user_osc_param_shape
 *    id 2:  Geometry    (0-100%)     -> k_user_osc_param_shiftshape
 *    id 3:  Strength    (0-100%)     -> k_user_osc_param_id1
 *    id 4:  Mallet      (0-100%)     -> k_user_osc_param_id2
 *    id 5:  Timbre      (0-100%)     -> k_user_osc_param_id3
 *    id 6:  Damping     (0-100%)     -> k_user_osc_param_id4
 *    id 7:  Brightness  (0-100%)     -> k_user_osc_param_id5
 *    id 8:  LFO Target  (strings)    -> k_user_osc_param_id6
 *    id 9:  LFO1 Shape  (strings)    -> custom OSC_PARAM index 11
 *    id 10: LFO1 Rate   (0-100%)     -> stored in wrapper (internal LFO1)
 *    id 11: LFO2 Rate   (0-100%)     -> custom OSC_PARAM index 8
 *    id 12: LFO2 Depth  (0-100%)     -> custom OSC_PARAM index 9
 *    id 13: LFO2 Target (strings)    -> custom OSC_PARAM index 10
 *    id 14: LFO2 Shape  (strings)    -> custom OSC_PARAM index 12
 *
 *  Rings oscillators (rings-resonator.cc):
 *    id 0:  Base Note   (0-127 MIDI) -> stored in wrapper (for gate trigger)
 *    id 1:  Position    (0-100%)     -> k_user_osc_param_shape
 *    id 2:  Structure   (0-100%)     -> k_user_osc_param_shiftshape
 *    id 3:  Brightness  (0-100%)     -> k_user_osc_param_id1
 *    id 4:  Damping     (0-100%)     -> k_user_osc_param_id2
 *    id 5:  Chord       (0-13)       -> k_user_osc_param_id3
 *    id 6:  Model       (strings)    -> custom OSC_PARAM index 8
 *    id 7:  Polyphony   (strings)    -> custom OSC_PARAM index 9
 *    id 8:  Arp         (strings)    -> custom OSC_PARAM index 10
 *    id 9:  Arp Src     (strings)    -> custom OSC_PARAM index 11
 *    id 10: Arp Rate    (strings)    -> custom OSC_PARAM index 12
 *    id 11: Arp Oct     (strings)    -> custom OSC_PARAM index 13
 *    id 12: LFO1 Target (strings)    -> k_user_osc_param_id4
 *    id 13: LFO1 Shape  (strings)    -> custom OSC_PARAM index 14
 *    id 14: LFO1 Rate   (0-100%)     -> stored in wrapper (internal LFO1)
 *    id 15: LFO1 Depth  (0-100%)     -> custom OSC_PARAM index 17
 *    id 16: LFO2 Target (strings)    -> custom OSC_PARAM index 15
 *    id 17: LFO2 Shape  (strings)    -> custom OSC_PARAM index 16
 *    id 18: LFO2 Rate   (0-100%)     -> k_user_osc_param_id5
 *    id 19: LFO2 Depth  (0-100%)     -> k_user_osc_param_id6
 *    id 20: Note Range  (0-24 st)    -> custom OSC_PARAM index 18
 *
 *  Clouds oscillators (clouds-granular.cc):
 *    id 0:  Base Note   (0-127 MIDI) -> stored in wrapper (for gate trigger)
 *    id 1:  Position    (0-100%)     -> k_user_osc_param_shape
 *    id 2:  Size        (0-100%)     -> k_user_osc_param_shiftshape
 *    id 3:  Density     (0-100%)     -> k_user_osc_param_id1
 *    id 4:  Texture     (0-100%)     -> k_user_osc_param_id2
 *    id 5:  Pitch       (-24..+24)   -> k_user_osc_param_id3 (semitones)
 *    id 6:  Feedback    (0-100%)     -> k_user_osc_param_id4
 *    id 7:  Dry/Wet     (0-100%)     -> k_user_osc_param_id5
 *    id 8:  Reverb      (0-100%)     -> k_user_osc_param_id6
 *    id 9:  Freeze      (on/off)     -> custom OSC_PARAM index 8
 *    id 10: Mode        (strings)    -> custom OSC_PARAM index 9
 *    id 11: Quality     (strings)    -> custom OSC_PARAM index 10
 *    id 12: SampleBank  (0-15)       -> custom OSC_PARAM index 11
 *    id 13: SampleNum   (0-64)       -> custom OSC_PARAM index 12
 *    id 14: SmplStart   (0-1000 ‰)   -> custom OSC_PARAM index 13
 *    id 15: SmplEnd     (0-1000 ‰)   -> custom OSC_PARAM index 14
 *
 *  CloudsFX insert effect (clouds-fx.cc, delfx unit — CLOUDS_FX):
 *    Same controls as the Clouds synth, minus Base Note and the four
 *    sample params — the audio to granulate arrives on the FX bus.
 *    id 0:  Position    (0-100%)     -> clouds_fx_set_param id 0
 *    id 1:  Size        (0-100%)     -> clouds_fx_set_param id 1
 *    id 2:  Density     (0-100%)     -> clouds_fx_set_param id 2
 *    id 3:  Texture     (0-100%)     -> clouds_fx_set_param id 3
 *    id 4:  Pitch       (-24..+24)   -> clouds_fx_set_param id 4 (semitones)
 *    id 5:  Feedback    (0-100%)     -> clouds_fx_set_param id 5
 *    id 6:  Dry/Wet     (0-100%)     -> clouds_fx_set_param id 6
 *    id 7:  Reverb      (0-100%)     -> clouds_fx_set_param id 7
 *    id 8:  Freeze      (on/off)     -> clouds_fx_set_param id 8
 *    id 9:  Mode        (strings)    -> clouds_fx_set_param id 9
 *    id 10: Quality     (strings)    -> clouds_fx_set_param id 10
 *
 *  Mussola vocal synth (mussola.cc):
 *    id 0:  Base Note   (0-127 MIDI) -> stored in wrapper (for gate trigger)
 *    id 1:  Phoneme     (0-100%)     -> k_user_osc_param_shape
 *    id 2:  Timbre      (0-100%)     -> k_user_osc_param_shiftshape
 *    id 3:  Harmonics   (0-100%)     -> k_user_osc_param_id1
 *    id 4:  Morph       (0-100%)     -> k_user_osc_param_id2
 *    id 5:  Speed       (0-100%)     -> custom OSC_PARAM index 8
 *    id 6:  Prosody     (0-100%)     -> custom OSC_PARAM index 9
 *    id 7:  Decay       (0-100%)     -> custom OSC_PARAM index 10
 *    id 8:  Mix         (0-100%)     -> custom OSC_PARAM index 11
 *    id 9:  Model       (strings)    -> custom OSC_PARAM index 12
 *    id 10: Gate Mode   (strings)    -> custom OSC_PARAM index 13
 *    id 11: Voices      (strings)    -> custom OSC_PARAM index 14
 *    id 12: Detune      (0-100%)     -> custom OSC_PARAM index 15
 *    id 13: Spread      (0-100%)     -> custom OSC_PARAM index 16
 *    id 14: Gender      (0-100%)     -> custom OSC_PARAM index 17
 *    id 15: Attack      (0-100%)     -> custom OSC_PARAM index 18
 *    id 16: Style       (strings)    -> custom OSC_PARAM index 19
 *    id 17: Key Mode    (strings)    -> custom OSC_PARAM index 20
 *    id 18: Gliss       (0-100%)     -> custom OSC_PARAM index 21
 *    id 19: Sustain     (0-100%)     -> custom OSC_PARAM index 22
 *    id 20: LFO Shape   (strings)    -> custom OSC_PARAM index 23
 *    id 21: LFO Dest    (strings)    -> custom OSC_PARAM index 24
 *    id 22: LFO Rate    (0-100%)     -> custom OSC_PARAM index 25
 *    id 23: LFO Depth   (0-100%)     -> custom OSC_PARAM index 26
 *
 *  Reference: logue-sdk/platform/drumlogue/dummy-synth/header.c
 *
 *  Copyright (c) 2020-2022 KORG Inc. (SDK definitions)
 *  Oscillator port by peterall/eurorack-prologue contributors.
 */

#include "unit.h"
#include "drumlogue_guards.h"

// ---- Unit header definition  ----------------------------------------------------------------

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    /* UNIT_OWN_TARGET picks delfx for CloudsFX (an insert effect: it processes
     * the FX-bus audio input, which synth units ignore) and synth for
     * everything else.  The wrappers validate against the same macro rather
     * than reading it back out of this struct; see drumlogue_guards.h. */
    .target = UNIT_OWN_TARGET,
    .api = UNIT_API_VERSION,
    .dev_id = 0x46654465U,    /* 'FeDe' - https://github.com/fedemone/logue-sdk */

    /* Per-oscillator unit ID and display name (max 13 chars).
     * Struct field order: unit_id, version, name — must stay in order. */
#if defined(CLOUDS_FX)
    .unit_id = 0x434C4458U,   /* 'CLDX' */
    .version = 0x00010000U,
    .name = "CloudsFX",
#elif defined(MUSSOLA_VOCAL)
    .unit_id = 0x4D555353U,   /* 'MUSS' */
    .version = 0x00010000U,
    .name = "Mussola",
#elif defined(CLOUDS_GRANULAR)
    .unit_id = 0x434C4453U,   /* 'CLDS' */
    .version = 0x00010000U,
    .name = "Clouds",
#elif defined(RINGS_RESONATOR)
    .unit_id = 0x524E5253U,   /* 'RNRS' */
    .version = 0x00010000U,
    .name = "Rings",
#elif defined(ELEMENTS_FULL)
    .unit_id = 0x456C4675U,   /* 'ElFu' */
    .version = 0x00010800U,
    .name = "ElementsFull",
#elif defined(ELEMENTS_RESONATOR_MODES) && defined(USE_LIMITER)
    .unit_id = 0x4D537224U,   /* 'MSr$' */
    .version = 0x00010800U,
    .name = "ModalStrike",
#elif defined(ELEMENTS_RESONATOR_MODES) && (ELEMENTS_RESONATOR_MODES == 16)
    .unit_id = 0x4D533136U,   /* 'MS16' */
    .version = 0x00010800U,
    .name = "Strike16",
#elif defined(ELEMENTS_RESONATOR_MODES)
    .unit_id = 0x4D533234U,   /* 'MS24' */
    .version = 0x00010800U,
    .name = "Strike24",
#elif defined(OSC_VA)
    .unit_id = 0x504C5641U,   /* 'PLVA' */
    .version = 0x00010800U,
    .name = "VirtAnalog",
#elif defined(OSC_WSH)
    .unit_id = 0x504C5753U,   /* 'PLWS' */
    .version = 0x00010800U,
    .name = "Waveshaper",
#elif defined(OSC_FM)
    .unit_id = 0x504C464DU,   /* 'PLFM' */
    .version = 0x00010800U,
    .name = "FM",
#elif defined(OSC_GRN)
    .unit_id = 0x504C4752U,   /* 'PLGR' */
    .version = 0x00010800U,
    .name = "Granular",
#elif defined(OSC_ADD)
    .unit_id = 0x504C4144U,   /* 'PLAD' */
    .version = 0x00010800U,
    .name = "Additive",
#elif defined(OSC_STRING)
    .unit_id = 0x504C5354U,   /* 'PLST' */
    .version = 0x00010800U,
    .name = "String",
#elif defined(OSC_WTA)
    .unit_id = 0x57544131U,   /* 'WTA1' */
    .version = 0x00010800U,
    .name = "Wavetable A",
#elif defined(OSC_WTB)
    .unit_id = 0x57544232U,   /* 'WTB2' */
    .version = 0x00010800U,
    .name = "Wavetable B",
#elif defined(OSC_WTC)
    .unit_id = 0x57544333U,   /* 'WTC3' */
    .version = 0x00010800U,
    .name = "Wavetable C",
#elif defined(OSC_WTD)
    .unit_id = 0x57544434U,   /* 'WTD4' */
    .version = 0x00010800U,
    .name = "Wavetable D",
#elif defined(OSC_WTE)
    .unit_id = 0x57544535U,   /* 'WTE5' */
    .version = 0x00010800U,
    .name = "Wavetable E",
#elif defined(OSC_WTF)
    .unit_id = 0x57544636U,   /* 'WTF6' */
    .version = 0x00010800U,
    .name = "Wavetable F",
#else
    .unit_id = 0x5265736fU,   /* fallback */
    .version = 0x00010800U,
    .name = "EurorackOSC",
#endif
    .num_presets = 0,

#if defined(CLOUDS_FX)
    /* ================================================================
     * CloudsFX insert effect (clouds-fx.cc, delfx unit)
     *
     * Same controls as the Clouds synth, minus everything that fed an
     * internal source: no Base Note, no SampleBank/SampleNum/SmplStart/
     * SmplEnd — the audio to granulate comes from the FX bus instead.
     *
     * 11 params: Position, Size, Density, Texture, Pitch, Feedback,
     *            Dry/Wet, Reverb, Freeze, Mode, Quality
     * ================================================================ */
    .num_params = 11,
    .params = {
        // Page 1
        /* id 0: Position (granular buffer position) */
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Position"}},
        /* id 1: Size (grain size / buffer region) */
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Size"}},
        /* id 2: Density (grain density / overlap) */
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Density"}},
        /* id 3: Texture (grain window shape / filter) */
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Texture"}},

        // Page 2
        /* id 4: Pitch (pitch shift in semitones, 24 = 0) */
        {0, 48, 24, 24, k_unit_param_type_none, 0, 0, 0, {"Pitch"}},
        /* id 5: Feedback (feedback amount) */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"Feedback"}},
        /* id 6: Dry/Wet (output mix; 50% default for an insert FX) */
        {0, 100, 0, 50, k_unit_param_type_drywet, 0, 0, 0, {"Dry/Wet"}},
        /* id 7: Reverb (reverb amount) */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"Reverb"}},

        // Page 3
        /* id 8: Freeze (freeze buffer) */
        {0, 1, 0, 0, k_unit_param_type_onoff, 0, 0, 0, {"Freeze"}},
        /* id 9: Mode (Granular/Stretch/Delay/Spectral) */
        {0, 3, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Mode"}},
        /* id 10: Quality (stereo/mono, hi/lo fidelity) */
        {0, 3, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Quality"}},

        // Pages 3-6: blank
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
    }

#elif defined(MUSSOLA_VOCAL)
    /* ================================================================
     * Mussola vocal synth (mussola.cc)
     *
     * 24 params: Base Note, Phoneme, Timbre, Harmonics, Morph,
     *            Speed, Prosody, Decay, Mix, Model, Gate Mode,
     *            Voices, Detune, Spread, Gender, Attack,
     *            Style, Key Mode, Gliss, Sustain,
     *            LFO Shape, LFO Dest, LFO Rate, LFO Depth
     * ================================================================ */
    .num_params = 24,
    .params = {
        // Page 1
        /* id 0: Base Note (MIDI note) */
        {0, 127, 60, 60, k_unit_param_type_midi_note, 0, 0, 0, {"Base Note"}},
        /* id 1: Phoneme (vowel/phoneme selection) */
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Phoneme"}},
        /* id 2: Timbre (vocal register / formant shift) */
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Timbre"}},
        /* id 3: Harmonics (model blend: Naive/SAM/LPC) */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"Harmonics"}},

        // Page 2
        /* id 4: Morph (morph within current model) */
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Morph"}},
        /* id 5: Speed (LPC playback speed, 50=normal) */
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Speed"}},
        /* id 6: Prosody (prosody replay amount) */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"Prosody"}},
        /* id 7: Decay (envelope decay time) */
        {0, 100, 0, 30, k_unit_param_type_percent, 0, 0, 0, {"Decay"}},

        // Page 3
        /* id 8: Mix (main/aux crossfade) */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"Mix"}},
        /* id 9: Model (0=Naive, 1=SAM, 2=LPC, 3=Blend) */
        {0, 3, 0, 3, k_unit_param_type_strings, 0, 0, 0, {"Model"}},
        /* id 10: Gate Mode (Trigger/Sustain/Continuous/Staccato) */
        {0, 3, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Gate Mode"}},
        /* id 11: Voices (1-4 unison voice count) */
        {1, 4, 1, 1, k_unit_param_type_strings, 0, 0, 0, {"Voices"}},

        // Page 4
        /* id 12: Detune (unison detune amount) */
        {0, 100, 0, 30, k_unit_param_type_percent, 0, 0, 0, {"Detune"}},
        /* id 13: Spread (stereo spread of voices) */
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Spread"}},
        /* id 14: Gender (formant shift, 50=neutral) */
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Gender"}},
        /* id 15: Attack (envelope attack time) */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"Attack"}},

        // Page 5
        /* id 16: Style (Male/Female/Child/Robot/Alien/Religious) */
        {0, 5, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Style"}},
        /* id 17: Key Mode (Normal / Syllable / 4 key-assign variants) */
        {0, 5, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Key Mode"}},
        /* id 18: Gliss (glissando time for pitch and phoneme passage) */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"Gliss"}},
        /* id 19: Sustain (envelope sustain level) */
        {0, 100, 0, 100, k_unit_param_type_percent, 0, 0, 0, {"Sustain"}},

        // Page 6
        /* id 20: LFO Shape (None/Sine/Square/Saw) */
        {0, 3, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"LFO Shape"}},
        /* id 21: LFO Dest (modulation destination) */
        {0, 14, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"LFO Dest"}},
        /* id 22: LFO Rate (0.05 Hz .. 20 Hz, exponential) */
        {0, 100, 0, 30, k_unit_param_type_percent, 0, 0, 0, {"LFO Rate"}},
        /* id 23: LFO Depth (modulation depth) */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"LFO Depth"}},
    }

#elif defined(CLOUDS_GRANULAR)
    /* ================================================================
     * Clouds oscillators (clouds-granular.cc)
     *
     * 16 params: Base Note, Position, Size, Density, Texture, Pitch,
     *            Feedback, Dry/Wet, Reverb, Freeze, Mode, Quality,
     *            SampleBank, SampleNum, SmplStart, SmplEnd
     * ================================================================ */
    .num_params = 16,
    .params = {
        // Page 1
        /* id 0: Base Note (MIDI note for gate trigger) */
        {0, 127, 60, 60, k_unit_param_type_midi_note, 0, 0, 0, {"Base Note"}},
        /* id 1: Position (granular buffer position) */
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Position"}},
        /* id 2: Size (grain size / buffer region) */
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Size"}},
        /* id 3: Density (grain density / overlap) */
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Density"}},

        // Page 2
        /* id 4: Texture (grain window shape / filter) */
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Texture"}},
        /* id 5: Pitch (pitch shift in semitones) */
        {0, 48, 24, 24, k_unit_param_type_none, 0, 0, 0, {"Pitch"}},
        /* id 6: Feedback (feedback amount) */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"Feedback"}},
        /* id 7: Dry/Wet (output mix) */
        {0, 100, 0, 100, k_unit_param_type_drywet, 0, 0, 0, {"Dry/Wet"}},

        // Page 3
        /* id 8: Reverb (reverb amount) */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"Reverb"}},
        /* id 9: Freeze (freeze buffer) */
        {0, 1, 0, 0, k_unit_param_type_onoff, 0, 0, 0, {"Freeze"}},
        /* id 10: Mode (Granular/Stretch/Delay/Spectral) */
        {0, 3, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Mode"}},
        /* id 11: Quality (stereo/mono, hi/lo fidelity) */
        {0, 3, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Quality"}},

        // Page 4
        /* id 12: SampleBank (drumlogue sample bank selection) */
        {0, 15, 0, 0, k_unit_param_type_none, 0, 0, 0, {"SampleBank"}},
        /* id 13: SampleNum (0=internal osc, 1+=sample from bank) */
        {0, 64, 0, 0, k_unit_param_type_none, 0, 0, 0, {"SampleNum"}},
        /* id 14: SmplStart (sample start point, 0-1000 = 0.0%-100.0%) */
        {0, 1000, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"SmplStart"}},
        /* id 15: SmplEnd (sample end point, 0-1000 = 0.0%-100.0%) */
        {0, 1000, 0, 1000, k_unit_param_type_percent, 0, 0, 0, {"SmplEnd"}},

        // Pages 5-6: blank
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
    }

#elif defined(RINGS_RESONATOR)
    /* ================================================================
     * Rings oscillators (rings-resonator.cc)
     *
     * 21 params: Base Note, Position, Structure, Brightness, Damping,
     *            Chord, Model, Polyphony, Arp, Arp Src, Arp Rate, Arp Oct,
     *            LFO1 Target, LFO1 Shape, LFO1 Rate, LFO1 Depth,
     *            LFO2 Target, LFO2 Shape, LFO2 Rate, LFO2 Depth,
     *            Note Range
     * ================================================================ */
    .num_params = 21,
    .params = {
        // Page 1
        /* id 0: Base Note (MIDI note for gate trigger) */
        {0, 127, 60, 60, k_unit_param_type_midi_note, 0, 0, 0, {"Base Note"}},
        /* id 1: Position (excitation position) */
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Position"}},
        /* id 2: Structure (modal density / inharmonicity) */
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Structure"}},
        /* id 3: Brightness (spectral tilt) */
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Brightness"}},

        // Page 2
        /* id 4: Damping (resonance / decay time) */
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Damping"}},
        /* id 5: Chord (chord type for sympathetic strings).
         * 0-10 are Rings' own; 11-13 are added by this port (Quartal, a just
         * dominant seventh, and the gamelan slendro scale).  The max must
         * track kNumChords in eurorack-opt/rings/dsp/performance_state.h --
         * rings-resonator.cc clamps against it, so a stale value here costs a
         * chord rather than an out-of-range index. */
        {0, 13, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Chord"}},
        /* id 6: Model (resonator type: Modal/SympStr/String/FM/...).
         * Default 4 = SympStrQ so the Chord parameter is effective on load
         * (Chord only affects the quantized sympathetic-strings model). */
        {0, 5, 4, 4, k_unit_param_type_strings, 0, 0, 0, {"Model"}},
        /* id 7: Polyphony (number of voices 1-4) */
        {1, 4, 1, 1, k_unit_param_type_strings, 0, 0, 0, {"Polyphony"}},

        // Page 3
        /* id 8: Arp (Off + 8 patterns: Up/Down/Up-Dn/Dn-Up + 4 pause vars).
         * Rings is monophonic here, so the arp steps the selected note
         * source (Chord tones or octaves) and re-strums the resonator. */
        {0, 8, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Arp"}},
        /* id 9: Arp Src (0 = Chord tones, 1 = Octaves of the root) */
        {0, 1, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Arp Src"}},
        /* id 10: Arp Rate (tempo-synced step: 1/4..1/32, incl. triplets) */
        {0, 5, 3, 3, k_unit_param_type_strings, 0, 0, 0, {"Arp Rate"}},
        /* id 11: Arp Oct (span the sequence across 1-4 octaves) */
        {1, 4, 1, 1, k_unit_param_type_strings, 0, 0, 0, {"Arp Oct"}},

        /* Pages 4 and 5 are one LFO each, in the same order, so the two read
         * as a pair on the panel: Target, Shape, Rate, Depth. */

        // Page 4
        /* id 12: LFO1 Target — what LFO1 modulates.  0-5 are the destinations
         * both LFOs share; 6 and 7 are LFO2's own rate and depth, which only
         * LFO1 can reach. */
        {0, 7, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"LFO1 Target"}},
        /* id 13: LFO1 Shape (transfer curve for the incoming shape LFO) */
        {0, 4, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"LFO1 Shape"}},
        /* id 14: LFO1 Rate (drumlogue has no host shape LFO, so the wrapper
         * runs one; 0 leaves LFO1 silent, which is the default) */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"LFO1 Rate"}},
        /* id 15: LFO1 Depth.  Defaults to full scale, which is where LFO1 was
         * fixed before this existed; Rate at 0 is what keeps it quiet on
         * load. */
        {0, 100, 0, 100, k_unit_param_type_percent, 0, 0, 0, {"LFO1 Depth"}},

        // Page 5
        /* id 16: LFO2 Target (the six shared destinations) */
        {0, 5, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"LFO2 Target"}},
        /* id 17: LFO2 Shape */
        {0, 4, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"LFO2 Shape"}},
        /* id 18: LFO2 Rate */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"LFO2 Rate"}},
        /* id 19: LFO2 Depth */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"LFO2 Depth"}},

        // Page 6
        /* id 20: Note Range — semitones of pitch modulation at full LFO
         * swing, for whichever LFO has Note as its target.  2 is vibrato,
         * 12 an octave sweep.  rings-resonator.cc clamps against its own
         * maximum, so a stale value here costs range rather than an
         * out-of-range index. */
        {0, 24, 0, 2, k_unit_param_type_none, 0, 0, 0, {"Note Range"}},

        // Page 6: blank
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
    }

#elif defined(ELEMENTS_RESONATOR_MODES)
    /* ================================================================
     * Elements oscillators (modal-strike.cc)
     *
     * 15 params: Base Note, Position, Geometry, Strength, Mallet,
     *            Timbre, Damping, Brightness, LFO Target, LFO1 Shape,
     *            LFO1 Rate, LFO2 Rate, LFO2 Depth, LFO2 Target, LFO2 Shape
     * ================================================================ */
    .num_params = 15,
    .params = {
        // Page 1
        /* id 0: Base Note (MIDI note for gate trigger) */
        {0, 127, 60, 60, k_unit_param_type_midi_note, 0, 0, 0, {"Base Note"}},
        /* id 1: Position (resonator excitation position) */
        {0, 100, 0, 30, k_unit_param_type_percent, 0, 0, 0, {"Position"}},
        /* id 2: Geometry (resonator geometry / modal density) */
        {0, 100, 0, 20, k_unit_param_type_percent, 0, 0, 0, {"Geometry"}},
        /* id 3: Strength (strike exciter level) */
        {0, 100, 0, 80, k_unit_param_type_percent, 0, 0, 0, {"Strength"}},

        // Page 2
        /* id 4: Mallet (strike meta - mallet to particles) */
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Mallet"}},
        /* id 5: Timbre (strike exciter timbre / brightness) */
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Timbre"}},
        /* id 6: Damping (resonator damping) */
        {0, 100, 0, 25, k_unit_param_type_percent, 0, 0, 0, {"Damping"}},
        /* id 7: Brightness (resonator brightness / spectral tilt) */
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Brightness"}},

        // Page 3
        /* id 8: LFO Target (which param the shape LFO modulates) */
        {0, 8, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"LFO Target"}},
        /* id 9: LFO1 Shape (waveshape for shape LFO modulation) */
        {0, 4, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"LFO1 Shape"}},
        /* id 10: LFO1 Rate (internal shape LFO frequency) */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"LFO1 Rate"}},
        /* id 11: LFO2 Rate */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"LFO2 Rate"}},

        // Page 4
        /* id 12: LFO2 Depth */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"LFO2 Depth"}},
        /* id 13: LFO2 Target (which param LFO2 modulates) */
        {0, 6, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"LFO2 Target"}},
        /* id 14: LFO2 Shape (waveform for LFO2) */
        {0, 4, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"LFO2 Shape"}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},

        // Pages 5-6: blank
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
    }

#elif defined(OSC_STRING)
    /* ================================================================
     * String oscillator (macro-oscillator2.cc, OSC_STRING)
     *
     * Same 13-param layout as the other Plaits engines, but the String
     * engine routes Param 2 to the output limiter as an *attenuation*
     * (pre_gain = 1 - Param2): 0 % = full level, 100 % = silence.  The
     * generic Plaits table below defaults Param 2 to 50, which halves the
     * String's level (and 100 % mutes it entirely) — the cause of "String
     * makes no / too little sound".  Give String its own table so the
     * knob is named "Attenuate" and defaults to 0 (full level).
     * ================================================================ */
    .num_params = 15,
    .params = {
        // Page 1
        /* id 0: Base Note (MIDI note for gate trigger) */
        {0, 127, 60, 60, k_unit_param_type_midi_note, 0, 0, 0, {"Base Note"}},
        /* id 1: Shape (morph) */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"Shape"}},
        /* id 2: Shift-Shape (timbre / exciter) */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"ShiftShape"}},
        /* id 3: Harmonics (string structure / inharmonicity) */
        {0, 100, 50, 50, k_unit_param_type_percent, 0, 0, 0, {"Harmonics"}},

        // Page 2
        /* id 4: Attenuate (0 = full level, 100 = silent) */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"Attenuate"}},
        /* id 5: LFO Target (which param the shape LFO modulates) */
        {0, 7, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"LFO Target"}},
        /* id 6: LFO1 Shape (waveshape for shape LFO modulation) */
        {0, 4, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"LFO1 Shape"}},
        /* id 7: LFO1 Rate (internal shape LFO frequency) */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"LFO1 Rate"}},

        // Page 3
        /* id 8: LFO2 Rate */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"LFO2 Rate"}},
        /* id 9: LFO2 Depth */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"LFO2 Depth"}},
        /* id 10: LFO2 Target (which param LFO2 modulates) */
        {0, 7, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"LFO2 Target"}},
        /* id 11: LFO2 Shape (waveform for LFO2) */
        {0, 4, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"LFO2 Shape"}},

        // Page 4
        /* id 12: Gate Mode (envelope/gate behavior) */
        {0, 2, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Gate Mode"}},
        /* id 13: LFO1 Depth.  Defaults to full scale, which is where LFO1
         * was fixed before this existed; LFO1 Rate at 0 is what keeps it
         * quiet on load. */
        {0, 100, 0, 100, k_unit_param_type_percent, 0, 0, 0, {"LFO1 Depth"}},
        /* id 14: Pitch Range — semitones of pitch modulation at full LFO
         * swing, for whichever LFO has Pitch as its target.  Replaces a
         * fixed half-semitone; 1 is vibrato, 12 an octave sweep.
         * macro-oscillator2.cc clamps against its own maximum, so a stale
         * value here costs range rather than an out-of-range pitch.
         *
         * Appended rather than slotted in beside the other LFO controls:
         * these units are in released binaries, and renumbering would
         * repoint every saved project's knobs. */
        {0, 24, 0, 1, k_unit_param_type_none, 0, 0, 0, {"Pitch Range"}},

        // Pages 5-6: blank
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
    }

#else
    /* ================================================================
     * Plaits oscillators (macro-oscillator2.cc)
     *
     * 13 params: Base Note, Shape, ShiftShape, Param 1, Param 2,
     *            LFO Target, LFO1 Shape, LFO1 Rate, LFO2 Rate,
     *            LFO2 Depth, LFO2 Target, LFO2 Shape, Gate Mode
     * ================================================================ */
    .num_params = 15,
    .params = {
        // Page 1
        /* id 0: Base Note (MIDI note for gate trigger) */
        {0, 127, 60, 60, k_unit_param_type_midi_note, 0, 0, 0, {"Base Note"}},
        /* id 1: Shape */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"Shape"}},
        /* id 2: Shift-Shape */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"ShiftShape"}},
        /* id 3: Param 1 (bipolar) */
        {0, 100, 50, 50, k_unit_param_type_percent, 0, 0, 0, {"Param 1"}},

        // Page 2
        /* id 4: Param 2 */
        {0, 100, 50, 50, k_unit_param_type_percent, 0, 0, 0, {"Param 2"}},
        /* id 5: LFO Target (which param the shape LFO modulates) */
        {0, 7, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"LFO Target"}},
        /* id 6: LFO1 Shape (waveshape for shape LFO modulation) */
        {0, 4, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"LFO1 Shape"}},
        /* id 7: LFO1 Rate (internal shape LFO frequency) */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"LFO1 Rate"}},

        // Page 3
        /* id 8: LFO2 Rate */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"LFO2 Rate"}},
        /* id 9: LFO2 Depth */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"LFO2 Depth"}},
        /* id 10: LFO2 Target (which param LFO2 modulates) */
        {0, 7, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"LFO2 Target"}},
        /* id 11: LFO2 Shape (waveform for LFO2) */
        {0, 4, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"LFO2 Shape"}},

        // Page 4
        /* id 12: Gate Mode (envelope/gate behavior) */
        {0, 2, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Gate Mode"}},
        /* id 13: LFO1 Depth.  Defaults to full scale, which is where LFO1
         * was fixed before this existed; LFO1 Rate at 0 is what keeps it
         * quiet on load. */
        {0, 100, 0, 100, k_unit_param_type_percent, 0, 0, 0, {"LFO1 Depth"}},
        /* id 14: Pitch Range — semitones of pitch modulation at full LFO
         * swing, for whichever LFO has Pitch as its target.  Replaces a
         * fixed half-semitone; 1 is vibrato, 12 an octave sweep.
         * macro-oscillator2.cc clamps against its own maximum, so a stale
         * value here costs range rather than an out-of-range pitch.
         *
         * Appended rather than slotted in beside the other LFO controls:
         * these units are in released binaries, and renumbering would
         * repoint every saved project's knobs. */
        {0, 24, 0, 1, k_unit_param_type_none, 0, 0, 0, {"Pitch Range"}},

        // Pages 5-6: blank
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
    }
#endif
};

/*
 * A second name for the same object, hidden.
 *
 * `unit_header` has to stay in the dynamic symbol table -- the firmware looks
 * it up with dlsym() -- and every drumlogue unit ever built defines it, into
 * one shared dynamic scope. So a reference to `unit_header` from inside a unit
 * goes through the GOT and resolves to whichever unit was loaded *first*, not
 * to this one. drumlogue_guards.h explains the same trap for `.target`, which
 * is compared against a compile-time constant for exactly this reason.
 *
 * The parameter clamp cannot use a constant: it needs the whole params table.
 * A hidden alias gives it one. Hidden symbols are not preemptible, so a
 * reference to this name binds to this unit's own copy at static link time,
 * while `unit_header` continues to be exported unchanged. Same storage, same
 * bytes -- only the binding differs.
 */
extern const unit_header_t unit_header_own
    __attribute__((alias("unit_header"), visibility("hidden")));
