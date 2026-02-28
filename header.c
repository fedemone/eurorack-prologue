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
 *    id 0: Shape      (0-100%)     -> k_user_osc_param_shape
 *    id 1: ShiftShape (0-100%)     -> k_user_osc_param_shiftshape
 *    id 2: Param 1    (0-100%)     -> k_user_osc_param_id1 (bipolar)
 *    id 3: Param 2    (0-100%)     -> k_user_osc_param_id2
 *    id 4: Base Note  (0-127 MIDI) -> stored in wrapper (for gate trigger)
 *    id 5: LFO Target (enum 0-7)   -> k_user_osc_param_id3
 *    id 6: LFO2 Rate  (0-100%)     -> k_user_osc_param_id4
 *    id 7: LFO2 Depth (0-100%)     -> k_user_osc_param_id5
 *    id 8: LFO2 Target(enum 0-7)   -> k_user_osc_param_id6
 *
 *  Elements oscillators (modal-strike.cc):
 *    id 0: Position   (0-100%)     -> k_user_osc_param_shape
 *    id 1: Geometry   (0-100%)     -> k_user_osc_param_shiftshape
 *    id 2: Strength   (0-100%)     -> k_user_osc_param_id1
 *    id 3: Mallet     (0-100%)     -> k_user_osc_param_id2
 *    id 4: Timbre     (0-100%)     -> k_user_osc_param_id3
 *    id 5: Damping    (0-100%)     -> k_user_osc_param_id4
 *    id 6: Brightness (0-100%)     -> k_user_osc_param_id5
 *    id 7: Base Note  (0-127 MIDI) -> stored in wrapper (for gate trigger)
 *    id 8: LFO Target (enum 0-6)   -> k_user_osc_param_id6
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
#if defined(ELEMENTS_FULL)
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

#if defined(ELEMENTS_RESONATOR_MODES)
    /* ================================================================
     * Elements oscillators (modal-strike.cc)
     *
     * 9 params: Position, Geometry, Strength, Mallet, Timbre,
     *           Damping, Brightness, Base Note, LFO Target
     * ================================================================ */
    .num_params = 9,
    .params = {
        // Page 1
        /* id 0: Position (resonator excitation position) */
        {0, 100, 0, 30, k_unit_param_type_percent, 0, 0, 0, {"Position"}},
        /* id 1: Geometry (resonator geometry / modal density) */
        {0, 100, 0, 20, k_unit_param_type_percent, 0, 0, 0, {"Geometry"}},
        /* id 2: Strength (strike exciter level) */
        {0, 100, 0, 80, k_unit_param_type_percent, 0, 0, 0, {"Strength"}},
        /* id 3: Mallet (strike meta - mallet to particles) */
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Mallet"}},

        // Page 2
        /* id 4: Timbre (strike exciter timbre / brightness) */
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Timbre"}},
        /* id 5: Damping (resonator damping) */
        {0, 100, 0, 25, k_unit_param_type_percent, 0, 0, 0, {"Damping"}},
        /* id 6: Brightness (resonator brightness / spectral tilt) */
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Brightness"}},
        /* id 7: Base Note (MIDI note for gate trigger) */
        {0, 127, 60, 60, k_unit_param_type_midi_note, 0, 0, 0, {"Base Note"}},

        // Page 3
        /* id 8: LFO Target (which param the shape LFO modulates) */
        {0, 6, 0, 0, k_unit_param_type_enum, 0, 0, 0, {"LFO Target"}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},

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

#else
    /* ================================================================
     * Plaits oscillators (macro-oscillator2.cc)
     *
     * 9 params: Shape, ShiftShape, Param 1, Param 2, Base Note,
     *           LFO Target, LFO2 Rate, LFO2 Depth, LFO2 Target
     * ================================================================ */
    .num_params = 9,
    .params = {
        // Page 1
        /* id 0: Shape */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"Shape"}},
        /* id 1: Shift-Shape */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"ShiftShape"}},
        /* id 2: Param 1 (bipolar) */
        {0, 100, 50, 50, k_unit_param_type_percent, 0, 0, 0, {"Param 1"}},
        /* id 3: Param 2 */
        {0, 100, 50, 50, k_unit_param_type_percent, 0, 0, 0, {"Param 2"}},

        // Page 2
        /* id 4: Base Note (MIDI note for gate trigger) */
        {0, 127, 60, 60, k_unit_param_type_midi_note, 0, 0, 0, {"Base Note"}},
        /* id 5: LFO Target (which param the shape LFO modulates) */
        {0, 7, 0, 0, k_unit_param_type_enum, 0, 0, 0, {"LFO Target"}},
        /* id 6: LFO2 Rate */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"LFO2 Rate"}},
        /* id 7: LFO2 Depth */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"LFO2 Depth"}},

        // Page 3
        /* id 8: LFO2 Target (which param LFO2 modulates) */
        {0, 7, 0, 0, k_unit_param_type_enum, 0, 0, 0, {"LFO2 Target"}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},

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
