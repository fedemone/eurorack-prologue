/*
 * File: drumlogue_fpu.h
 *
 * Put the FPU into flush-to-zero mode for the duration of unit_render().
 *
 * The SDK builds every unit with -ffast-math -funsafe-math-optimizations; the
 * drumlogue Makefile even spells out what that means:
 *
 *     OPT += -funsafe-math-optimizations ## denormals assumed 0, so breaks IEEE754
 *
 * The compiler assumes it, but nothing ever tells the *hardware*.  GCC would
 * normally arrange that by linking crtfastmath.o, which sets FZ in FPSCR at
 * startup — but crtfastmath.o goes into executables, never into shared
 * objects, and a .drmlgunit is a plain -shared -fPIC ELF loaded with dlopen().
 * So a unit inherits whatever mode the host audio thread happens to be in,
 * with no guarantee that denormals are flushed.
 *
 * That matters for Clouds specifically because almost everything in it decays
 * exponentially towards zero once the input stops — the reverb and diffuser
 * tails, the feedback high-pass, the ONE_POLE smoothers on dry/wet, freeze and
 * grain gain.  Left un-flushed those tails spend a long time in the denormal
 * range, where VFP operations are far slower than normal arithmetic, and a
 * quiet decaying tail becomes the most expensive audio the unit ever renders.
 * A steady tone measures fine and the tail after it blows the deadline, which
 * is precisely the "first sound is correct, then the audio dies" shape.
 *
 * FZ (bit 24) flushes denormal operands and results to zero; DN (bit 25) makes
 * every NaN the default NaN so a stray NaN cannot propagate an odd payload.
 * Both are what an audio thread wants.  Note that ARMv7's FPSCR puts DN at bit
 * 25 and the rounding mode at 23:22 — setting bit 22 instead selects
 * round-towards-plus-infinity rather than default-NaN.
 *
 * NEON already flushes denormals unconditionally on ARMv7, so this only
 * changes the scalar VFP paths — which is where the smoothers and filter
 * states live, since they are not vectorizable.
 *
 * The previous FPSCR is restored on the way out: the audio thread is shared
 * with the rest of the drumlogue and with any other unit, and changing their
 * arithmetic behaviour is not ours to do.
 */

#ifndef DRUMLOGUE_FPU_H_
#define DRUMLOGUE_FPU_H_

#include <stdint.h>

#if defined(__arm__) && defined(__ARM_FP)

static inline uint32_t fpu_begin_audio(void) {
  uint32_t prev;
  __asm__ __volatile__("vmrs %0, fpscr" : "=r"(prev));
  const uint32_t want = prev | (1u << 24) /* FZ */ | (1u << 25) /* DN */;
  if (want != prev)
    __asm__ __volatile__("vmsr fpscr, %0" : : "r"(want));
  return prev;
}

static inline void fpu_end_audio(uint32_t prev) {
  uint32_t now;
  __asm__ __volatile__("vmrs %0, fpscr" : "=r"(now));
  /* Only the two mode bits are ours; leave the condition/exception flags the
   * render actually produced alone. */
  const uint32_t restored =
      (now & ~((1u << 24) | (1u << 25))) | (prev & ((1u << 24) | (1u << 25)));
  if (restored != now)
    __asm__ __volatile__("vmsr fpscr, %0" : : "r"(restored));
}

#else /* host builds and tests */

static inline uint32_t fpu_begin_audio(void) { return 0; }
static inline void fpu_end_audio(uint32_t prev) { (void)prev; }

#endif

#endif /* DRUMLOGUE_FPU_H_ */
