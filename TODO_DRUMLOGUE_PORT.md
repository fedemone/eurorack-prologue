# Drumlogue Port - TODO List

## Completed ✅
1. Created `drumlogue-port` branch
2. Added DRUMLOGUE_PORT.md documentation
3. Updated Makefile with drumlogue platform support
4. Created drumlogue wrapper infrastructure
   - `drumlogue_osc_adapter.h` - OSC API adapter header
   - `drumlogue_osc_adapter.cc` - OSC API adapter implementation
   - `drumlogue_unit_wrapper.cc` - Unit wrapper implementation
5. Created `userosc.h` - User oscillator API header with type definitions
6. Updated all oscillator `.mk` files for drumlogue support:
   - Macro oscillators: va, wsh, fm, grn, add, string
   - Wavetable oscillators: wta, wtb, wtc, wtd, wte, wtf
   - Modal strike oscillators: modal_strike, modal_strike_16_nolimit, modal_strike_24_nolimit
7. Implemented complete build system in `makefile.inc`:
   - Platform detection and configuration
   - ARM Cortex-A7 compiler flags with NEON vectorization
   - Shared library build for drumlogue
   - Integration with logue-SDK for other platforms
   - Packaging targets for all platforms

## Remaining Work 🔄

### 1. Build and Test
- [ ] Install ARM cross-compiler toolchain (arm-linux-gnueabihf-gcc)
- [ ] Test actual compilation of all oscillators for drumlogue
- [ ] Verify `.drmlgunit` file generation
- [ ] Test with logue-sdk Docker environment (if available)
- [ ] Verify unit manifest generation

### 2. Code Verification & Testing
- [ ] Verify ARM Cortex-A7 compatibility (no Cortex-M4 specific code)
- [ ] Test NEON vectorization optimizations
- [ ] Verify buffer size (48 samples) compatibility with all oscillators
- [ ] Test sample rate (48kHz) with existing code
- [ ] Ensure no hard-coded platform assumptions

### 3. API Adapter Implementation Review
- [ ] Review OSC API to Synth Module API mappings
- [ ] Verify parameter scaling is correct
- [ ] Test note on/off triggers work properly
- [ ] Validate LFO and shape/shift-shape parameter handling

### 4. Hardware Testing
- [ ] Test on actual drumlogue hardware (if available)
- [ ] Performance benchmarking
- [ ] Audio quality verification
- [ ] Memory usage verification

### 5. Documentation Updates
- [ ] Update main README.md with drumlogue instructions
- [ ] Add build instructions for drumlogue
- [ ] Document any drumlogue-specific limitations
- [ ] Add installation guide for `.drmlgunit` files
- [ ] Create troubleshooting section

### 6. Release Preparation
- [ ] Update version numbers
- [ ] Create release notes
- [ ] Build all oscillators for drumlogue
- [ ] Package `.drmlgunit` files
- [ ] Update credits and acknowledgments

## Technical Implementation Notes

### Build System Architecture
The build system now supports four platforms:
- **prologue**: ARM Cortex-M4, .prlgunit files
- **minilogue-xd**: ARM Cortex-M4, .mnlgxdunit files  
- **nutekt-digital**: ARM Cortex-M4, .ntkdigunit files
- **drumlogue**: ARM Cortex-A7, .drmlgunit files (shared libraries)

### Drumlogue-Specific Implementation

#### Wrapper Architecture
```
User Code (OSC API)
       ↓
drumlogue_osc_adapter.cc (OSC API Implementation)
       ↓
drumlogue_unit_wrapper.cc (Synth Module API)
       ↓
Drumlogue Runtime
```

#### Key Differences from Other Platforms
1. **Architecture**: ARM Cortex-A7 with NEON SIMD instead of Cortex-M4
2. **Build Type**: Shared library (.so → .drmlgunit) instead of static binary
3. **API**: Synth Module API instead of direct OSC API
4. **Toolchain**: arm-linux-gnueabihf-gcc instead of arm-none-eabi-gcc
5. **Optimization**: NEON vectorization flags for better performance

#### Compilation Flags for Drumlogue
- `-mcpu=cortex-a7`: Target ARM Cortex-A7 processor
- `-mfpu=neon-vfpv4`: Enable NEON SIMD instructions
- `-mfloat-abi=hard`: Use hardware floating-point
- `-march=armv7ve+simd`: Target ARMv7VE with SIMD extensions
- `-ftree-vectorize`: Enable auto-vectorization
- `-ffast-math`: Optimize floating-point operations
- `-fPIC`: Position-independent code for shared library
- `-shared`: Create shared library

### Build Process

#### For Prologue/Minilogue XD/NTS-1:
```bash
make PLATFORM=prologue VERSION=1.6-1 -f osc_va.mk
```
Produces: `mo2_va.prlgunit` (ZIP containing binary + manifest)

#### For Drumlogue:
```bash
make PLATFORM=drumlogue VERSION=1.6-1 -f osc_va.mk  
```
Produces: `mo2_va.drmlgunit` (shared library renamed)

### Parameter Mapping

OSC API parameters are mapped to Synth Module API:
- Shape → `k_user_osc_param_shape`
- Shift-Shape → `k_user_osc_param_shiftshape`
- Param 1-6 → `k_user_osc_param_id1` through `id6`
- LFO → `shape_lfo`

## Questions to Resolve ❓
1. Does drumlogue have different parameter mapping requirements?
2. Are there drumlogue-specific features to leverage?
3. What are the exact memory/CPU limitations?
4. How does the unit installation process work on drumlogue?
5. Are there any licensing considerations for drumlogue platform?

## Known Limitations
- ARM cross-compiler toolchain required for building drumlogue units
- No drumlogue platform support in official logue-SDK yet (using custom wrapper)
- Hardware testing required to verify functionality
- Modal oscillator (OSC_MODAL) is disabled in original project

## Nice to Have 🌟
- [ ] Port effects if applicable
- [ ] Add drumlogue-specific presets
- [ ] Create video demonstrations
- [ ] Add CI/CD for automated builds
- [ ] Create issue templates for drumlogue-specific bugs

## Notes
- Original project (peterall/eurorack-prologue) supports prologue, minilogue-xd, and NTS-1
- Drumlogue uses different architecture but same sample rate (48kHz)
- Key differences: ARM Cortex-A7, 48-sample buffers, Synth Module API
- Reference: https://github.com/korginc/logue-sdk (when drumlogue support is added)

---
**Last Updated:** 2026-01-09
**Branch:** copilot/port-eurorack-oscillators
**Status:** Core integration complete, ready for toolchain testing
