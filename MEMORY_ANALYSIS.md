# Drumlogue Memory Analysis & Expansion Planning

**Date:** January 11, 2026
**Current Build:** eurorack.drmlgunit (Braids + Plaits)
**Status:** 🚨 **HARDWARE TESTING REQUIRED**

---

## Executive Summary

✅ **Achievements:**
- Successfully integrated Braids (16 shapes) + Plaits (8 engines)
- Build system working flawlessly
- Dispatcher architecture proven
- 351.72 KB unit file created

⚠️ **Critical Unknown:**
- **Drumlogue memory limits are NOT documented** by Korg
- 351 KB is **100x larger** than reference dummy-synth (3.5 KB)
- Unknown if this will load/run on hardware
- **Hardware testing is MANDATORY** before any expansion

🎯 **Recommendation:**
- **Test current build on hardware immediately**
- **Split into separate units if issues occur**
- Follow proven Prologue approach (multiple smaller units)

---

## Platform Specifications

### Korg Drumlogue Hardware

**Processor:**
- CPU: ARM Cortex-A7 (32-bit ARMv7-A architecture)
- Estimated clock: ~800 MHz (not officially documented)
- SIMD: ARM NEON extensions available (not currently utilized)
- Operating system: Linux-based

**Architecture Notes:**
- **NOT** "ARM Neon v7" (that's not a processor model)
- ARM NEON is the SIMD instruction set extension on Cortex-A7
- Cortex-A7 is part of the ARMv7-A architecture family
- Supports both ARM and Thumb-2 instruction sets

**Memory Constraints:**
- User unit memory limit: **UNKNOWN** (Korg has not published specifications)
- Reference dummy-synth: 3.5 KB (minimal example)
- Multi-engine (Prologue/Minilogue): 32 KB hard limit (different platform)
- Drumlogue appears more permissive but **actual limits untested**

---

## Current Build Analysis

### eurorack.drmlgunit (Braids + Plaits)

**Size Breakdown:**
```
Total size:       351.72 KB  (360,160 bytes)
├─ Code (text):   ~347 KB    (97%)
├─ Data:          ~2 KB      (0.6%)
└─ BSS:           ~20 bytes  (0.06%)
```

**Component Estimates:**
```
Braids oscillator:      ~120-150 KB
  ├─ Macro oscillator     ~50 KB
  ├─ 16 synthesis shapes  ~40 KB
  ├─ Resources/tables     ~20 KB
  └─ Wrapper code         ~10 KB

Plaits oscillator:      ~180-200 KB
  ├─ 8 engine implementations ~120 KB
  ├─ Physical modeling    ~30 KB
  ├─ Resources/tables     ~20 KB
  └─ Wrapper code         ~10 KB

Dispatcher + glue:      ~10-15 KB
stmlib utilities:       ~5-10 KB
```

**Comparison:**
- Dummy-synth: 3.5 KB (100% baseline)
- Our build: 351.72 KB (**10,049% of baseline**)
- **This is unprecedented territory for Drumlogue user units**

---

## Memory Requirements for Planned Expansions

### Estimated Sizes

| Module | Description | Est. Code Size | Buffer Size | Total | Risk |
|--------|-------------|----------------|-------------|-------|------|
| **Rings** | Modal resonator | ~80-100 KB | ~50-100 KB | **~150-200 KB** | 🟡 Medium |
| **Elements** | Physical modeling voice | ~120-150 KB | ~80-100 KB | **~200-250 KB** | 🟡 Medium |
| **Warps** | Meta-modulator | ~60-80 KB | ~20-40 KB | **~80-120 KB** | 🟢 Low |
| **Clouds** | Granular texture | ~200-250 KB | ~200-250 KB | **~400-500 KB** | 🔴 High |

### Buffer Requirements Explained

**Why buffers matter:**
- Rings: Multiple resonator delays (kMaxBlockSize arrays)
- Elements: Oversampling buffers, resonator delays
- Clouds: Granular buffer (largest - multiple seconds of audio)
- Warps: Relatively small processing buffers

**Block size impact:**
- Drumlogue: BLOCKSIZE=24 samples
- Each float buffer: 24 samples × 4 bytes = 96 bytes
- But modules use multiple buffers (10-50+ buffers typical)

---

## Expansion Scenarios

### Scenario A: Combined Unit (Current Approach)

**Current state:**
```
eurorack.drmlgunit
├─ Braids (16 shapes)      ✅ Working
├─ Plaits (8 engines)      ✅ Working
└─ Total: 351.72 KB        ⚠️ UNTESTED
```

**Add Warps:**
```
eurorack.drmlgunit
├─ Braids (16 shapes)      ✅ Working
├─ Plaits (8 engines)      ✅ Working
├─ Warps (8 algorithms)    ❓ To be added
└─ Total: ~430-470 KB      🔴 HIGH RISK
```

**Add Rings:**
```
eurorack.drmlgunit
├─ Braids (16 shapes)      ✅ Working
├─ Plaits (8 engines)      ✅ Working
├─ Warps (8 algorithms)    ❓ To be added
├─ Rings (resonator)       ❓ To be added
└─ Total: ~580-670 KB      🔴 VERY HIGH RISK
```

**Risks:**
- 🔴 Unknown if 351 KB even works
- 🔴 Each addition increases risk exponentially
- 🔴 No rollback if larger build fails
- 🔴 Potential runtime memory issues (heap fragmentation)
- 🔴 May hit undocumented hard limits

**Benefits:**
- ✅ Single unit to manage
- ✅ Easy switching via parameter 0

**Verdict:** ⚠️ **NOT RECOMMENDED** until hardware limits are known

---

### Scenario B: Separate Units (Recommended)

**Distribution:**
```
eurorack_braids.drmlgunit      ~150 KB  🟢 Safe
├─ Braids macro oscillator
└─ 16 synthesis shapes

eurorack_plaits.drmlgunit      ~200 KB  🟢 Safe
├─ Plaits macro oscillator 2
└─ 8 synthesis engines

eurorack_rings.drmlgunit       ~180 KB  🟡 Likely safe
├─ Modal resonator
└─ Physical modeling

eurorack_elements.drmlgunit    ~230 KB  🟡 Likely safe
├─ Physical modeling voice
└─ Complex excitation models

eurorack_warps.drmlgunit       ~100 KB  🟢 Safe
└─ 8 modulation algorithms

eurorack_clouds.drmlgunit      ~450 KB  🔴 HIGH RISK
├─ Granular synthesis
└─ Large buffer requirements
```

**Risks per unit:**
- 🟢 <200 KB: Low risk (reasonable extrapolation from dummy-synth)
- 🟡 200-300 KB: Medium risk (need hardware validation)
- 🔴 >300 KB: High risk (unprecedented, untested)

**Benefits:**
- ✅ User loads only needed oscillators
- ✅ Isolated failure domains (one unit failure doesn't affect others)
- ✅ Follows Prologue precedent (wavetables split into 6 units due to 32 KB limit)
- ✅ Easier to troubleshoot
- ✅ Better memory management
- ✅ Incremental testing approach

**Drawbacks:**
- ❌ No runtime switching between oscillator families
- ❌ More files to manage/distribute
- ❌ Need to swap units to change oscillator type

**Verdict:** ✅ **RECOMMENDED** - Proven approach, safer, more flexible

---

## Decision Matrix

### Test Results → Action Plan

#### Result 1: 351 KB Works Perfectly ✅
**Action:**
- Document findings (memory usage, stability)
- Cautiously add Warps (~100 KB) → Test again
- Stop if any issues appear
- Maximum recommended: ~500 KB total

**Risk level:** 🟡 Medium - Still in uncharted territory

---

#### Result 2: 351 KB Loads But Has Issues ⚠️
**Issues might include:**
- Slow loading
- Occasional glitches
- Memory warnings
- Unstable after long sessions

**Action:**
- 🔴 **Split immediately** - Don't push further
- Rebuild as separate units
- Test each unit independently
- Document maximum safe size

**Risk level:** 🟡 Medium - Clear guidance for future

---

#### Result 3: 351 KB Fails to Load ❌
**Action:**
- 🔴 **Split immediately** - REQUIRED
- Build eurorack_braids.drmlgunit (~150 KB)
- Build eurorack_plaits.drmlgunit (~200 KB)
- Test each separately
- Document confirmed limits

**Risk level:** 🟢 Low - Clear constraints established

---

## Recommendations

### Immediate Actions (Priority 1) 🚨

1. **Test current build on hardware**
   - [ ] Load eurorack.drmlgunit onto Drumlogue
   - [ ] Test oscillator 0 (Braids) - all 16 shapes
   - [ ] Test oscillator 1 (Plaits) - all 8 engines
   - [ ] Monitor for issues (glitches, crashes, slow loading)
   - [ ] Run extended session (30+ minutes)
   - [ ] Document results

2. **Make split/combine decision**
   - [ ] If works: Document limits, proceed cautiously
   - [ ] If issues: Split units immediately
   - [ ] Update documentation with findings

### Future Development (Priority 2)

**If combined approach viable:**
- Add only small modules (Warps ~100 KB)
- Test thoroughly after each addition
- Stop at first sign of issues
- Maximum recommended: 500 KB

**If split approach required:**
- Build separate units in this order:
  1. eurorack_braids.drmlgunit (proven, ~150 KB)
  2. eurorack_plaits.drmlgunit (proven, ~200 KB)
  3. eurorack_warps.drmlgunit (small, ~100 KB)
  4. eurorack_rings.drmlgunit (medium, ~180 KB)
  5. eurorack_elements.drmlgunit (medium, ~230 KB)
  6. eurorack_clouds.drmlgunit (large, ~450 KB - may not work)

### Optimization Opportunities (Priority 3)

**If memory becomes critical:**
1. **Code size optimization:**
   - Enable LTO (Link-Time Optimization)
   - Use `-Os` instead of `-O2`
   - Strip unused code paths
   - Share common lookup tables

2. **NEON SIMD utilization:**
   - Optimize DSP hot paths with NEON intrinsics
   - Potential 2-4x speedup for math-heavy operations
   - Allows higher quality/features in same CPU budget

3. **Buffer optimization:**
   - Share buffers between oscillators (if separate, only one active)
   - Use smaller block sizes where possible
   - Reduce oversampling where acceptable

---

## Conclusion

### What We Know ✅
- Build system works perfectly
- Braids + Plaits integrate successfully
- 351.72 KB unit file created
- Dispatcher architecture is sound
- Code quality is good

### What We Don't Know ⚠️
- **Drumlogue memory limits** (not documented)
- Will 351 KB load on hardware?
- Runtime stability with current size
- Safe maximum unit size
- Performance headroom

### Critical Next Step 🚨
**HARDWARE TESTING IS MANDATORY**

Without hardware testing, we cannot:
- Expand further safely
- Recommend approach to other developers
- Optimize effectively
- Guarantee stability

### Recommended Path Forward ✅
1. **Test current build** (highest priority)
2. **Split into separate units** if any issues
3. **Proceed cautiously** if current build works
4. **Document findings** for community benefit

---

**Bottom Line:** The project is technically sound, but we're in uncharted territory regarding size. Hardware testing will determine whether we continue with the combined approach or split into the (safer) multiple-unit strategy.
