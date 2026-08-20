/*
 * File: drumlogue_guards.h
 *
 * Two things every drumlogue unit owes the instrument, at the ABI boundary.
 *
 *
 * 1. Parameter values are clamped to the range the unit declared
 * -------------------------------------------------------------
 * `unit_set_param_value()` takes an int32_t, and the firmware pushes a value
 * for every one of the 24 parameter slots — not just the ones a unit uses.
 * The units here then cast that int32_t to uint16_t and hand it to
 * `OSC_PARAM()`, so a value outside a parameter's declared [min,max] does not
 * arrive as an out-of-range number, it arrives as a *large* one: -100 becomes
 * 65436.  Mussola's LFO Rate turns its argument into a frequency with
 * `0.05f * expf(value * 0.0599f)`, which at 65436 is +inf; the LFO phase
 * becomes inf, its sine becomes NaN, and every sample the unit produces from
 * then on is NaN.
 *
 * Clamping here rather than in each unit's OSC_PARAM keeps the guarantee in
 * one place and makes it structural: the header already states the range, and
 * this is what enforces it.
 *
 *
 * 2. A render never returns a non-finite sample
 * --------------------------------------------
 * This one matters more than it looks.  The drumlogue mixes every part
 * through shared send effects, and a reverb or delay is an IIR with feedback:
 * once a NaN reaches its delay line, every subsequent output of that effect
 * is NaN, forever, no matter what is fed in afterwards.  A unit that emits one
 * bad block does not produce one bad block of audio — it silences the whole
 * instrument until the effect is rebuilt or the power is cycled, and it keeps
 * it silent across a change of unit, which reads exactly like the audio engine
 * having crashed.
 *
 * So the last thing a render does is drop non-finite samples.  The unit goes
 * quiet, which is a fault the user can hear and recover from by moving a knob;
 * poisoning the instrument's FX bus is not.  The test for it is an integer one
 * (`exponent == all ones` covers NaN and both infinities) because these units
 * are built with -ffast-math, where isnan()/isfinite() may be folded away —
 * the same reason mussola.cc's per-voice watchdog is written that way.
 *
 * This is containment, not a cure: an engine whose filter state has latched
 * NaN keeps producing it, and recovering that is the engine's own business
 * (mussola.cc re-initializes the offending voice).  What this guarantees is
 * that the damage stops at the unit boundary.
 */

#ifndef DRUMLOGUE_GUARDS_H_
#define DRUMLOGUE_GUARDS_H_

#include <stdint.h>
#include <string.h>

#include "runtime.h"
#include "unit.h"

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

/*
 * This unit's own target word, as a compile-time constant.
 *
 * `unit_header` is the one data symbol drumlogue/unit_exports.map has to keep
 * exported, every drumlogue unit ever built defines it, and the device loads
 * them all into one address space.  A reference to it from inside the unit
 * therefore goes through the GOT and is resolved against the shared dynamic
 * scope — so `unit_header.target` read inside unit_init() is not necessarily
 * *this* unit's target, it is the first-loaded unit's.  Load an FX unit after
 * a synth and its target check compares delfx against synth, fails, and the
 * unit stays uninitialized: silence, with nothing to see in the UI.
 *
 * The shipped binaries escape this only because the SDK builds with -flto,
 * which folds the constant and emits no relocation at all.  That is luck, not
 * design; a non-LTO build of the same sources reproduces the failure exactly.
 * Comparing against the constant removes the reference and the dependency on
 * the optimizer, and header.c initializes `.target` from the same macro so the
 * two cannot drift.
 */
#if defined(CLOUDS_FX)
#define UNIT_OWN_TARGET (UNIT_TARGET_PLATFORM | k_unit_module_delfx)
#else
#define UNIT_OWN_TARGET (UNIT_TARGET_PLATFORM | k_unit_module_synth)
#endif

/*
 * This unit's own header, under a name that cannot be preempted.
 *
 * The trap described above for `.target` is not limited to `.target`: *any*
 * reference to `unit_header` from inside a unit binds to the first-loaded
 * unit's copy. The clamp below cannot dodge it with a constant the way the
 * target check does, because it needs the whole params table -- so header.c
 * defines a hidden alias for the same object, and the clamp reads that.
 * Hidden symbols are not preemptible, so this binds to this unit's copy at
 * static link time while `unit_header` stays exported for the firmware's
 * dlsym(). Same storage, same bytes, different binding.
 *
 * Getting this wrong is not subtle in effect. Every unit would clamp its
 * parameters to the first-loaded unit's ranges: load Plaits (13 params)
 * before Rings (21) and Rings' Chord knob stops at 8 instead of 13, while its
 * eight LFO controls sit at indices Plaits declares as unused -- min 0, max 0
 * -- and are frozen at zero. Silent, and it depends on install order.
 */
#ifdef __cplusplus
extern "C" {
#endif
extern const unit_header_t unit_header_own __attribute__((visibility("hidden")));
#ifdef __cplusplus
}
#endif

/**
 * Clamp a parameter value to the range this unit's header declares for it.
 * `id` must already be known to be < UNIT_MAX_PARAM_COUNT.
 */
static inline int32_t unit_param_clamp(uint8_t id, int32_t value) {
  const unit_param_t *const p = &unit_header_own.params[id];
  if (value < (int32_t)p->min) return (int32_t)p->min;
  if (value > (int32_t)p->max) return (int32_t)p->max;
  return value;
}

/**
 * Replace NaN and +/-inf with silence in an interleaved stereo buffer.
 * `frames` is frames, not samples.
 */
static inline void unit_drop_nonfinite(float *out, uint32_t frames) {
  const uint32_t n = frames * 2;
#ifdef __ARM_NEON
  const uint32x4_t exponent = vdupq_n_u32(0x7F800000u);
  const float32x4_t zero = vdupq_n_f32(0.0f);
  uint32_t i = 0;
  for (; i + 4 <= n; i += 4) {
    const float32x4_t v = vld1q_f32(out + i);
    const uint32x4_t bad =
        vceqq_u32(vandq_u32(vreinterpretq_u32_f32(v), exponent), exponent);
    vst1q_f32(out + i, vbslq_f32(bad, zero, v));
  }
  for (; i < n; ++i) {
    uint32_t b;
    memcpy(&b, &out[i], sizeof(b));
    if ((b & 0x7F800000u) == 0x7F800000u) out[i] = 0.0f;
  }
#else
  for (uint32_t i = 0; i < n; ++i) {
    uint32_t b;
    memcpy(&b, &out[i], sizeof(b));
    if ((b & 0x7F800000u) == 0x7F800000u) out[i] = 0.0f;
  }
#endif
}

#endif /* DRUMLOGUE_GUARDS_H_ */
