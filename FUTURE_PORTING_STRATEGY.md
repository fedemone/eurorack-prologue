# Future Porting Strategy

**Document Version**: 1.0
**Last Updated**: January 11, 2026
**Status**: Planning Phase

---

## Overview

This document outlines the strategy for porting additional Mutable Instruments Eurorack modules to Korg Drumlogue, based on lessons learned from the initial Braids/Plaits implementation.

---

## Project Structure

### Current Projects

| Project | Type | File | Size | Status |
|---------|------|------|------|--------|
| **eurorack** | Synth (Combined) | `eurorack.drmlgunit` | 351.7 KB | ❌ Too large - deprecated |
| **braids** | Synth | `braids.drmlgunit` | 109.1 KB | ✅ Built, ready for testing |
| **plaits** | Synth | `plaits.drmlgunit` | ~200 KB (est) | 🔄 To be built |

### Future Projects

#### Synth Units (Oscillators)

| Project | Type | File | Estimated Size | Priority |
|---------|------|------|----------------|----------|
| **eurorack2** | Synth | `eurorack2.drmlgunit` | <350 KB | 🔄 **Next synth project** |
| **eurorack3** | Synth | `eurorack3.drmlgunit` | <350 KB | ⏳ Future |
| **rings** | Synth | `rings.drmlgunit` | ~180 KB | ⏳ Future |
| **elements** | Synth | `elements.drmlgunit` | ~230 KB | ⏳ Future |

#### Effect Units

| Effect Type | Module | File | Estimated Size | Priority |
|-------------|--------|------|----------------|----------|
| **Delay** | Clouds (Granular delay) | `clouds.delfxunit` | ~150 KB | 🔄 High priority |
| **Reverb** | Rings (Resonator) | `rings.revfxunit` | ~180 KB | 🔄 High priority |
| **Reverb** | Clouds (Reverb mode) | `clouds_reverb.revfxunit` | ~150 KB | ⏳ Medium |
| **Modulation** | Warps (Wavefolder/etc) | `warps.modfxunit` | ~100 KB | ⏳ Medium |
| **Master FX** | Shelves (EQ) | `shelves.masterfxunit` | ~50 KB | ⏳ Low |

---

## Build Type Guidelines

### Synth Units (.drmlgunit)

**Use for**: Oscillators, sound generators, synthesizers

**Target**: User oscillators that generate audio from MIDI input

**Examples**:
- Braids (macro oscillator)
- Plaits (multi-engine synth)
- Rings (modal synthesizer)
- Elements (physical modeling)

**File Extension**: `.drmlgunit`

**Size Limit**: **<350 KB** (hardware constraint discovered empirically)

**Safe Target**: **<250 KB** (recommended for safety margin)

---

### Delay Effects (.delfxunit)

**Use for**: Delay-based effects, echo, granular processors

**Target**: Delay effect slot in Drumlogue

**Examples**:
- Clouds (granular delay/texture synthesizer)
- Custom delay implementations

**File Extension**: `.delfxunit`

**Size Limit**: Unknown (to be determined - likely similar to synth units)

**Recommended First Port**: **Clouds** (granular delay mode)

---

### Reverb Effects (.revfxunit)

**Use for**: Reverb, spatial effects, resonators

**Target**: Reverb effect slot in Drumlogue

**Examples**:
- Rings (resonator/reverb mode)
- Clouds (reverb mode)
- Custom reverb algorithms

**File Extension**: `.revfxunit`

**Size Limit**: Unknown (to be determined)

**Recommended First Port**: **Rings** (resonator mode)

---

### Modulation Effects (.modfxunit)

**Use for**: Chorus, flanger, phaser, waveshaping, distortion

**Target**: Modulation effect slot in Drumlogue

**Examples**:
- Warps (wavefolder, waveform animator)
- Veils (VCA/distortion)
- Custom modulation effects

**File Extension**: `.modfxunit`

**Size Limit**: Unknown (to be determined)

---

### Master Effects (.masterfxunit)

**Use for**: Final stage processing, EQ, limiting, multi-band effects

**Target**: Master effect slot in Drumlogue

**Examples**:
- Shelves (3-band EQ)
- Custom master effects

**File Extension**: `.masterfxunit`

**Size Limit**: Unknown (to be determined)

---

## Project Organization

### Directory Structure

```
logue-sdk/platform/drumlogue/
├── braids/              # ✅ Built (109.1 KB)
├── plaits/              # 🔄 To be built (~200 KB)
├── eurorack/            # ❌ Deprecated (too large)
├── eurorack2/           # 🔄 Next synth project
│   ├── config.mk
│   ├── header.c
│   ├── manifest.json
│   ├── unit.cc
│   ├── synth.h/cc
│   └── eurorack_src/
├── clouds_delay/        # 🔄 Future delay effect
│   ├── config.mk
│   ├── header.c
│   ├── manifest.json
│   └── ...
└── rings_reverb/        # 🔄 Future reverb effect
    ├── config.mk
    ├── header.c
    ├── manifest.json
    └── ...
```

---

## Eurorack2 Project Specification

### Purpose

Second generation of eurorack synth ports, incorporating different modules from the original combined project.

### Candidate Modules for Eurorack2

#### Option A: Rings + Warps (Complementary)
- **Rings**: Modal synthesis, physical modeling (~180 KB)
- **Warps**: Waveshaping, oscillator animator (~100 KB)
- **Combined Estimate**: ~280 KB ✅ Should fit

#### Option B: Elements (Standalone)
- **Elements**: Physical modeling synthesizer (~230 KB)
- **Estimate**: ~230 KB ✅ Should fit
- **Note**: Complex, feature-rich, may need more testing

#### Option C: Additional Plaits Engines (Expansion)
- Port engines that weren't in Plaits unit
- Lower risk, known architecture
- **Estimate**: ~200 KB ✅ Should fit

### Recommended: Option A (Rings + Warps)

**Rationale**:
1. Complementary sonic characteristics
2. Combined size well under 350 KB limit
3. Rings provides physical modeling
4. Warps provides waveshaping/modulation
5. Both are popular, well-tested modules

---

## Effect Porting Priority

### Phase 1: Delay Effects

**Priority Module**: **Clouds (Granular Delay Mode)**

**Reasons**:
1. Highly popular and versatile
2. Granular delay is unique in Drumlogue
3. Well-documented codebase
4. Reasonable size (~150 KB estimated)

**Implementation**:
- Directory: `logue-sdk/platform/drumlogue/clouds_delay/`
- File: `clouds_delay.delfxunit`
- Mode: Focus on granular delay/texture modes
- Simplified controls for Drumlogue interface

---

### Phase 2: Reverb Effects

**Priority Module**: **Rings (Resonator/Reverb Mode)**

**Reasons**:
1. Natural reverb from modal synthesis
2. Unique character compared to standard reverbs
3. Good synergy with Drumlogue drums
4. Proven DSP quality

**Implementation**:
- Directory: `logue-sdk/platform/drumlogue/rings_reverb/`
- File: `rings_reverb.revfxunit`
- Mode: Resonator as reverb/shimmer effect
- Stereo output optimization

---

### Phase 3: Modulation Effects

**Priority Module**: **Warps (Wavefolder/Animator)**

**Reasons**:
1. Versatile modulation/distortion
2. Unique timbral possibilities
3. Small footprint (~100 KB)
4. Multiple algorithms in one unit

**Alternative**: Could be included in eurorack2 as synth instead

---

## Size Management Strategy

### Lessons Learned

1. **Size Limit**: Drumlogue has <350 KB hardware limit
2. **Safe Target**: Aim for <250 KB for margin
3. **Split Approach**: Better to have multiple smaller units than one large combined unit
4. **User Flexibility**: Separate units allow users to choose what they load

### Size Estimation Formula

```
Estimated Size = DSP Code + UI Code + Resources + Overhead

Typical Breakdown:
- DSP algorithms: 80-180 KB
- UI/parameter handling: 10-20 KB
- Lookup tables/resources: 5-50 KB
- Framework overhead: 15-25 KB
```

### Pre-Build Checks

Before building any new unit:

1. ✅ Estimate code size from source files
2. ✅ Check for large lookup tables
3. ✅ Consider optimization flags
4. ✅ Plan for <250 KB target
5. ✅ Document expected size range

---

## Development Workflow

### For New Synth Units

```bash
# 1. Create project directory
cd logue-sdk/platform/drumlogue
cp -r braids/ eurorack2/

# 2. Update configuration
cd eurorack2/
# Edit config.mk - update PROJECT name and sources
# Edit header.c - update unit name and parameters
# Edit manifest.json - update metadata

# 3. Implement synth wrapper
# Edit synth.h/cc for new module integration

# 4. Build
cd ../../../..
bash drumlogue_build.sh build drumlogue/eurorack2

# 5. Verify size
ls -lh logue-sdk/platform/drumlogue/eurorack2/*.drmlgunit
```

### For New Effect Units

```bash
# 1. Create project directory
cd logue-sdk/platform/drumlogue
mkdir clouds_delay/

# 2. Copy effect template (if available) or adapt from synth template
# Structure will be similar but with effect-specific APIs

# 3. Update configuration for effect type
# config.mk - set correct build type for delfx
# header.c - define effect parameters
# manifest.json - mark as delay effect

# 4. Implement effect processing
# Create effect.h/cc with process() method

# 5. Build with effect target
cd ../../../..
bash drumlogue_build.sh build drumlogue/clouds_delay

# 6. Verify size and type
ls -lh logue-sdk/platform/drumlogue/clouds_delay/*.delfxunit
```

---

## Testing Strategy

### Hardware Testing Checklist

For each new unit (synth or effect):

#### Functional Tests
- ✅ Unit loads without errors
- ✅ Parameters respond correctly
- ✅ Audio output is clean (no clicks, pops, or artifacts)
- ✅ MIDI input works (for synths)
- ✅ Effect processing works (for effects)
- ✅ CPU usage is acceptable
- ✅ No memory leaks or crashes

#### Performance Tests
- ✅ Polyphony handling (for synths)
- ✅ Latency is acceptable
- ✅ Works in combination with other effects
- ✅ Stable under continuous use

#### Integration Tests
- ✅ Works with Drumlogue's sequencer
- ✅ Parameter automation works
- ✅ Preset saving/loading works
- ✅ Interacts correctly with other units

---

## Documentation Requirements

### For Each New Unit

Create the following documentation:

1. **BUILD_SUCCESS.md** - Build metrics and verification
2. **FEATURES.md** - Feature list and parameter descriptions
3. **HARDWARE_TEST.md** - Test results and known issues
4. **USER_GUIDE.md** - Usage instructions for end users

### Update Central Documentation

When adding new units:

1. Update [STATUS_SUMMARY.md](STATUS_SUMMARY.md)
2. Update [README_DRUMLOGUE_STATUS.md](README_DRUMLOGUE_STATUS.md)
3. Update this document (FUTURE_PORTING_STRATEGY.md)
4. Update [BUILD_STATUS.md](BUILD_STATUS.md) if needed

---

## Risk Assessment

### Known Risks

| Risk | Severity | Mitigation |
|------|----------|------------|
| Size exceeds 350 KB | High | Pre-estimate size, split if needed |
| CPU usage too high | Medium | Profile early, optimize algorithms |
| Audio artifacts | Medium | Thorough testing, buffer size validation |
| API compatibility | Low | Use logue-sdk templates and examples |
| Hardware instability | Low | Extensive testing before release |

### Validation Gates

Before declaring a unit "complete":

1. ✅ Build succeeds with size <250 KB
2. ✅ Hardware loads without errors
3. ✅ All features work as expected
4. ✅ No audio artifacts detected
5. ✅ CPU usage acceptable (<80% on Drumlogue)
6. ✅ Documentation complete
7. ✅ User testing feedback positive

---

## Next Steps (Immediate)

### Priority 1: Complete Current Work

1. ✅ Test braids.drmlgunit on hardware
2. 🔄 Build plaits.drmlgunit
3. 🔄 Test plaits.drmlgunit on hardware
4. 🔄 Document results

### Priority 2: Begin Eurorack2

1. 🔄 Design eurorack2 module selection (recommend Rings + Warps)
2. 🔄 Create eurorack2 project structure
3. 🔄 Estimate size and validate feasibility
4. 🔄 Implement and build

### Priority 3: Explore Effects

1. 🔄 Research Drumlogue effect SDK documentation
2. 🔄 Create first effect unit (clouds_delay)
3. 🔄 Test effect on hardware
4. 🔄 Document effect porting process

---

## Module Selection Guide

### Best Candidates for Synth Ports

| Module | Complexity | Size Est | Sonic Utility | Priority |
|--------|------------|----------|---------------|----------|
| **Rings** | Medium | ~180 KB | Very High | ⭐⭐⭐⭐⭐ |
| **Warps** | Low | ~100 KB | High | ⭐⭐⭐⭐ |
| **Elements** | High | ~230 KB | Very High | ⭐⭐⭐⭐ |
| Tides | Low | ~80 KB | Medium | ⭐⭐⭐ |
| Streams | Medium | ~120 KB | Medium | ⭐⭐ |

### Best Candidates for Effect Ports

| Module | Type | Size Est | Sonic Utility | Priority |
|--------|------|----------|---------------|----------|
| **Clouds** | Delay | ~150 KB | Very High | ⭐⭐⭐⭐⭐ |
| **Rings** | Reverb | ~180 KB | Very High | ⭐⭐⭐⭐⭐ |
| **Warps** | Mod | ~100 KB | High | ⭐⭐⭐⭐ |
| Shelves | Master | ~50 KB | Medium | ⭐⭐⭐ |
| Veils | Mod | ~60 KB | Medium | ⭐⭐ |

---

## Conclusion

The split-unit strategy has proven successful with the Braids port (109 KB). Future development should:

1. **Keep units focused** - One or two complementary modules per unit
2. **Target <250 KB** - Safe margin under 350 KB hardware limit
3. **Prioritize effects** - Expand Drumlogue's effect capabilities
4. **Build incrementally** - Test each unit thoroughly before moving to next
5. **Document thoroughly** - Maintain clear records for future reference

**Next recommended project**: **Eurorack2** (Rings + Warps) or **clouds_delay.delfxunit**

---

**Prepared by**: GitHub Copilot
**Review Status**: Pending user validation
**Document Status**: Living document - update as strategy evolves
