# Drumlogue Port of Eurorack Oscillators

This branch adds support for the **Korg drumlogue** platform to the eurorack-prologue project.

## Key Differences: Minilogue XD vs Drumlogue

### Architecture
- **Minilogue XD**: ARM Cortex-M4 (bare metal)
- **Drumlogue**: ARM Cortex-A7 (Linux-based)

### SDK Version
- **Minilogue XD**: logue SDK v1.1.0
- **Drumlogue**: logue SDK v2.0.0

### Module Type
- **Minilogue XD**: User Oscillators (k_unit_module_osc)
- **Drumlogue**: Synth Modules (k_unit_module_synth)

### Build Output
- **Minilogue XD**: `.mnlgxdunit` files (static executables)
- **Drumlogue**: `.drmlgunit` files (shared objects/dynamic libraries)

### Toolchain
- **Minilogue XD**: `arm-none-eabi-gcc` (embedded)
- **Drumlogue**: `arm-unknown-linux-gnueabihf-gcc` (Linux ARM)

## Changes Made

### 1. Main Makefile
Added drumlogue to the build targets:
- Build loop now includes `PLATFORM=drumlogue`
- Added `package_drumlogue` target for `.drmlgunit` packaging

### 2. makefile.inc
Added drumlogue-specific configuration:
- Package suffix: `drmlgunit`
- Block size: 64 (same as nutekt-digital)
- MCU: `cortex-a7`
- Toolchain: `arm-unknown-linux-gnueabihf-`
- FPU: NEON VFPV4 with vectorization
- Compilation mode: ARM (not Thumb)
- Build type: Shared library (`-fPIC`, `-shared`)

### 3. API Adaptations Required

The oscillator code needs to be adapted to work as synth modules:

#### Original Oscillator API (Minilogue XD):
```c++
void OSC_INIT(uint32_t platform, uint32_t api)
void OSC_CYCLE(const user_osc_param_t *const params, int32_t *yn, const uint32_t frames)
void OSC_NOTEON(const user_osc_param_t *const params)
void OSC_NOTEOFF(const user_osc_param_t *const params)
void OSC_PARAM(uint16_t index, uint16_t value)
```

#### Drumlogue Synth API:
```c++
int8_t unit_init(const unit_runtime_desc_t * desc)
void unit_teardown()
void unit_render(const float * in, float * out, uint32_t frames)
void unit_note_on(uint8_t note, uint8_t velocity)
void unit_note_off(uint8_t note)
void unit_set_param_value(uint8_t id, int32_t value)
```

## Building for Drumlogue

### Prerequisites
1. Clone with submodules:
   ```bash
   git clone --recursive https://github.com/fedemone/eurorack-prologue.git
   cd eurorack-prologue
   git checkout drumlogue-port
   ```

2. Install Docker (recommended) or set up the drumlogue toolchain manually

### Using Docker (Recommended)
```bash
cd logue-sdk/docker
./build_image.sh
./run_interactive.sh

# Inside container:
env drumlogue
cd /workspace
make
```

### Manual Build
```bash
# Ensure drumlogue toolchain is installed
make PLATFORM=drumlogue
```

## Current Status

✅ **Completed:**
- Main Makefile updated with drumlogue support
- makefile.inc configured for Cortex-A7 and shared library build
- Packaging targets for `.drmlgunit` files

⚠️ **In Progress:**
- Oscillator wrapper to adapt OSC API to Synth API
- Testing with actual drumlogue hardware

❌ **TODO:**
- Port each oscillator module (mo2_va, mo2_fm, mo2_grn, etc.)
- Adapt Mutable Instruments code for NEON optimization
- Test memory footprint (no 32K limit like Cortex-M4)
- Create manifest files for drumlogue
- Update README with drumlogue-specific instructions

## Testing

Once built, transfer `.drmlgunit` files to drumlogue using the Korg Sound Librarian or manual file transfer.

## Notes

- Drumlogue has more CPU power and memory than Minilogue XD
- NEON SIMD instructions should be utilized for better performance
- Sample rate is 48kHz (same as Minilogue XD)
- Drumlogue is designed for drum synthesis but can work with any synth module

## References

- [Korg logue SDK](https://github.com/korginc/logue-sdk)
- [Drumlogue Platform Documentation](https://github.com/korginc/logue-sdk/tree/master/platform/drumlogue)
- [Original Eurorack-Prologue](https://github.com/peterall/eurorack-prologue)