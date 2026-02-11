/*
    BSD 3-Clause License

    Copyright (c) 2018-2022, KORG INC.
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this
      list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.

    * Neither the name of the copyright holder nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
    FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
    OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

//*/

/**
 * @file    runtime.h
 * @brief   Core type definitions for drumlogue units.
 *
 * Based on logue-sdk v2.0 platform/drumlogue/common/runtime.h
 */

#ifndef DRUMLOGUE_RUNTIME_H_
#define DRUMLOGUE_RUNTIME_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/* Constants                                                                 */
/*===========================================================================*/

#define UNIT_HEADER_SIZE       (0x1000)
#define UNIT_MAX_PARAM_COUNT   (24)
#define UNIT_PARAM_NAME_LEN    (12)
#define UNIT_NAME_LEN          (13)

/*===========================================================================*/
/* API Version                                                               */
/*===========================================================================*/

enum {
  k_unit_api_1_0_0 = ((1U << 16) | (0U << 8) | (0U)),
  k_unit_api_1_1_0 = ((1U << 16) | (1U << 8) | (0U)),
  k_unit_api_2_0_0 = ((2U << 16) | (0U << 8) | (0U)),
};

#define UNIT_API_VERSION (k_unit_api_2_0_0)

/*===========================================================================*/
/* Module Types                                                              */
/*===========================================================================*/

enum {
  k_unit_module_global = 0U,
  k_unit_module_modfx,
  k_unit_module_delfx,
  k_unit_module_revfx,
  k_unit_module_osc,
  k_unit_module_synth,
  k_unit_module_masterfx,
  k_num_unit_modules,
};

/*===========================================================================*/
/* Target Platform                                                           */
/*===========================================================================*/

enum {
  k_unit_target_drumlogue          = (4U << 8),
  k_unit_target_drumlogue_delfx    = (4U << 8) | k_unit_module_delfx,
  k_unit_target_drumlogue_revfx    = (4U << 8) | k_unit_module_revfx,
  k_unit_target_drumlogue_synth    = (4U << 8) | k_unit_module_synth,
  k_unit_target_drumlogue_masterfx = (4U << 8) | k_unit_module_masterfx,
};

#define UNIT_TARGET_PLATFORM (k_unit_target_drumlogue)

/*===========================================================================*/
/* Error Codes                                                               */
/*===========================================================================*/

enum {
  k_unit_err_none        =   0,
  k_unit_err_target      =  -1,
  k_unit_err_api_version =  -2,
  k_unit_err_samplerate  =  -4,
  k_unit_err_geometry    =  -8,
  k_unit_err_memory      = -16,
  k_unit_err_undef       = -32,
};

/*===========================================================================*/
/* Parameter Types                                                           */
/*===========================================================================*/

enum {
  k_unit_param_type_none = 0U,
  k_unit_param_type_percent,
  k_unit_param_type_db,
  k_unit_param_type_cents,
  k_unit_param_type_semi,
  k_unit_param_type_oct,
  k_unit_param_type_hertz,
  k_unit_param_type_khertz,
  k_unit_param_type_bpm,
  k_unit_param_type_msec,
  k_unit_param_type_sec,
  k_unit_param_type_enum,
  k_unit_param_type_strings,
  k_unit_param_type_bitmaps,
  k_unit_param_type_drywet,
  k_unit_param_type_pan,
  k_unit_param_type_spread,
  k_unit_param_type_onoff,
  k_unit_param_type_midi_note,
  k_num_unit_param_type
};

enum {
  k_unit_param_frac_mode_fixed   = 0U,
  k_unit_param_frac_mode_decimal = 1U,
};

/*===========================================================================*/
/* Sample Wrapper                                                            */
/*===========================================================================*/

#define UNIT_SAMPLE_WRAPPER_MAX_NAME_LEN 31

#pragma pack(push, 1)
typedef struct sample_wrapper {
  uint8_t  bank;
  uint8_t  index;
  uint8_t  channels;
  uint8_t  _padding;
  char     name[UNIT_SAMPLE_WRAPPER_MAX_NAME_LEN + 1];
  size_t   frames;
  const float *sample_ptr;
} sample_wrapper_t;
#pragma pack(pop)

/*===========================================================================*/
/* Runtime Descriptor                                                        */
/*===========================================================================*/

typedef uint8_t (*unit_runtime_get_num_sample_banks_ptr)(void);
typedef uint8_t (*unit_runtime_get_num_samples_for_bank_ptr)(uint8_t);
typedef const sample_wrapper_t * (*unit_runtime_get_sample_ptr)(uint8_t, uint8_t);

#pragma pack(push, 1)
typedef struct unit_runtime_desc {
  uint16_t target;
  uint32_t api;
  uint32_t samplerate;
  uint16_t frames_per_buffer;
  uint8_t  input_channels;
  uint8_t  output_channels;
  unit_runtime_get_num_sample_banks_ptr        get_num_sample_banks;
  unit_runtime_get_num_samples_for_bank_ptr    get_num_samples_for_bank;
  unit_runtime_get_sample_ptr                  get_sample;
} unit_runtime_desc_t;
#pragma pack(pop)

/*===========================================================================*/
/* Parameter Descriptor                                                      */
/*===========================================================================*/

#pragma pack(push, 1)
typedef struct unit_param {
  int16_t  min;
  int16_t  max;
  int16_t  center;
  int16_t  init;
  uint8_t  type;
  uint8_t  frac      : 4;
  uint8_t  frac_mode : 1;
  uint8_t  reserved  : 3;
  char     name[UNIT_PARAM_NAME_LEN + 1];
} unit_param_t;
#pragma pack(pop)

/*===========================================================================*/
/* Unit Header                                                               */
/*===========================================================================*/

#pragma pack(push, 1)
typedef struct unit_header {
  uint32_t     header_size;
  uint16_t     target;
  uint32_t     api;
  uint32_t     dev_id;
  uint32_t     unit_id;
  uint32_t     version;
  char         name[UNIT_NAME_LEN + 1];
  uint32_t     num_presets;
  uint32_t     num_params;
  unit_param_t params[UNIT_MAX_PARAM_COUNT];
} unit_header_t;
#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif /* DRUMLOGUE_RUNTIME_H_ */
