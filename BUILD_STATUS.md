# ✅ Eurorack-Drumlogue: Dual-Oscillator Build Complete!

**Status:** 2 oscillators working (Braids + Plaits) | Memory analysis phase | Hardware testing required
**Last Updated:** January 11, 2026
**Build Size:** 351.72 KB

---

## Summary of Completed Work

### Phase 1: SDK Integration ✅
- **Issue Found**: SDK submodule pinned to old version (b7a424e) without Drumlogue support
- **Solution Applied**: Updated to `main` branch with full Drumlogue platform
- **Result**: `logue-sdk/platform/drumlogue/` now populated with unit templates

### Phase 2: Docker Build Environment ✅
- **Built**: `logue-sdk-dev-env` Docker image (3.77GB)
- **Contains**: ARM Cortex-A7 cross-compiler, build tools, Linux environment
- **Tested**: Successfully built `dummy_synth.drmlgunit`
- **Output**: Valid 5.0KB unit file ready for device

### Phase 3: Multi-Oscillator Architecture ✅
- **Designed**: Dispatcher pattern with `OscillatorBase` abstract interface
- **Implemented**: Main dispatcher in `synth.h/.cc`
- **Created**: Braids oscillator implementation (`synth_braids.h/.cc`)
- **Created**: Plaits oscillator implementation (`synth_plaits.h/.cc`)
- **Configured**: 11-parameter interface with oscillator selection (param 0)
- **Result**: Successfully builds `eurorack.drmlgunit` with Braids + Plaits

### Phase 4: Plaits Integration Complete ✅
- **Fixed**: Linker conflict (renamed `plaits/resources.cc` → `plaits/plaits_resources.cc`)
- **Updated**: 18+ Plaits source files to use renamed header
- **Fixed**: Engine::Render() API signature (5 args with aux buffer)
- **Fixed**: Wavetable macro for default table selection
- **Result**: All 8 Plaits engines compiling and linking successfully

### Phase 5: Documentation Created ✅
Created comprehensive guides for development and expansion:

| Document | Purpose |
|----------|---------|
| [QUICK_START.md](QUICK_START.md) | 5-min overview, key commands |
| [WINDOWS_WSL2_SETUP.md](WINDOWS_WSL2_SETUP.md) | Full environment setup |
| [EURORACK_DRUMLOGUE_PORTING.md](EURORACK_DRUMLOGUE_PORTING.md) | Code adaptation guide |
| [README_DRUMLOGUE_STATUS.md](README_DRUMLOGUE_STATUS.md) | Comprehensive status & next steps |

---

## What's Ready Now

### ✅ Dual-Oscillator Eurorack Unit

**Build the eurorack synth:**
```bash
cd /path/to/eurorack-prologue
bash drumlogue_build.sh build drumlogue/eurorack
```

**Output:** `logue-sdk/platform/drumlogue/eurorack/eurorack.drmlgunit` (351.72 KB)

**Features:**
- **2 oscillators**: Braids (16 shapes) + Plaits (8 engines = 13+ synthesis types)
- 11 parameters (oscillator selector + 10 oscillator controls)
- Runtime oscillator switching via parameter 0
- 3 presets per oscillator
- Full MIDI support (Note On/Off, Velocity, Pitch Bend)
- 48kHz stereo output

### ✅ Currently Available Oscillators

#### 1. **Braids** (16 shapes) - Macro Oscillator
   - CSAW, MORPH_TABLE, SAW_SWARM, FM, FB_FM
   - DUTY_CYCLE, FILTERED_SAW, ADDITIVE
   - WAVETABLES, WVTBL_MORPH, CHORD
   - VOWEL, VOWEL_FOF, HARMONICS
   - FM_FEEDBACK, STRING

#### 2. **Plaits** (8 engines covering 13+ synthesis types) - Macro Oscillator 2
   - **VA**: Virtual Analog (pair of classic waveforms)
   - **WSH**: Waveshaping oscillator
   - **FM**: Two-operator FM synthesis
   - **GRN**: Granular formant oscillator
   - **ADD**: Additive/harmonic oscillator
   - **WTBL**: Wavetable synthesis with morphing
   - **STRING**: Physical modeling string (Karplus-Strong)
   - **MODAL**: Modal synthesis (resonant physical models)

### ✅ Build System Configuration

- **Local DSP Sources**: Eurorack code in `eurorack_src/` (braids, plaits, stmlib)
- **Modular Design**: Each oscillator is a separate class
- **Dispatcher Pattern**: Clean oscillator switching at runtime
- **Docker Integration**: Builds via official logue-sdk Docker environment
- **Conflict Resolution**: Plaits resources renamed to avoid Braids collision

---

## 💾 Memory Status & Platform Analysis

### Current Build
- **Size**: 351.72 KB (360,160 bytes)
- **Code (text)**: ~347 KB
- **Initialized data**: ~2 KB
- **BSS**: ~20 bytes
- **Comparison**: 100x larger than dummy-synth (3.5 KB)

### Platform: Korg Drumlogue
- **CPU**: ARM Cortex-A7 (ARMv7-A, ~800 MHz est.)
- **SIMD**: NEON available (not yet utilized)
- **OS**: Linux-based
- **Memory limits**: ⚠️ **NOT DOCUMENTED** by Korg

### Critical Unknown
🚨 **Hardware testing required** - 351 KB is unprecedented for user units
- Prologue multi-engine limit: 32 KB (hard constraint)
- Drumlogue appears more permissive, but **actual limits unknown**
- **MUST test on hardware before expanding further**

---

## Next Steps: Hardware Testing & Expansion Decision

### CRITICAL PRIORITY 🚨

#### 1. Hardware Testing (IMMEDIATE)
- [ ] Load `eurorack.drmlgunit` onto Drumlogue
- [ ] Test basic functionality (load, both oscillators, parameters)
- [ ] Monitor stability (30+ minute session)
- [ ] Document memory behavior, glitches, crashes
- [ ] **DECISION POINT**: Continue combined or split into separate units

#### 2. Based on Test Results

**If 351 KB works reliably:**
- ✅ Continue with combined unit approach
- ✅ Document actual memory limits
- ✅ Add smaller modules (Warps ~80-120 KB)
- ⚠️ Monitor total size carefully

**If 351 KB has issues:**
- 🔴 **Split into separate units** (REQUIRED)
- 🔴 Build "eurorack_braids.drmlgunit" (~150 KB)
- 🔴 Build "eurorack_plaits.drmlgunit" (~200 KB)
- 🔴 Test each independently
- ✅ Safer, proven approach (follows Prologue precedent)

---

## Potential Future Expansions (Post-Hardware Test)

### Additional Mutable Instruments Modules

#### High-Quality Physical Modeling
| Module | Description | Est. Size | Priority | Notes |
|--------|-------------|-----------|----------|-------|
| **Rings** | Modal resonator | ~150-200 KB | HIGH | Polyphonic, sympathetic strings |
| **Elements** | Physical modeling voice | ~200-250 KB | HIGH | Bowed/blown/struck excitation |
| **Warps** | Meta-modulator | ~80-120 KB | MEDIUM | 8 algorithms, smaller size |
| **Clouds** | Granular texture | ~400-500 KB | LOW | LARGE - may need standalone unit |

#### Implementation Strategy

**Option A: Continue Combined Unit** (if 351 KB works)
- Add Warps next (~80-120 KB) → Total: ~430-470 KB
- Monitor for issues
- Stop before Rings/Elements (~600-850 KB total)

**Option B: Separate Focused Units** (if split required)
1. `eurorack_braids.drmlgunit` (~150 KB)
2. `eurorack_plaits.drmlgunit` (~200 KB)
3. `eurorack_rings.drmlgunit` (~180 KB)
4. `eurorack_elements.drmlgunit` (~230 KB)
5. `eurorack_warps.drmlgunit` (~100 KB)

**Recommended:** Option B (safer, proven, more flexible)

---

## Quick Implementation Guide

### Add a New Oscillator to Combined Unit

#### 1. Create Oscillator Files

```bash
cd logue-sdk/platform/drumlogue/eurorack

# Create from template
cp synth_plaits.h synth_rings.h
cp synth_plaits.cc synth_rings.cc

# Edit to rename class: SynthPlaits -> SynthRings
```

#### 2. Copy DSP Sources

```bash
# From eurorack/rings module
cp -r ../../../../eurorack/rings/dsp \
      eurorack_src/rings/
```

#### 3. Register in Dispatcher

Edit `synth.h`:
```cpp
enum OscillatorType {
  OSC_BRAIDS = 0,
  OSC_PLAITS = 1,
  OSC_RINGS = 2,     // Add new oscillator
  OSC_COUNT
};
```

Edit `synth.cc` `CreateOscillator()`:
```cpp
case OSC_RINGS:
  active_oscillator_ = std::make_unique<SynthRings>();
  break;
```

#### 4. Update Build Configuration

Edit `config.mk`:
```makefile
RINGSDIR = $(EURORACKDIR)/rings

CXXSRC = unit.cc \
         synth.cc \
         synth_braids.cc \
         synth_plaits.cc \
         synth_rings.cc \                  # Add new oscillator
         $(BRAIDSDIR)/...                  # Existing
         $(PLAITSDIR)/...                  # Existing
         $(RINGSDIR)/dsp/part.cc \         # Add Rings sources
         $(RINGSDIR)/dsp/string.cc \
         $(RINGSDIR)/dsp/resonator.cc \
         $(STMLIBDIR)/utils/random.cc
```

#### 5. Implement Oscillator Class

Key methods to implement:
```cpp
class SynthRings : public OscillatorBase {
public:
  void Init(float sample_rate) override;
  void NoteOn(uint8_t note, uint8_t velocity) override;
  void SetParameter(uint8_t index, int16_t value) override;
  void Render(float* out, size_t frames) override;
private:
  rings::Part part_;  // Main DSP engine
  // ... buffers and state
};
```

#### 6. Build and Test

```bash
bash drumlogue_build.sh build drumlogue/eurorack
```

### Create Separate Unit (Recommended Approach)

#### 1. Create New Unit Directory

```bash
cd logue-sdk/platform/drumlogue
cp -r eurorack rings
cd rings
```

#### 2. Update Files

**header.c:**
```c
.name = "Rings",
.num_preload_programs = 1,
// ... update strings for Rings-specific params
```

**config.mk:**
```makefile
PROJECT := rings

CXXSRC = unit.cc \
         synth.cc \
         synth_rings.cc \
         eurorack_src/rings/dsp/part.cc \
         eurorack_src/rings/dsp/string.cc \
         eurorack_src/rings/dsp/resonator.cc \
         eurorack_src/stmlib/utils/random.cc
```

**synth.h/cc:**
- Remove dispatcher (single oscillator)
- Direct instantiation of SynthRings

#### 3. Build Separate Unit

```bash
bash drumlogue_build.sh build drumlogue/rings
# Output: logue-sdk/platform/drumlogue/rings/rings.drmlgunit
```

---

## Implementation Details

### Oscillator Class Template

See [EURORACK_DRUMLOGUE_PORTING.md](EURORACK_DRUMLOGUE_PORTING.md) for detailed code templates.

### Key Implementation Points
- `Init()` - Initialize DSP engine at sample rate
- `NoteOn/Off()` - Handle MIDI notes
- `NOTE_ON/OFF()` - Handle MIDI notes

See [EURORACK_DRUMLOGUE_PORTING.md](EURORACK_DRUMLOGUE_PORTING.md) for detailed code templates.

### Step 4: Build & Test

```bash
cd /path/to/eurorack-prologue
./logue-sdk/docker/run_cmd.sh build drumlogue/macro-oscillator2-synth
```

### Step 5: Deploy to Device

1. Connect Drumlogue via USB
2. Enter USB Storage mode (Power + MODE)
3. Copy `.drmlgunit` to `Units/Synths/`
4. Eject and restart

---

## Project Structure

```
eurorack-prologue/
├── logue-sdk/                           (Official Korg SDK on main branch)
│   ├── platform/drumlogue/
│   │   ├── common/                      (Platform headers)
│   │   ├── dummy-synth/                 (Synth template - working!)
│   │   ├── dummy-delfx/                 (Delay FX template)
│   │   ├── dummy-revfx/                 (Reverb FX template)
│   │   ├── dummy-masterfx/              (Master FX template)
│   │   └── [your-unit-name]/            (Create here)
│   └── docker/
│       ├── build_image.sh               (Already run)
│       ├── run_cmd.sh                   (Build command)
│       └── run_interactive.sh           (Interactive shell)
│
├── eurorack/                            (Eurorack libraries)
│   ├── braids/
│   ├── elements/
│   └── ... (various eurorack modules)
│
├── macro-oscillator2.cc                 (Oscillator source)
├── modal-strike.cc
├── osc_*.mk                             (Build rules)
│
└── Documentation/
    ├── QUICK_START.md                   (Start here!)
    ├── WINDOWS_WSL2_SETUP.md            (Full setup guide)
    ├── EURORACK_DRUMLOGUE_PORTING.md    (Code guide)
    └── README.md                        (Project overview)
```

---

## Known Configuration

Your system setup (from WINDOWS_WSL2_SETUP.md):

- **Host**: Windows 10/11 with WSL2
- **WSL Distribution**: Ubuntu 20.04
- **Docker**: Desktop for Windows
- **Project Path**: `/mnt/d/Fede/drumlogue/logue-sdk-eurorack-drumlogue-port/eurorack-prologue`
- **SDK**: logue-sdk main branch (commit a891472)
- **Docker Image**: logue-sdk-dev-env:latest (3.77GB)

---

## Troubleshooting Reference

### Build failed with "drumlogue not found"
Update SDK to main branch:
```bash
cd logue-sdk && git reset --hard origin/main && cd ..
```

### Docker image missing
Rebuild it:
```bash
cd logue-sdk/docker && ./build_image.sh
```
(May fail first time—retry if needed)

### No .drmlgunit in output
Check manifest.json is valid:
```bash
python3 -m json.tool logue-sdk/platform/drumlogue/[unit]/manifest.json
```

### WSL2 bash command not found
Use full WSL invocation:
```bash
wsl bash -c "cd /mnt/d/... && command"
```

---

## Key Files Created/Modified

### New Files
- ✅ `QUICK_START.md` - 5-min overview
- ✅ `EURORACK_DRUMLOGUE_PORTING.md` - Detailed porting guide
- ✅ `WINDOWS_WSL2_SETUP.md` - Complete setup instructions

### Modified Files
- ✅ `README.md` - Updated with new guide links and status

### Configuration Changed
- ✅ `logue-sdk` submodule updated to `main` branch
- ✅ Docker image built (`logue-sdk-dev-env:latest`)

---

## Ready to Start?

1. **Read first**: [QUICK_START.md](QUICK_START.md) (5 minutes)
2. **Understand setup**: [WINDOWS_WSL2_SETUP.md](WINDOWS_WSL2_SETUP.md) (if needed)
3. **Code guide**: [EURORACK_DRUMLOGUE_PORTING.md](EURORACK_DRUMLOGUE_PORTING.md) (when porting)

**Then pick an oscillator and start porting!**

---

## Success Metrics

- ✅ SDK has Drumlogue platform files
- ✅ Docker image builds successfully
- ✅ Test unit compiles to valid .drmlgunit
- ✅ Documentation complete
- 🎯 Ready for oscillator porting

**Your environment is ready. Pick your first oscillator and start porting!** 🎛️
