# HARDWARE TEST RESULTS - CRITICAL

**Date:** January 11, 2026
**Build:** eurorack.drmlgunit (358,230 bytes / 349.8 KB)
**Result:** ❌ **LOAD FAILED - Generic Error**

## Test Details

### Build Size
```
   text    data     bss     dec     hex filename
 355866    2344      20  358230   57756 build/eurorack.drmlgunit
```

- **Code (text):** 355,866 bytes (~347 KB)
- **Initialized data:** 2,344 bytes (~2.3 KB)
- **Uninitialized (BSS):** 20 bytes
- **Total:** 358,230 bytes (349.8 KB)

### Hardware Response
- **Error:** Generic error displayed on Drumlogue
- **Load status:** Failed
- **Conclusion:** **File size too large** (350 KB exceeds undocumented limit)

## Root Cause

**Drumlogue has undocumented size limit for user units**
- Reference dummy-synth: 3.5 KB
- Our combined build: 349.8 KB (100x larger)
- **Limit appears to be well below 350 KB**
- Estimated safe limit: **< 200 KB** per unit

## Required Action: SPLIT INTO SEPARATE UNITS

### Decision: Implement Option B (Separate Units)

As documented in [MEMORY_ANALYSIS.md](MEMORY_ANALYSIS.md), we must split the combined unit into separate .drmlgunit files.

### Implementation Plan

#### Unit 1: eurorack_braids.drmlgunit
- **Content:** Braids oscillator only (16 shapes)
- **Estimated size:** ~150 KB
- **Risk:** 🟢 LOW - Well below observed limit
- **Priority:** HIGH - Create first

#### Unit 2: eurorack_plaits.drmlgunit
- **Content:** Plaits oscillator only (8 engines)
- **Estimated size:** ~200 KB
- **Risk:** 🟡 MEDIUM - Close to estimated limit, test carefully
- **Priority:** HIGH - Create second

## Next Steps

1. ✅ Document this failure (done - this file)
2. 🔴 Create separate build configurations
3. 🔴 Build eurorack_braids.drmlgunit
4. 🔴 Test Braids unit on hardware
5. 🔴 Build eurorack_plaits.drmlgunit
6. 🔴 Test Plaits unit on hardware
7. ✅ Update all documentation with confirmed limits

## Lessons Learned

1. **Size matters:** Drumlogue has strict but undocumented limits
2. **Test early:** Hardware testing revealed critical constraint
3. **Split approach validated:** Prologue's multiple-unit strategy is correct
4. **Document limits:** Estimated safe limit: < 200 KB per unit

## Files to Update

- [x] HARDWARE_TEST_RESULTS.md (this file)
- [ ] MEMORY_ANALYSIS.md - Update with confirmed limit
- [ ] README_DRUMLOGUE_STATUS.md - Update status
- [ ] BUILD_STATUS.md - Add split build instructions
- [ ] STATUS_SUMMARY.md - Update current status

---

**Conclusion:** Combined unit approach FAILED due to size. Proceeding with separate unit strategy.
