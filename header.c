/**
 *  @file header.c
 *  @brief drumlogue SDK unit header for Eurorack oscillator port
 *
 *  Following the logue-sdk v2.0 convention, the unit header is defined
 *  in a separate C file and placed in the .unit_header ELF section.
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
    .dev_id = 0x0U,
    .unit_id = 0x0U,
    .version = 0x00010600U,   /* v1.6.0 matching project version */
    .name = "EurorkOSC",      /* max 13 chars */
    .num_presets = 0,
    .num_params = 6,
    .params = {
        // Page 1
        // Format: min, max, center, default, type, fractional, frac. type, <reserved>, name

        /* id 0: Shape */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"Shape"}},
        /* id 1: Shift-Shape */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"ShiftShape"}},
        /* id 2: Param 1 (bipolar) */
        {0, 100, 50, 50, k_unit_param_type_percent, 0, 0, 0, {"Param 1"}},
        /* id 3: Param 2 */
        {0, 100, 50, 50, k_unit_param_type_percent, 0, 0, 0, {"Param 2"}},

        // Page 2
        /* id 4: LFO Target (enum) */
        {0, 7, 0, 0, k_unit_param_type_enum, 0, 0, 0, {"LFO Target"}},
        /* id 5: LFO2 Rate */
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"LFO2 Rate"}},
        /* Remaining slots unused */
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},

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
};
