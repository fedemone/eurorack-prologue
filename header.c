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
 *    id 7:  LFO2 Rate   (0-100%)     -> k_user_osc_param_id4
 *    id 8:  LFO2 Depth  (0-100%)     -> k_user_osc_param_id5
 *    id 9:  LFO2 Target (strings)    -> k_user_osc_param_id6
 *    id 10: LFO2 Shape  (strings)    -> custom OSC_PARAM index 12
 *    id 11: Gate Mode   (strings)    -> custom OSC_PARAM index 13
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
 *    id 10: LFO2 Rate   (0-100%)     -> custom OSC_PARAM index 8
 *    id 11: LFO2 Depth  (0-100%)     -> custom OSC_PARAM index 9
 *    id 12: LFO2 Target (strings)    -> custom OSC_PARAM index 10
 *    id 13: LFO2 Shape  (strings)    -> custom OSC_PARAM index 12
 *
 *  Rings oscillators (rings-resonator.cc):
 *    id 0:  Base Note   (0-127 MIDI) -> stored in wrapper (for gate trigger)
 *    id 1:  Position    (0-100%)     -> k_user_osc_param_shape
 *    id 2:  Structure   (0-100%)     -> k_user_osc_param_shiftshape
 *    id 3:  Brightness  (0-100%)     -> k_user_osc_param_id1
 *    id 4:  Damping     (0-100%)     -> k_user_osc_param_id2
 *    id 5:  Chord       (0-10)       -> k_user_osc_param_id3
 *    id 6:  Model       (strings)    -> custom OSC_PARAM index 8
 *    id 7:  Polyphony   (strings)    -> custom OSC_PARAM index 9
 *
 *  Reference: logue-sdk/platform/drumlogue/dummy-synth/header.c
 *
 *  Copyright (c) 2020-2022 KORG Inc. (SDK definitions)
 *  Oscillator port by peterall/eurorack-prologue contributors.
 */

#include "unit.h"

// ---- Unit header definition  ----------------------------------------------------------------

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_synth,
    .api = UNIT_API_VERSION,
    .dev_id = 0x46654465U,    /* 'FeDe' - https://github.com/fedemone/logue-sdk */

    /* Per-oscillator unit ID and display name (max 13 chars).
     * Struct field order: unit_id, version, name — must stay in order. */
#if defined(RINGS_RESONATOR)
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

#if defined(RINGS_RESONATOR)
    /* ================================================================
     * Rings oscillators (rings-resonator.cc)
     *
     * 8 params: Base Note, Position, Structure, Brightness, Damping,
     *           Chord, Model, Polyphony
     * ================================================================ */
    .num_params = 8,
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
        /* id 5: Chord (chord type for sympathetic strings) */
        {0, 10, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Chord"}},
        /* id 6: Model (resonator type: Modal/SympStr/String/FM/...) */
        {0, 5, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Model"}},
        /* id 7: Polyphony (number of voices 1-4) */
        {1, 4, 1, 1, k_unit_param_type_strings, 0, 0, 0, {"Polyphony"}},

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
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
    }

#elif defined(ELEMENTS_RESONATOR_MODES)
    /* ================================================================
     * Elements oscillators (modal-strike.cc)
     *
     * 14 params: Base Note, Position, Geometry, Strength, Mallet,
     *            Timbre, Damping, Brightness, LFO Target, LFO1 Shape,
     *            LFO2 Rate, LFO2 Depth, LFO2 Target, LFO2 Shape
     * ================================================================ */
    .num_params = 14,
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
        /* id 10: LFO2 Rate */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"LFO2 Rate"}},
        /* id 11: LFO2 Depth */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"LFO2 Depth"}},

        // Page 4
        /* id 12: LFO2 Target (which param LFO2 modulates) */
        {0, 6, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"LFO2 Target"}},
        /* id 13: LFO2 Shape (waveform for LFO2) */
        {0, 4, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"LFO2 Shape"}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
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

#else
    /* ================================================================
     * Plaits oscillators (macro-oscillator2.cc)
     *
     * 12 params: Base Note, Shape, ShiftShape, Param 1, Param 2,
     *            LFO Target, LFO1 Shape, LFO2 Rate, LFO2 Depth,
     *            LFO2 Target, LFO2 Shape, Gate Mode
     * ================================================================ */
    .num_params = 12,
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
        /* id 7: LFO2 Rate */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"LFO2 Rate"}},

        // Page 3
        /* id 8: LFO2 Depth */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"LFO2 Depth"}},
        /* id 9: LFO2 Target (which param LFO2 modulates) */
        {0, 7, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"LFO2 Target"}},
        /* id 10: LFO2 Shape (waveform for LFO2) */
        {0, 4, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"LFO2 Shape"}},
        /* id 11: Gate Mode (envelope/gate behavior) */
        {0, 2, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Gate Mode"}},

        // Pages 4-6: blank
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
#endif
};
