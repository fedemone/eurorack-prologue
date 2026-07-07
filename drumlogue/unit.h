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
 * @file    unit.h
 * @brief   Unit API function declarations for drumlogue synth modules.
 *
 * Based on logue-sdk v2.0 platform/drumlogue/common/unit.h
 */

#ifndef DRUMLOGUE_UNIT_H_
#define DRUMLOGUE_UNIT_H_

#include <stdint.h>
#include "attributes.h"
#include "runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const unit_header_t unit_header;

/* Lifecycle */
int8_t unit_init(const unit_runtime_desc_t *desc);
void unit_teardown(void);
void unit_reset(void);
void unit_resume(void);
void unit_suspend(void);

/* Audio rendering */
void unit_render(const float *in, float *out, uint32_t frames);

/* Presets */
uint8_t unit_get_preset_index(void);
const char * unit_get_preset_name(uint8_t idx);
void unit_load_preset(uint8_t idx);

/* Parameters */
int32_t unit_get_param_value(uint8_t id);
const char * unit_get_param_str_value(uint8_t id, int32_t value);
const uint8_t * unit_get_param_bmp_value(uint8_t id, int32_t value);
void unit_set_param_value(uint8_t id, int32_t value);

/* Tempo */
void unit_set_tempo(uint32_t tempo);

/* Synth-specific note/MIDI control */
void unit_note_on(uint8_t note, uint8_t velocity);
void unit_note_off(uint8_t note);
void unit_gate_on(uint8_t velocity);
void unit_gate_off(void);
void unit_all_note_off(void);
void unit_pitch_bend(uint16_t bend);
void unit_channel_pressure(uint8_t pressure);
void unit_aftertouch(uint8_t note, uint8_t aftertouch);

#ifdef __cplusplus
}
#endif

#endif /* DRUMLOGUE_UNIT_H_ */
