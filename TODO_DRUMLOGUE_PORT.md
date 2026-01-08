# Drumlogue Port - TODO List

## Completed ✅
1. Created `drumlogue-port` branch
2. Added DRUMLOGUE_PORT.md documentation
3. Updated Makefile with drumlogue platform support
4. Created `drumlogue/` directory structure
5. Added OSC API adapter header (`drumlogue/osc_api_adapter.h`)
6. Added unit wrapper implementation (`drumlogue/unit_wrapper.cpp`)

## Remaining Work 🔄

### 1. Code Verification & Testing
- [ ] Verify ARM Cortex-A7 compiler flags are correct
- [ ] Test NEON vectorization optimizations
- [ ] Verify buffer size (48 samples) compatibility with all oscillators
- [ ] Test sample rate (48kHz) with existing code
- [ ] Ensure no Cortex-M4 specific code remains

### 2. Build System
- [ ] Test `make drumlogue` command builds successfully
- [ ] Verify `.drunit` file generation
- [ ] Test with logue-sdk Docker environment
- [ ] Verify unit manifest generation

### 3. API Adapter Implementation Review
- [ ] Review OSC API to Synth Module API mappings
- [ ] Verify parameter scaling is correct
- [ ] Test note on/off triggers work properly
- [ ] Validate LFO and shape/shift-shape parameter handling

### 4. Individual Oscillator Ports
For each oscillator in the project:
- [ ] Braids (macro oscillator)
- [ ] Modal Resonator (Elements)
- [ ] String Machine
- [ ] Wavetable oscillators
- [ ] Other Eurorack oscillators

Tasks per oscillator:
- [ ] Create drumlogue-specific unit file
- [ ] Adapt any platform-specific code
- [ ] Test on actual drumlogue hardware (if available)
- [ ] Update documentation with drumlogue-specific notes

### 5. Documentation Updates
- [ ] Update main README.md with drumlogue instructions
- [ ] Add build instructions for drumlogue
- [ ] Document any drumlogue-specific limitations
- [ ] Add installation guide for `.drunit` files
- [ ] Create troubleshooting section

### 6. Code Quality & Compatibility
- [ ] Review for memory usage (drumlogue constraints)
- [ ] Check CPU usage (ARM Cortex-A7 vs Cortex-M4)
- [ ] Ensure no hard-coded sample rates or buffer sizes
- [ ] Review for potential optimization opportunities
- [ ] Add error handling where needed

### 7. Testing Plan
- [ ] Unit tests for API adapter
- [ ] Integration tests with logue-sdk
- [ ] Real hardware testing on drumlogue
- [ ] Performance benchmarking
- [ ] Audio quality verification

### 8. Release Preparation
- [ ] Update version numbers
- [ ] Create release notes
- [ ] Build all oscillators for drumlogue
- [ ] Package `.drunit` files
- [ ] Update credits and acknowledgments

## Questions to Resolve ❓
1. Does drumlogue have different parameter mapping requirements?
2. Are there drumlogue-specific features to leverage?
3. What are the exact memory/CPU limitations?
4. How does the unit installation process work on drumlogue?
5. Are there any licensing considerations for drumlogue platform?

## Nice to Have 🌟
- [ ] Port effects if applicable
- [ ] Add drumlogue-specific presets
- [ ] Create video demonstrations
- [ ] Add CI/CD for automated builds
- [ ] Create issue templates for drumlogue-specific bugs

## Notes
- Original project (peterall/eurorack-prologue) supports prologue, minilogue-xd, and NTS-1
- Drumlogue uses same logue-sdk but with different APIs
- Key differences: ARM Cortex-A7, 48kHz, 48-sample buffers, Synth Module API
- Reference: https://github.com/korginc/logue-sdk/tree/master/platform/drumlogue

---
**Last Updated:** 2026-01-08
**Branch:** drumlogue-port
**Status:** In Progress - Core infrastructure complete, oscillator ports pending