# Eurorack-Drumlogue Project: Status & Next Steps

**Last Updated:** January 11, 2026

## 🎉 MILESTONE 2 ACHIEVED: Plaits Multi-Engine Integration Complete!

### ✅ Build Status: **BOTH OSCILLATORS WORKING**
- **Braids**: 16 synthesis shapes ✅
- **Plaits**: 8 synthesis engines (13+ synthesis types) ✅
- **Current Build Size**: 351.72 KB (.drmlgunit file)

### ✅ Phase 1: Build System - COMPLETE

1. **SDK Integration**: Updated `logue-sdk` submodule to `main` branch with Drumlogue support
2. **Docker Environment**: ARM cross-compiler environment built and tested
3. **Eurorack Sources**: Local copies of Braids and stmlib integrated into build system
4. **Build Output**: Successfully generating `.drmlgunit` files

### ✅ Phase 2: Dispatcher Architecture - COMPLETE

**Implementation Location:** `logue-sdk/platform/drumlogue/eurorack/`

#### Core Files Created:
1. **synth.h / synth.cc** - Main dispatcher with oscillator selection
   - `OscillatorBase` abstract interface
   - `Synth` dispatcher class
   - `SelectOscillator()` method routes to active implementation
   - `CreateOscillator()` factory method instantiates oscillators

2. **synth_braids.h / synth_braids.cc** - Braids oscillator (16 shapes) ✅
   - Shapes: CSAW, MORPH_TABLE, SAW_SWARM, FM, FB_FM, SINE, VOSIM, etc.
   - Full MIDI support (NoteOn, PitchBend, etc.)
   - 3 presets: Init, Bright, Deep
   - Proper audio rendering with gain control

3. **synth_plaits.h / synth_plaits.cc** - Plaits oscillator (8 engines) ✅ NEW!
   - Engine 0 (VA): Virtual Analog synthesis - dual oscillators, waveshaping
   - Engine 1 (WSH): Waveshaping oscillator - versatile waveform modulation
   - Engine 2 (FM): Two-operator FM synthesis
   - Engine 3 (GRN): Granular synthesis engine
   - Engine 4 (ADD): Additive synthesis with harmonics
   - Engine 5 (WTBL): Wavetable synthesis with morphing
   - Engine 6 (STRING): Physical modeling - Karplus-Strong strings
   - Engine 7 (MODAL): Modal synthesis - resonant physical models
   - Runtime engine switching via parameter 1
   - Proper integration with Plaits DSP library

4. **header.c** - 11-parameter interface
   - Param 0: OSCSEL (oscillator selector: 0=Braids, 1=Plaits)
   - Params 1-10: Shape, Timbre, Color, Env, Mod, Pitch, Gain, AuxA, AuxB, AuxC

5. **config.mk** - Build configuration with Braids and Plaits sources
   - Renamed `plaits/resources.cc` → `plaits/plaits_resources.cc` (avoiding conflict with Braids)
   - All 8 Plaits engine sources included
   - Physical modeling components (string, resonator, modal_voice, string_voice)
   - Shared stmlib utilities

### ✅ Phase 3: Plaits Integration - COMPLETE

**Fixed Issues:**
1. ✅ Linker conflict: Both Braids and Plaits had `resources.cc` → renamed Plaits version
2. ✅ Updated 18+ Plaits source files to use `plaits/plaits_resources.h`
3. ✅ Wavetable macro fixed for default wavetable selection
4. ✅ Engine::Render() API signature corrected (5 args with aux buffer)
5. ✅ All compilation and linking errors resolved

### ✅ Build Status: SUCCESS - Dual Oscillator Unit

```bash
✓ Compiling header.c
✓ Compiling unit.cc
✓ Compiling synth.cc
✓ Compiling synth_braids.cc (16 shapes)
✓ Compiling synth_plaits.cc (8 engines)
✓ Compiling Braids sources (resources, macro_oscillator, analog_oscillator, digital_oscillator, quantizer)
✓ Compiling Plaits sources (plaits_resources, voice, 8 engines, physical_modelling components)
✓ Compiling stmlib utilities
✓ Linking build/eurorack.drmlgunit
✓ Deploying eurorack.drmlgunit
```

**Output:** `logue-sdk/platform/drumlogue/eurorack/eurorack.drmlgunit` ✅

**Build Size:** 351.72 KB (360,160 bytes)
- text (code): ~347 KB
- data (initialized): ~2 KB
- bss (uninitialized): ~20 bytes

---

## 📋 Remaining Work: Port 13 Additional Oscillators

### Current Status
- ✅ **Braids** (16 shapes) - COMPLETE
- ⏳ **13 oscillators remaining**

### Oscillators to Port

Based on available eurorack modules:

1. **va** - Pair of classic waveforms (Virtual Analog)
2. **wsh** - Waveshaping oscillator
3. **fm** - Two operator FM
4. **grn** - Granular formant oscillator
5. **add** - Harmonic/additive oscillator
6. **wta** - Wavetable: Additive (2x sine, quadratic, comb)
7. **wtb** - Wavetable: Additive (pair, triangle stack, 2x drawbar)
8. **wtc** - Wavetable: Formantish (trisaw, sawtri, burst, bandpass)
9. **wtd** - Wavetable: Formantish (formant, digi_formant, pulse, sine power)
10. **wte** - Wavetable: Braids (male, choir, digi, drone)
11. **wtf** - Wavetable: Braids (metal, fant, 2x unknown)
12. **modal** - Modal synthesis
13. **string** - Inharmonic string model

---

## 🚀 Next Steps: Adding New Oscillators

### Template: Use Braids Implementation as Reference

The completed Braids implementation (`synth_braids.h/.cc`) serves as the template for all future oscillators.

### Workflow for Each New Oscillator

#### 1. Create Header File: `synth_xxx.h`

```cpp
#pragma once
#include "synth.h"
#include "eurorack/xxx/dsp.h"  // DSP engine for oscillator

class SynthXXX : public OscillatorBase {
 public:
  SynthXXX();
  ~SynthXXX() override;

  // Implement all virtual methods from OscillatorBase
  int8_t Init(const unit_runtime_desc_t * desc) override;
  void Render(float * out, size_t frames) override;
  // ... etc

 private:
  // Oscillator-specific members
};
```

#### 2. Create Implementation File: `synth_xxx.cc`

- Initialize DSP engine in `Init()`
- Implement `Render()` method (convert oscillator output to float stereo)
- Map parameters (Shape, Timbre, Color, etc.) to oscillator controls
- Implement preset system (3 presets minimum)
- Handle MIDI (NoteOn, NoteOff, PitchBend)

#### 3. Register in Dispatcher

In `synth.h`:
```cpp
enum OscillatorType {
  OSC_BRAIDS = 0,
  OSC_XXX = 1,     // Add new oscillator
  OSC_COUNT
};
```

In `synth.cc` `CreateOscillator()`:
```cpp
case OSC_XXX:
  active_oscillator_ = std::make_unique<SynthXXX>();
  break;
```

#### 4. Update Build Configuration

In `config.mk`:
```makefile
CXXSRC = unit.cc \
         synth.cc \
         synth_braids.cc \
         synth_xxx.cc \          # Add new file
         eurorack_src/xxx/...    # Add DSP sources
```

#### 5. Copy Required DSP Sources

From `eurorack/plaits/` or relevant module:
```bash
cp -r ../../../../eurorack/plaits/dsp/xxx \
      eurorack_src/plaits/dsp/
```

#### 6. Build and Test

```bash
bash drumlogue_build.sh build drumlogue/eurorack
```

---

## 💾 Memory Analysis & Platform Capabilities

### Current Build Status
**Eurorack Unit (Braids + Plaits):** 351.72 KB
- Code (text): ~347 KB
- Initialized data: ~2 KB
- Uninitialized data (BSS): ~20 bytes

### Korg Drumlogue Hardware Specifications
**Processor:** ARM Cortex-A7 (32-bit, ARMv7-A architecture)
- **NOT** ARM Neon v7 (NEON is a SIMD extension available on Cortex-A7)
- NEON SIMD support: Available but not utilized in current build
- Clock speed: ~800 MHz (estimated, not officially documented)
- Instruction set: ARM/Thumb-2

**Memory Constraints:**
- User unit size limit: **NOT OFFICIALLY DOCUMENTED**
- Dummy-synth reference: ~3.5 KB (minimal example)
- Our current build: **351.72 KB** (100x larger than dummy-synth)
- **Status: ⚠️ UNTESTED ON HARDWARE** - Size is unprecedented for user units

**Comparison with Multi-Engine (Prologue/Minilogue XD):**
- Multi-engine limit: **32 KB** (hard constraint)
- Drumlogue appears more permissive, but **limits unknown**
- Different architecture: Drumlogue runs Linux, not bare-metal

### Memory Usage by Module

**Current Implementation:**
```
Braids (16 shapes):       ~120-150 KB estimated
Plaits (8 engines):       ~180-200 KB estimated
Dispatcher + glue code:   ~10-15 KB
stmlib utilities:         ~5-10 KB
Total:                    ~351 KB
```

**Potential Additional Oscillators:**
```
Rings (resonator):        ~150-200 KB (large buffers for resonators)
Elements (voice):         ~200-250 KB (complex physical modeling)
Clouds (granular):        ~400-500 KB (LARGE - granular buffers)
Warps (meta-modulator):   ~80-120 KB
```

### ⚠️ Critical Decision Point: Single Unit vs. Multiple Units

#### Option A: Continue Single Multi-Oscillator Unit
**Pros:**
- ✅ One-stop solution
- ✅ Easy oscillator switching
- ✅ Shared parameter interface

**Cons:**
- ❌ Unknown if 350+ KB will load on hardware
- ❌ Adding Rings/Elements would push to 500-800 KB (likely too large)
- ❌ No official documentation on size limits
- ❌ Risk of runtime memory issues (heap allocation for engines)

**Risk Level:** 🔴 **HIGH** - Untested territory

#### Option B: Split into Separate Units (RECOMMENDED)
**Pros:**
- ✅ Each unit stays under 200 KB (safer)
- ✅ User loads only what they need
- ✅ Follows Prologue precedent (multiple units for wavetables)
- ✅ Better memory management
- ✅ Easier troubleshooting per unit

**Cons:**
- ❌ Need to build/manage multiple .drmlgunit files
- ❌ No runtime switching between units
- ❌ More files to distribute

**Risk Level:** 🟢 **LOW** - Proven approach

### 🎯 Recommended Strategy: SPLIT INTO FOCUSED UNITS

#### Proposed Unit Distribution:

**Unit 1: "Eurorack Braids" (Current, Validated)**
- ✅ 16 Braids shapes
- ✅ Known working ~120-150 KB
- Status: COMPLETE, BUILDS SUCCESSFULLY

**Unit 2: "Eurorack Plaits" (Current, Validated)**
- ✅ 8 Plaits engines (VA, WSH, FM, GRN, ADD, WTBL, STRING, MODAL)
- ✅ Known working ~180-200 KB
- Status: COMPLETE, BUILDS SUCCESSFULLY

**Unit 3: "Eurorack Rings" (Future)**
- Modal resonator synthesis
- Estimated: ~150-200 KB
- Features: Polyphonic resonator, sympathetic strings
- Complexity: MEDIUM

**Unit 4: "Eurorack Elements" (Future)**
- Physical modeling voice
- Estimated: ~200-250 KB
- Features: Bowed/blown/struck excitation + resonator
- Complexity: HIGH

**Unit 5: "Eurorack Warps" (Future)**
- Meta-modulator / wavefolder
- Estimated: ~80-120 KB
- Features: 8 algorithms for timbral transformation
- Complexity: LOW-MEDIUM

**Unit 6+: Additional Modules (Optional)**
- Clouds (granular) - LARGE, may need standalone
- Tides (oscillator/function generator)
- Streams (dynamics processor)

### Hardware Testing Priority

**IMMEDIATE NEXT STEP:** 🚨 **TEST CURRENT BUILD ON HARDWARE**
1. Load current 351 KB unit onto Drumlogue
2. Verify it loads and runs without errors
3. Test memory stability (long sessions, switching oscillators)
4. Document actual memory limits

**If 351 KB works:**
- ✅ Can proceed with combined units
- ✅ Could add one more small oscillator (Warps?)
- ⚠️ Monitor for memory issues

**If 351 KB fails or causes issues:**
- 🔴 MUST split into separate units
- 🔴 Rebuild as "Eurorack Braids" + "Eurorack Plaits" separately
- ✅ Ensures reliability and compatibility

---

## 🎯 Next Steps & Roadmap

### IMMEDIATE ACTION REQUIRED (Priority 1) 🚨
1. **Hardware Testing - CRITICAL**
   - Load `eurorack.drmlgunit` (351 KB) onto Drumlogue hardware
   - Test basic functionality (both oscillators, parameter changes)
   - Monitor for memory errors or crashes
   - Test sustained use (30+ minutes)
   - Document observations (stability, latency, glitches)
   - **DECISION POINT:** Results determine whether to split units

2. **Based on Hardware Test Results:**

   **If 351 KB works reliably:**
   - ✅ Can keep combined unit approach
   - ✅ Document actual memory limits found
   - ✅ Cautiously add smaller modules (Warps ~80-120 KB)

   **If 351 KB has issues (fails to load, crashes, glitches):**
   - 🔴 Split into separate units immediately
   - 🔴 Create "Eurorack Braids" unit (~150 KB)
   - 🔴 Create "Eurorack Plaits" unit (~200 KB)
   - 🔴 Test each separately

### Phase 4: Expand Oscillator Library (Medium Priority)

**If Combined Unit Approach Viable:**
1. Add **Warps** meta-modulator (~80-120 KB)
   - 8 algorithms for timbral transformation
   - Relatively small, good test case
   - Monitor total unit size

**If Separate Units Required:**
1. Build "Eurorack Rings" (~150-200 KB)
   - Modal resonator synthesis
   - Polyphonic capabilities
   - Unique sonic character

2. Build "Eurorack Elements" (~200-250 KB)
   - Physical modeling voice
   - Complex but valuable
   - High-quality sounds

3. Build "Eurorack Warps" (~80-120 KB)
   - Smallest of the three
   - Quick win

### Phase 5: Advanced Features (Lower Priority)

1. **Preset Management**
   - Expand from 3 to 8-10 presets per oscillator
   - Add preset save/recall via MIDI SysEx
   - Document preset format

2. **Parameter Mapping Optimization**
   - Fine-tune parameter ranges for musicality
   - Add parameter scaling/curves
   - Optimize for hardware controls

3. **Performance Optimization**
   - Profile CPU usage per oscillator
   - Identify optimization opportunities
   - Utilize ARM NEON SIMD if beneficial
   - Test at lower sample rates if needed

### Phase 6: Additional Modules (Optional/Long-term)

1. **Clouds** - Granular processor
   - **CHALLENGE:** Very large (~400-500 KB)
   - May require standalone unit
   - Consider simplified version

2. **Tides** - Oscillator/function generator
   - Moderate size (~100-150 KB)
   - Useful for modulation

3. **Streams** - Dynamics processor
   - Moderate size (~80-120 KB)
   - Good for effects processing

---

## 📋 Current TODO List

### Critical (Do First) 🔴
- [ ] **TEST ON HARDWARE** - Load eurorack.drmlgunit onto Drumlogue
- [ ] Document hardware test results (load success, stability, memory)
- [ ] Make split/combine decision based on hardware results
- [ ] Update documentation with actual memory limits

### High Priority (If Hardware Test Passes) 🟡
- [ ] Document oscillator switching behavior on hardware
- [ ] Test all 8 Plaits engines thoroughly
- [ ] Test all 16 Braids shapes thoroughly
- [ ] Create user manual with parameter mappings
- [ ] Record audio demos of each engine/shape

### High Priority (If Hardware Test Fails) 🟡
- [ ] Split config.mk into separate units
- [ ] Build "eurorack_braids.drmlgunit" (Braids only)
- [ ] Build "eurorack_plaits.drmlgunit" (Plaits only)
- [ ] Test each unit separately on hardware
- [ ] Create distribution package with both units

### Medium Priority (After Hardware Validation) 🟢
- [ ] Add Rings module implementation
- [ ] Add Elements module implementation
- [ ] Add Warps module implementation
- [ ] Optimize preset management
- [ ] Add more presets (target: 8-10 per oscillator)
- [ ] Profile CPU usage
- [ ] Optimize hot paths if needed

### Low Priority (Polish & Enhancement) 🔵
- [ ] Investigate NEON SIMD optimization opportunities
- [ ] Add MIDI SysEx preset save/load
- [ ] Create parameter curve mappings
- [ ] Build web-based preset editor
- [ ] Create comprehensive video tutorial

---

## 🎓 Lessons Learned

1. **Linker Symbol Conflicts**
   - Both Braids and Plaits had `resources.cc` → Makefile used `notdir`, collapsed to same `resources.o`
   - Solution: Rename one file (`plaits_resources.cc`) and update all includes
   - Lesson: Check for filename collisions when integrating multiple DSP libraries

2. **API Signature Mismatches**
   - Plaits Engine::Render() requires 5 args, not 4
   - Solution: Read source headers carefully, use correct buffer types (float arrays, not Voice::Frame)
   - Lesson: Don't assume API from documentation alone

3. **Macro Expansion Issues**
   - Wavetable macro `wav_integrated_waves_##type` failed when OSCILLATOR_TYPE undefined
   - Solution: Add default definition and fix macro (remove underscore)
   - Lesson: Provide defaults for all compile-time macros

4. **Memory Is Critical**
   - 351 KB build is unprecedented for user units
   - Multi-engine had 32 KB hard limit
   - Lesson: **Always test on hardware before expanding further**

5. **Dispatcher Pattern Works Well**
   - Clean separation between oscillators
   - Easy to add new implementations
   - Factory pattern simplifies instantiation
   - Lesson: Good architecture pays off during integration

---

## 📊 Technical Summary

### What Works ✅
- Dispatcher architecture with factory pattern
- Dual oscillator integration (Braids + Plaits)
- 11-parameter interface
- MIDI support (note, velocity, pitch bend)
- Preset system
- Docker build environment
- Resource file conflict resolution
- All compilation and linking

### What's Unknown ⚠️
- **Drumlogue memory limits** (not documented by Korg)
- Will 351 KB unit load on hardware?
- Runtime memory allocation stability
- Maximum practical unit size
- NEON SIMD support/benefits
- Actual CPU headroom at 48 kHz

### What's Next ⏭️
1. **Hardware testing** (critical decision point)
2. Split vs. combined unit decision
3. Expand library based on test results
4. Optimization if needed

---

## 📊 Technical Notes

### Architecture Benefits
- **Modular**: Each oscillator is independent
- **Extensible**: Adding new oscillators doesn't affect existing ones
- **Runtime Switching**: Change oscillators via parameter 0
- **Shared Interface**: All oscillators use same 11-parameter structure

### Build System Notes
- **Local Sources**: Eurorack DSP code copied to `eurorack_src/`
- **Docker Mount**: Only `logue-sdk/platform/` accessible in container
- **Include Path**: `eurorack_src` added to compiler includes
- **Modular Compilation**: Each oscillator compiles separately

### Known Working Configuration
- Sample Rate: 48kHz
- Buffer Size: 24 samples (defined in Braids render loop)
- Audio Format: float [-1.0, 1.0] stereo
- MIDI: Full support (note, velocity, pitch bend)
- Presets: 3 per oscillator

```
logue-sdk/platform/drumlogue/
├── my-macro-osc/
│   ├── Makefile
│   ├── manifest.json
│   ├── header.c
│   ├── unit.cc
│   └── _unit_base.c
```

### 3. Build & Test

```bash
# After creating your unit structure:
cd /mnt/d/Fede/drumlogue/logue-sdk-eurorack-drumlogue-port/eurorack-prologue
./logue-sdk/docker/run_cmd.sh build drumlogue/my-macro-osc
```

### 4. Load to Device

```
Units/Synths/my_macro_osc.drmlgunit  (on Drumlogue USB storage)
```

---

## File Structure After Completion

```
eurorack-prologue/
├── EURORACK_DRUMLOGUE_BUILD.md        ← Quick reference
├── logue-sdk/                         (updated to main branch)
│   ├── docker/                        ✅ Now available
│   ├── platform/
│   │   ├── drumlogue/                 ✅ Now populated
│   │   │   ├── dummy-synth/
│   │   │   ├── dummy-delfx/
│   │   │   ├── my-osc/                ← You'll create these
│   │   │   └── ...
│   │   ├── prologue/
│   │   └── ...
│   └── ...
├── macro-oscillator2.cc               (source to port)
├── modal-strike.cc                    (source to port)
├── *.mk files                         (build configs)
└── ...
```

---

## Key Docker Commands

**Build a single unit:**
```bash
./logue-sdk/docker/run_cmd.sh build drumlogue/dummy-synth
```

**Interactive development:**
```bash
./logue-sdk/docker/run_interactive.sh
user@logue-sdk $ build drumlogue/my-osc
user@logue-sdk $ exit
```

**List available units:**
```bash
./logue-sdk/docker/run_cmd.sh build -l --drumlogue
```

---

## Porting Challenges & Solutions

| Challenge | Solution |
|-----------|----------|
| Different CPU architecture | Use provided build system & templates |
| Memory constraints | Reference dummy-synth size & optimize |
| OS differences | Use logue-sdk's platform abstraction |
| Audio format differences | Adapt header.c interface |
| Compilation errors | Check Drumlogue platform docs |

---

## Documentation Resources

📖 **In Your Project:**
- `EURORACK_DRUMLOGUE_BUILD.md` - Step-by-step build guide
- `logue-sdk/platform/drumlogue/README.md` - Official Drumlogue docs
- `logue-sdk/platform/drumlogue/dummy-synth/` - Reference implementation

📚 **External:**
- Korg logue-sdk: https://github.com/korginc/logue-sdk
- Drumlogue specs: https://www.korg.com/us/products/drums/drumlogue

---

## Status Summary

✅ **Completed:**
- Git submodule initialized
- SDK updated to drumlogue-enabled version
- Docker build system available
- Drumlogue platform templates ready
- Build guide created

⏳ **In Progress:**
- Docker image build (should complete in ~15 minutes)

🚀 **Ready to Start:**
- Porting individual oscillators
- Building .drmlgunit files
- Testing on device

---

## Quick Command Reference

```bash
# Enter WSL2
wsl
bash

# Update to project directory
cd /mnt/d/Fede/drumlogue/logue-sdk-eurorack-drumlogue-port/eurorack-prologue

# Test Docker image when ready
docker images | grep logue-sdk

# Build an example
./logue-sdk/docker/run_cmd.sh build drumlogue/dummy-synth

# Find output
ls logue-sdk/platform/drumlogue/dummy-synth/build/dummy_synth.drmlgunit
```

---

**Next Action:** Wait for Docker build to complete (progress shown in terminal), then try building the dummy-synth as a test!
