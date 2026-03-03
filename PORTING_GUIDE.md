# Porting Guide: Adding a New Oscillator Module

This document explains how to port an additional Mutable Instruments DSP module
(or any OSC API-compatible source) to the drumlogue using the existing adapter layer.

## Prerequisites

- Working knowledge of the target DSP module's API (Init, Process, parameters)
- The eurorack submodule cloned (`git submodule update --init`)
- Docker installed (for ARM cross-compilation)
- Host-side tests passing (`make test-all`)

## Architecture Overview

```
Drumlogue Runtime
     |  unit_init / unit_render / unit_note_on / unit_set_param_value
     v
drumlogue_unit_wrapper.cc     <-- param mapping (compile-time #ifdef)
     |  osc_adapter_init / osc_adapter_render / osc_adapter_note_on
     v
drumlogue_osc_adapter.cc      <-- buffered rendering, Q31<->float, pitch
     |  OSC_INIT / OSC_CYCLE / OSC_NOTEON / OSC_NOTEOFF / OSC_PARAM
     v
your_new_module.cc            <-- implements OSC_* functions (unchanged DSP)
```

**Key principle**: The oscillator source file implements the OSC API
(`OSC_INIT`, `OSC_CYCLE`, `OSC_NOTEON`, `OSC_NOTEOFF`, `OSC_PARAM`).
The adapter and wrapper handle all drumlogue-specific concerns.

## Step-by-Step Guide

### Step 1: Create the Oscillator Source File

Create a new `.cc` file (e.g., `rings-resonator.cc`) that implements the
five OSC API functions:

```cpp
#include "userosc.h"
// Include the target DSP module headers from eurorack/
#include "rings/dsp/part.h"
#include "rings/resources.h"

// Static state (no dynamic allocation)
static rings::Part part_;
static uint16_t reverb_buffer_[32768];
static rings::Patch patch_;
static rings::PerformanceState perf_state_;

// Parameter storage
uint16_t p_values[6] = {0};
float shape = 0, shiftshape = 0;

void OSC_INIT(uint32_t platform, uint32_t api) {
    (void)platform; (void)api;
    part_.Init(reverb_buffer_);
    // Set defaults...
}

void OSC_CYCLE(const user_osc_param_t *const params,
               int32_t *yn, const uint32_t frames) {
    // 1. Read params->pitch, params->shape_lfo
    // 2. Update patch_ from p_values[]
    // 3. Call part_.Process(perf_state_, patch_, ...)
    // 4. Convert float output to Q31 and write to yn[]
}

void OSC_NOTEON(const user_osc_param_t *const params) {
    // Trigger note on
}

void OSC_NOTEOFF(const user_osc_param_t *const params) {
    // Trigger note off
}

void OSC_PARAM(uint16_t index, uint16_t value) {
    switch (index) {
    case k_user_osc_param_shape:
        shape = param_val_to_f32(value);
        break;
    case k_user_osc_param_shiftshape:
        shiftshape = param_val_to_f32(value);
        break;
    case k_user_osc_param_id1:
    case k_user_osc_param_id2:
    case k_user_osc_param_id3:
    case k_user_osc_param_id4:
    case k_user_osc_param_id5:
    case k_user_osc_param_id6:
        p_values[index] = value;
        break;
    default:
        break;
    }
}
```

**Important constraints**:
- No `malloc`/`new` at runtime -- all memory must be static or stack
- `OSC_CYCLE` output is Q31 (`int32_t`). Use `f32_to_q31()` to convert float output.
- The `frames` parameter tells you how many samples to produce, but your DSP
  may have a fixed block size. The adapter handles buffering for you.

### Step 2: Set the Block Size

Determine your module's native processing block size:

| Module | Block Size | Constant |
|--------|-----------|----------|
| Plaits engines | 24 | `plaits::kMaxBlockSize` |
| Elements | 16 (x2 FIR = 32 output) | `elements::kMaxBlockSize` |
| Rings | 24 | `rings::kMaxBlockSize` |
| Clouds | 32 | `clouds::kMaxBlockSize` |

This becomes `OSC_NATIVE_BLOCK_SIZE` in your `.mk` file (Step 4).

### Step 3: Add Parameter Layout to header.c

Add a new `#ifdef` block in `header.c` for your oscillator's unit identity
and parameter layout:

```c
// In the unit_id / name section:
#elif defined(OSC_RINGS)
    .unit_id = 0x52494E47U,   /* 'RING' */
    .version = 0x00010800U,
    .name = "Rings",

// In the params section:
#elif defined(OSC_RINGS)
    .num_params = 10,
    .params = {
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Structure"}},
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Brightness"}},
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Damping"}},
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Position"}},
        {0, 5,  0, 0,  k_unit_param_type_enum,    0, 0, 0, {"Model"}},
        {0, 3,  0, 0,  k_unit_param_type_enum,    0, 0, 0, {"Polyphony"}},
        {0, 1,  0, 1,  k_unit_param_type_enum,    0, 0, 0, {"Int Exciter"}},
        {0, 127,60,60, k_unit_param_type_midi_note,0, 0, 0, {"Base Note"}},
        // ... up to 24 params total, pad with k_unit_param_type_none
    }
```

Parameter type reference:
- `k_unit_param_type_percent`: 0-100% display
- `k_unit_param_type_enum`: Integer selection
- `k_unit_param_type_midi_note`: MIDI note display (C-1 to G9)
- `k_unit_param_type_none`: Unused slot (required padding to fill 24 slots)

### Step 4: Add Parameter Mapping to drumlogue_unit_wrapper.cc

Add a new `#elif` block inside `unit_set_param_value()`:

```cpp
#elif defined(OSC_RINGS)
  switch (id) {
    case 0: /* Structure: 0-100 -> 10-bit */
      osc_id    = k_user_osc_param_shape;
      osc_value = (uint16_t)((value * 1023 + 50) / 100);
      break;
    case 1: /* Brightness: 0-100 percent */
      osc_id    = k_user_osc_param_id1;
      osc_value = (uint16_t)value;
      break;
    // ... etc
    case 7: /* Base Note */
      s_state.base_note = (uint8_t)(value & 0x7F);
      return;
    default:
      return;
  }
```

### Step 5: Create the Build File

Create `osc_rings.mk`:

```makefile
OSCILLATOR = rings
PROJECT = $(OSCILLATOR)

OSC_DDEFS = -DOSC_RINGS

UCXXSRC = rings-resonator.cc \
    eurorack/rings/dsp/part.cc \
    eurorack/rings/dsp/resonator.cc \
    eurorack/rings/dsp/string.cc \
    eurorack/rings/dsp/fm_voice.cc \
    eurorack/rings/resources.cc \
    eurorack/stmlib/dsp/units.cc \
    eurorack/stmlib/utils/random.cc

# Drumlogue wrapper
ifeq ($(PLATFORM),drumlogue)
    UCSRC = header.c
    UCXXSRC += drumlogue_osc_adapter.cc drumlogue_unit_wrapper.cc
    OSC_DDEFS += -DOSC_NATIVE_BLOCK_SIZE=24
endif

include makefile.inc
```

### Step 6: Add to generate_sdk_projects.sh

Add a `create_project` call for the new oscillator:

```bash
create_project "rings" "rings" \
    "rings-resonator.cc" \
    "eurorack/rings/dsp/part.cc eurorack/rings/dsp/resonator.cc ..." \
    "-DOSC_RINGS -DOSC_NATIVE_BLOCK_SIZE=24" \
    "24"
```

### Step 7: Add Tests

Add conditional test registration in `test_drumlogue_callbacks.cc`:

```cpp
#if defined(OSC_RINGS)
TEST(rings_param_structure) {
  init_unit();
  unit_set_param_value(0, 50);
  ASSERT_EQ(k_user_osc_param_shape, g_mock.last_param_index);
  ASSERT_EQ(512, g_mock.last_param_value); /* (50*1023+50)/100 */
  teardown_unit();
}
// ... more param tests
#endif
```

Add a Makefile target:

```makefile
test-rings:
    $(CXX) $(COMMON_TEST_FLAGS) -DOSC_NATIVE_BLOCK_SIZE=24 \
        -DOSC_RINGS \
        test_drumlogue_callbacks.cc $(COMMON_TEST_SRC) \
        -o test_drumlogue_callbacks -lm
    ./test_drumlogue_callbacks
```

### Step 8: Build and Test

```bash
# 1. Run host-side tests (no ARM needed)
make test-rings

# 2. Generate SDK project structure
./generate_sdk_projects.sh

# 3. Build via Docker
./build_drumlogue.sh rings

# 4. Install on drumlogue
# Copy the resulting .drmlgunit to Units/Synths/
```

## Stereo Output Modules

The current adapter assumes **mono output** from `OSC_CYCLE`, which is then
duplicated to stereo in `unit_render`. For modules with native stereo output
(Clouds, Rings), there are two approaches:

### Approach A: Interleaved Stereo in Q31

Write interleaved L/R samples to the `yn` buffer in `OSC_CYCLE`:
```cpp
// yn[i*2]   = left channel (Q31)
// yn[i*2+1] = right channel (Q31)
```
Set `OSC_NATIVE_BLOCK_SIZE` to `2 * actual_block_size` (this is how
`modal-strike.cc` already works with its FIR filter output).

### Approach B: Custom unit_render

For modules that produce float stereo natively, bypass the adapter's
mono-to-stereo path and write directly in a custom `unit_render` override.
This requires modifying `drumlogue_unit_wrapper.cc` with another `#ifdef`.

## Checklist

- [ ] Oscillator source file implements all 5 OSC functions
- [ ] No dynamic allocation (`malloc`/`new`) at runtime
- [ ] `OSC_NATIVE_BLOCK_SIZE` set correctly in `.mk` file
- [ ] `header.c` has unit identity (`unit_id`, `name`) and parameter layout
- [ ] `drumlogue_unit_wrapper.cc` has parameter mapping for new `#ifdef`
- [ ] `.mk` build file lists all required source files
- [ ] `generate_sdk_projects.sh` updated with new project
- [ ] Host-side tests added and passing
- [ ] `make test-all` still passes (no regressions)
- [ ] Docker build produces `.drmlgunit` output
