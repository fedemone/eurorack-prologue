Clouds on drumlogue — audio-thread notes
========================================

> ## ⚠ Spectral hung the drumlogue. That is fixed. It is still CPU-marginal.
>
> **Round one, on hardware:** `clouds` in **Mode 3 (Spectral)** crackled and,
> after a few seconds of continuous use, **froze the whole instrument,
> recoverable only by a power-cycle.** The cause is measured, and is **not
> memory corruption** — it is the phase vocoder's FFT running on the audio
> thread as one burst per hop, missing the deadline on 3.12% of blocks for as
> long as Spectral was selected. Two changes address it: the FFT was shrunk
> from upstream's 4096 to 512, and the stereo pair's two transforms were split
> apart so they no longer land in the same host render.
>
> **Round two, on hardware: the freeze is gone, and Spectral still clicks.**
> The burst model was right about the hang and did not account for what was
> left. The binding constraint is now Spectral's *average* cost, not its peak,
> and the two are not optimised the same way — see
> [The prediction, and what hardware said](#the-prediction-and-what-hardware-said).
>
> Treat Mode 3 as usable only sparingly: expect clicks, avoid fast parameter
> changes while it plays, do not use it live. Modes 0-2 have been fine on
> hardware throughout.

Working notes from debugging audio dropouts ("audio interface crash") in the
`clouds` synth unit and the `clouds_fx` insert effect on real hardware.

Read this before changing anything in `clouds-granular.cc`, `clouds-fx.cc` or
the drumlogue wrappers. It records what was measured, what was fixed, what is
still broken, and — importantly — a few plausible-sounding theories that
turned out to be wrong.

**Status summary**

| Unit | State |
|------|-------|
| `clouds` (synth) | Modes 0-2 working on hardware. Mode 3 (Spectral) hung the instrument at FFT size 4096; at 512 the **hang is fixed, confirmed on hardware**, but the mode is still heavy enough to click. Stretch now has the thinnest margin of the four. |
| `clouds_fx` (delfx) | Crash **fixed** on hardware. CPU cost high; was unstable in Spectral and at Position 100% + Density 100%. Shares the engine fork, so it gets the smaller FFT — and needed it: 1024 was enough for the synth but not for this unit. Roughly 1.5x the synth's cost per render. |

---

Measurement method
------------------

Numbers come from the real ARM engine build (`arm-linux-gnueabihf-g++`, the
SDK's own flags: `-march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4 -Os
-ffast-math -ftree-vectorize`) running under `qemu-arm`.

Two harnesses, and it matters which one a number came from:

- **Engine-level** (`bench_mode.cc`, `bench_dens.cc`): times
  `GranularProcessor::Prepare()` and `::Process()` separately per 32-frame
  block. Good for attributing cost inside the engine.
- **Per-block distribution** (`bench_clouds_spike.cc`, `make
  bench-clouds-spike`): same split, but reports p99/p99.9 and the fraction of
  blocks over the deadline rather than only the mean, with Granular as a
  no-burst reference row to calibrate the host. This is the one that found
  the Spectral freeze and chose the FFT size, and the only one of the three
  committed to the repository — the other two were throwaway harnesses.
- **Scheduling differential** (`test_clouds_pvoc_rr.cc`, `make
  test-clouds-pvoc-rr`): not a timing harness at all — it compiles the same
  rendering with the phase vocoder's round-robin on and off and compares the
  output sample for sample, at every FFT size. What makes a scheduling change
  safe to make is that it changes nothing audible, and that is a correctness
  question, not a performance one.
- **Per-unit** (`bench_units.c`, `make bench-units`): dlopens the shipped
  `.drmlgunit` binaries and times whole `unit_render()` calls at the buffer
  size the firmware asks for, reporting the same distribution. It is a level
  above the others — the number includes the wrapper, the adapter's block
  buffering and every reconfiguration a unit does on the audio thread — and
  it is the only scale on which two *different* units can be compared.
  `-p Name=Value` sets a parameter first, which is how a mode is selected.
- **End-to-end** (`bench_cycle.cc`, `bench_fx.cc`): times whole `OSC_CYCLE`
  calls in pairs, and whole 64-frame FX renders,
  because the drumlogue asks the adapter for 64 frames and the adapter fills
  that with two 32-sample calls. **This is the deadline that actually
  exists**, and since the 32 kHz pipeline deliberately makes one call in three
  free, per-call figures are misleading — a pair is the smallest honest unit.
  The gap between this and the engine-level harness is not cosmetic: a 64-frame
  render spans 1.33 engine blocks, which is exactly why spreading the two
  channel transforms one block apart looked like a win engine-side and was
  nearly worthless end to end.

**These percentages are relative, not absolute.** QEMU is roughly an order of
magnitude slower than the real SoC, so a row reading "25%" does not mean the
unit uses a quarter of the drumlogue's CPU. Comparisons *between* rows are
meaningful; the absolute values are not. QEMU also does not model memory
bandwidth, cache behaviour or the denormal penalty, so anything dominated by
those is understated here — and it prices every NEON instruction about the
same, so it cannot rank two vectorisations against each other at all.

The end-to-end harness reports the **99.9th percentile** rather than the
single worst block: under QEMU the maximum is dominated by host scheduling
noise, not by the code under test. Dropouts are still a tail phenomenon — a
mean comfortably under budget with a large spike still drops audio — so the
tail is reported, just not the one sample of it that is pure noise.

Function-level attribution comes from `gprof` on an x86 `-O2` build. That
does not transfer as an absolute cost, but the *ranking* does, and it is the
only way to see inside the inlined engine.

---

Hardware results and the FFT size
---------------------------------

Everything above and below this section was verified on the host and under
QEMU. This section records what the **real drumlogue** did — the only verdict
that counts — and the change made in response.

**Reported from hardware:**

| Unit | Result |
|------|--------|
| `clouds_fx` | No longer crashes — the park handshake worked. But CPU is too high for the unit to be really usable, and the sound is unstable in Spectral and at Position 100% + Density 100%. |
| `clouds` | Modes 0-2 fine. **Spectral crackles, and after a few seconds of continuous use the whole instrument freezes — recoverable only by unplugging the power.** |

**Second hardware round, after the FFT was shrunk to 512 and the stereo pair
split across the hop:**

| Unit | Result |
|------|--------|
| `clouds` | **The freeze is gone.** Spectral no longer hangs the instrument. It is still heavy enough to click, and fast parameter changes while it plays are suspected — not observed — to be able to push it over. |

That is the change working as designed and not being enough. See *The
prediction, and what hardware said* below for what it moved and what it
did not.

### It is not memory corruption

That was the first hypothesis, because a hard hang usually means something
was overwritten, and the most recent change (the NEON FFT butterfly, which
only runs in Spectral) is exactly the kind of code that gets bounds wrong.
It was ruled out rather than argued away:

- The whole synth test suite — all four modes, all four quality settings —
  was rebuilt for ARM with **AddressSanitizer** and NEON enabled, and run
  under QEMU so the vector path was the one actually executing.
- The only report is upstream's long-known `lut_window[4097]`, a one-element
  read off the end of the grain window LUT in `stmlib::Interpolate`, in
  Granular code that has nothing to do with the FFT. It lands inside the
  padding before `lut_sin` in the real ELF layout. It fires four times and is
  the same finding as before this branch.
- With that made non-fatal (`-fsanitize-recover=address`) the suite runs to
  completion: `ALL PASS (0 failures)`, no other diagnostic of any kind.

The NEON butterfly's four store ranges were also re-derived by hand against
the twiddle-table geometry, and every access is in bounds with the tightest
case exactly touching the last table entry. So the freeze is a *timing*
failure, not a memory one.

### The cost is not high, it is concentrated

Measured per 32-sample engine block at 32 kHz — a 1.00 ms deadline — with the
`eurorack-opt/` fork, i.e. everything this branch already optimised:

| Mode | mean | worst block | blocks over deadline |
|------|-----:|------------:|---------------------:|
| Granular, Density 50% | 8.4% | 33% | 0.00% |
| Granular, Density 100%, Position 100% | 15.9% | 59% | 0.00% |
| Stretch | 7.1% | 29% | 0.00% |
| **Spectral** | **12.0%** | **380-442%** | **3.12%** |

(Worst-block figures here; the harness later switched to percentiles, for the
reason given under the fix below. The over-deadline column is unaffected.)

Spectral's *average* cost is the second-lowest of the four. What breaks it is
that **84% of that average sits in one block out of 32**: `Prepare()` alone
measures 10.07% mean against a 435.82% worst block, with 187 spikes in 6000
blocks — one every 32 blocks, which is exactly the phase vocoder's hop
(4096-point FFT, hop ratio 4, so 1024 samples = 32 blocks). The worst block
moves between 380% and 442% across runs; the spike rate and the fraction of
blocks over deadline do not move at all. Each spike does a
full 4096-point forward FFT, the spectral modifier, and a full inverse — and
`PhaseVocoder::Buffer()` loops over channels, so in stereo that is **two of
each, back to back, inside a single audio block**.

3.12% of blocks miss the deadline, forever, for as long as Spectral is
selected. That matches the symptom precisely: crackling immediately, and a
wedged audio thread once enough of those pile up.

This is a **mis-port, not an inefficiency**. Upstream never runs this on the
audio path: `Process()` is in the DMA interrupt (`clouds.cc:90`) and
`Prepare()` in the main idle loop (`clouds.cc:133-135`), and the
`ready_`/`done_` counter pair in `STFT` exists precisely so the FFT can be
preempted by audio. This port calls the two back to back on the audio thread,
which collapses that decoupling and turns a background task into a deadline
spike. For comparison, the same measurement at 48 kHz before this branch put
the worst block at 1028.83% — the 32 kHz change and the fork more than halved
it, and it is still around 4x over.

### The fix: a smaller FFT

Two fixes were available. The one **not** taken was to run
`phase_vocoder_.Buffer()` on a worker thread — architecturally the correct
one, since it restores exactly what upstream does, and the arithmetic is
generous: the worker would get a full hop to do work measuring a few
milliseconds, and the audio thread would drop to ~1.9% mean. It was rejected
because it cannot be verified from here. It needs `pthread_create` inside a
drumlogue unit, whether the firmware tolerates a plugin spawning a thread is
unknown, and if it does not, the failure mode is *the same hang* — with no
way to tell the two apart without the hardware in hand.

The one taken was to **shrink the transform**: `kMaxFftSize` 4096 → 512,
settable at build time via `CLOUDS_FFT_SIZE`. Peak cost goes as
`(N/2)·log2(N)` while the hop only goes as `N`, so a smaller transform cuts
the burst by more than it raises the burst rate.

Measured with `make bench-clouds-spike`, two runs per size, Granular included
as a reference row because it has no burst mechanism at all and therefore
calibrates the harness and the host:

| `kMaxFftSize` | mean | p99 | p99.9 | over deadline |
|---|---:|---:|---:|---:|
| 4096 (upstream) | 11.1% | 298% | 380-463% | **3.12%** |
| 2048 | 10.3% | 147% | 168-178% | **6.25%** |
| 1024 | 9.9% | 74-79% | 94-96% | 0.01-0.05% |
| **512 (chosen)** | **9.2%** | **35-44%** | **42-50%** | **0.00%** |
| *Granular, reference* | *15.1%* | *22-24%* | *28-30%* | *0.00-0.01%* |

The over-deadline column is the one that matters. At 4096 and 2048 it is
*exactly* 1/32 and 1/16 in every run — structural, one block per hop, not
noise. Note that **2048 is worse than 4096** by that measure: halving the
transform did not bring the burst under the deadline, it only doubled how
often the deadline was missed. The obvious "one step down" would have made
things worse while looking like progress on the mean.

### Why 512 and not 1024 — the two units disagree

The table above is engine-level. Repeating it end-to-end, per whole render
callback — the deadline that actually exists — splits the two units apart:

| | Spectral p99.9 | over deadline |
|---|---:|---:|
| `clouds` synth @ 4096 | 268% | 4.16% |
| `clouds` synth @ 1024 | 79% | 0.05% |
| `clouds` synth @ 512 | 60% | 0.00% |
| `clouds_fx` @ 4096 | 329% | 4.17% |
| `clouds_fx` @ 1024 | 126% | **0.93%** |
| `clouds_fx` @ 512 | 75-82% | 0.00% |

1024 is enough for the synth and **not** for the FX. CloudsFX runs stereo
resampling in both directions with the dry path included, so less of its
64-frame buffer is left for the burst; at 1024 it still missed 0.93% of
renders, which at roughly 750 renders a second is about seven dropouts a
second — audible, and consistent with the instability reported for that unit.
At 512 both clear, and the FX's Spectral tail (p99.9 75-82%) sits alongside
its other modes (Granular 48-51%, Stretch 51-54%) rather than above them.

The two units share one engine fork, so they share the constant; a per-unit
`-D` would work mechanically, since every translation unit of a drumlogue
project sees that project's `UDEFS`, but it would mean Spectral was two
different effects depending on which unit you loaded, and it would put the
host test targets and the unit builds one edit apart from disagreeing. Not
worth it. A synth-only build can raise the constant to 1024 in the header.

This is the change's real cost, and it is worth being blunt about: at 512 the
analysis window is 16 ms with 62.5 Hz bins, against upstream's 128 ms and
7.8 Hz. Spectral keeps far less of its long frozen-pad character. The other
three modes are untouched.

**Percentiles, not maxima.** The harness originally reported the single worst
block, and that number is useless here: the Granular reference row, which
does no FFT at all, produced maxima anywhere between 31% and 305% across
runs under QEMU. p99/p99.9 are stable to a few percent and still catch a
once-per-hop burst, since even the shortest hop fires far more often than one
block in a thousand.

### What this also fixed, by accident

`num_textures` in `PhaseVocoder::Init` is
`min(free buffer space / texture_size, kMaxNumTextures)`, and
`FrameTransformation::Init` takes one of them for phase storage. At 4096 the
port's buffer sizes yielded **2** textures, so `num_textures_` was 1 — a
single magnitude buffer. Both `StoreMagnitudes` and `ReplayMagnitudes`
compute `index = position * (num_textures_ - 1)`, which with one texture is
identically zero.

**The POSITION knob did nothing whatsoever in Spectral mode**, on every build
of this port to date. At 512 the allocation reaches the `kMaxNumTextures = 7`
cap, so POSITION scans six magnitude textures as upstream intends. This was
found while checking that the smaller FFT did not break the allocator, not by
noticing it in use — which is a fair illustration of the point made at the
top of `test_clouds_fft.cc`: in a mode whose job is to smear and randomise, a
dead control does not announce itself.

### The second fix: one channel per call, spread across the hop

The FFT-size change attacks how big the burst is. This one attacks the fact
that it is a single burst. `PhaseVocoder::Buffer()` loops over the channels,
so in stereo a forward FFT, the modifier and an inverse run **twice, back to
back, inside one audio block**. Splitting the pair halves the spike, and the
buffers already have room: `STFT`'s analysis ring is `fft_size + hop_size`
long while `Buffer()` reads `fft_size` of it, leaving exactly one hop between
the write pointer and the window being transformed.

Where the second transform lands turned out to matter more than that it was
moved at all:

| CloudsFX, Spectral, p99.9 per render | defaults | reverb 60 % + texture 90 % |
|---|---:|---:|
| upstream scheduling | 95–111 % (0.05–0.53 % over) | 101–111 % (0.10–0.53 % over) |
| split, adjacent blocks | 85–100 % (0.05–0.08 % over) | 107–121 % (0.62–0.97 % over) |
| split, spread across the hop | **63 %** (0.00 % over) | **69 %** (0.00 % over) |

Adjacent blocks bought almost nothing, and the reason is a granularity
mismatch that the engine-level bench cannot see. The deadline that exists is
one 64-frame host render, and with the engine at 32 kHz that render spans
1.33 audio blocks — so two transforms one block apart still share a render
about a third of the time. Half a hop apart they never do. `Init()` sets the
spacing to `hop_blocks / num_channels`, which at 512 is two blocks out of a
four-block hop.

On the synth the same change takes Spectral's p99.9 from 84 % of deadline to
44 %, and at engine level from ~71 % to 36 % — below Stretch's 38 %, where
before it was at twice everything else.

**It does not buy the window back.** 1024 with the split still misses 1.12 %
of renders on the FX, so `CLOUDS_FFT_SIZE` stays at 512 and the sonic cost
above stands. What it buys is margin — roughly a factor of two on both units,
on the mode that was hanging the hardware. The synth alone clears 1024
comfortably (p99.9 90 %, no misses), which is what keeps the synth-only
override worth documenting.

Steady-state output is **bit-identical** to upstream's scheduling, pinned by
`make test-clouds-pvoc-rr`: the same rendering built with
`CLOUDS_PVOC_ROUND_ROBIN` 1 and 0 and compared sample for sample at every FFT
size from 256 to 4096, stereo and mono, hi-fi and lo-fi. Two findings came
out of getting there and are recorded in `phase_vocoder.cc`:

- **The channel order is load-bearing, in upstream.** `FrameTransformation`
  writes only the lowest `(fft_size / 2) - kHighFrequencyTruncation` bins of
  `ifft_in`, and `ifft_in` is scratch shared by both channels, so every
  transform inherits its top 16 bins from whichever transform ran last. A
  rotation coming up in the opposite phase feeds each channel the other's
  residue and changes the output. The turn therefore advances only when a
  transform actually runs; an earlier version advanced on idle calls, and the
  output then depended on whether Spectral was entered by `Init()` or by a
  mode change.
- **Transitions can land in the gap.** FREEZE engaging, or a mode change
  reallocating the workspace, can arrive between the two channels' transforms
  and leave them a frame apart in what they captured. At 512 the test
  measures no difference; it appears only at 2048 and 4096, where the gap is
  8 or 16 blocks, and then as a fraction of a percent of level.

### The prediction, and what hardware said

The prediction was specific and falsifiable: at 512 with the split, on both
units, Spectral's per-render cost distribution sits inside the range of the
modes that already work on hardware — below them, in fact — and no render
misses its deadline in 4000-8000 sample runs. If Spectral still misbehaves,
the model is wrong somewhere and the worker thread is the next thing to try.

**Hardware verdict: half right.**

| Claim | Result |
|-------|--------|
| The instrument no longer freezes in Spectral | **Confirmed.** The hang is gone. |
| Spectral is comfortable to run | **False.** Still heavy, and it clicks. |

So the burst model was right about the *freeze* and wrong about what was
left over. That distinction is worth being precise about, because it changes
what to optimise next:

- **The burst was the freeze.** 3.12% of blocks missing their deadline,
  forever, is what wedged the audio thread, and removing it removed the hang.
  Nothing since has contradicted that.
- **The mean is now the binding constraint.** Post-fix, measured per whole
  64-frame render with `make bench-units`: Spectral costs 20.3% mean on the
  synth and 32.0% on the FX, with p99.9 at 53.8% and 85.8% and *no* renders
  over deadline. On this harness Spectral now looks fine. Hardware says
  otherwise, and the discrepancy is the point — the harness measures a unit
  alone against the whole buffer period, and on a real drumlogue the unit is
  sharing that period with eleven other parts, the master effects, the
  sequencer and the UI. A unit at 32% of the *whole* deadline can be well
  over its actual share.

That reframes the target. Until now the job was to flatten a spike; now it is
to cut total work, and the two are not optimised the same way. Peak-shaving
changes (a smaller FFT, spreading transforms) barely move a mean; the mean
only comes down by doing less arithmetic, or by doing it somewhere else.

### Where the time actually goes

Per `STFT::Buffer()` call, ARM under QEMU, fft_size 512, unit defaults —
relative shares, not absolute times:

| Phase | µs | share |
|-------|---:|------:|
| FFT forward | 127.4 | 32% |
| `FrameTransformation::Process` | 137.9 | 35% |
| FFT inverse | 101.3 | 26% |
| window in | 8.1 | 2% |
| window out | 20.8 | 5% |

and inside `FrameTransformation`, at the unit defaults (240 bins):

| Step | µs | note |
|------|---:|------|
| `RectangularToPolar` | 36.8 | `fast_atan2r` per bin — LUT plus a Carmack rsqrt |
| `WarpMagnitudes` | 18.6 | **the polynomial is the identity at Size 50%** |
| `StoreMagnitudes` | 10.1 | |
| `SetPhases` | 8.8 | unchanged by phase randomization; the cost is the first loop |
| `PolarToRectangular` | 6.3 | `fast_p2r` per bin |
| `ReplayMagnitudes` | 3.8 | |
| `ShiftMagnitudes` | 1.3 | at pitch ratio 1, two copies |
| `QuantizeMagnitudes` | 0.2 | Texture 50% lands in its dead band, already skipped |

Two things that look like easy wins and are not, recorded so they are not
tried again:

- **Skipping the phase-randomization loop when its amount is zero.** It is
  zero at the default Density, and it is 240 RNG calls that add zero. It also
  costs nothing measurable: `SetPhases` is 8.84 µs with randomization off and
  8.47 µs with it on. The cost is the first loop, not the second.
- **A cheaper `fast_atan2r`.** It is the single largest step, but the LUT
  lookup is a per-bin gather, which ARMv7 NEON cannot do, and the parts that
  *would* vectorise are the rsqrt — which changes every magnitude in the
  spectrum and so cannot be done without changing the output.

### What is left, and what it would cost

Ranked by what they would actually buy, since the incremental work above is
worth about 10% in total and the problem is bigger than that:

1. **Halve the overlap** (`CLOUDS_PVOC_HOP_RATIO` 4 → 2). Halves the number
   of transforms, and so halves nearly all of Spectral's cost — the only
   change here in the right order of magnitude. **Taken**, after hardware
   reported Spectral still clicking. It is COLA-correct: the window is applied
   at both analysis and synthesis, so the effective window is sine² = Hann,
   and Hann at 50% overlap sums to exactly 1; the normalisation in `stft.cc`
   is already derived from `hop_size_`. `make test-clouds-cola` measures that
   rather than trusting the algebra — 0.62 dB reconstruction ripple at both
   ratio 4 and ratio 2, against 9.48 dB at ratio 1 as the control. End to end
   it took Spectral from 20.3% mean per render to **13.6%** on the synth and
   32.0% to **23.9%** on the FX, moving it from the most expensive of the four
   modes to the second cheapest. The price is phase-vocoder artifacts on
   *modified* spectra, which is most of what Spectral is for; unmodified
   pass-through stays exact. Spectral's character had already been cut once by
   the 4096 → 512 change, and the judgement is that a mode which clicks is
   worth less than a mode which is grainier. `-DCLOUDS_PVOC_HOP_RATIO=4`
   restores upstream's overlap at upstream's cost.
2. **Spread one transform across several audio blocks.** `STFT::Buffer()` is
   five sequential phases and the hop carries four engine blocks of slack, so
   a stage machine could run window-in + forward FFT in one call, the
   modifier in the next, the inverse and window-out in a third. This attacks
   the peak, not the mean — worth less now than it would have been before the
   FFT shrank. It also needs per-channel scratch: `PhaseVocoder::Init`
   allocates one `fft_buffer`/`ifft_buffer` pair shared by both channels
   (8 KB more), and the shared top-16-bin residue documented in
   `phase_vocoder.cc` is load-bearing, so the change is not as local as it
   looks.
3. **A worker thread.** Still the architecturally correct answer, still
   unverifiable from here: it needs `pthread_create` inside a drumlogue unit,
   and if the firmware does not tolerate that the failure mode is a hang
   indistinguishable from the one just fixed. Now testable in principle,
   since there is hardware in the loop — but it should be tried on its own,
   not folded into another change.

CloudsFX's instability at POSITION 100 % + DENSITY 100 % is still
undiagnosed and is a separate matter from the FFT burst: that setting
measures 15.9 % mean with no deadline misses, so nothing in this analysis
explains it.


### Stretch now has the worst tail, and it is `LoadCorrelator`

With Spectral's burst gone, Mode 1 is the mode with the thinnest margin left.
Per whole 64-frame render, three runs of 6000 each, with Granular as the
control:

| Mode | mean | p99 | p99.9 | over deadline |
|------|-----:|----:|------:|--------------:|
| Stretch, `clouds` | 15.7-15.9% | 55-60% | 88-93% | 0.02-0.07% |
| Stretch, `clouds_fx` | 24.0-24.3% | 65-67% | 98-102% | 0.07-0.10% |
| *Granular, `clouds`, control* | *19.3-19.7%* | *42-49%* | *59-64%* | *0.00-0.02%* |

The tail is real and repeatable -- consistently about 1.5x the control's --
but the over-deadline column sits close enough to the control's own floor
that it should not be read as 0.07% of renders being dropped.

`Prepare()` in Stretch does two things, and taking each away in turn says
which one is the burst:

| Build | `Prepare()` mean | `Prepare()` worst |
|-------|-----------------:|------------------:|
| stock | 3.25% | 63.67% |
| without `EvaluateSomeCandidates()` | 1.94% | 47.46% |
| without `LoadCorrelator()` | 0.13% | 7.91% |

`LoadCorrelator()` is it. It packs sign bits for a `window_size_` source
window and a `2 * window_size_` destination window in one call -- up to 6144
interpolated buffer reads, doubled for stereo -- in whichever block follows a
window being scheduled. `EvaluateSomeCandidates()` is already incremental and
costs almost nothing on average; upstream sized it that way deliberately.

**A reorder was tried and rejected first.** `EvaluateNextCandidate()` returns
immediately once `done_`, so moving the candidate batch *before*
`LoadCorrelator()` -- behind `if (!correlator_.done())` -- makes the block that
packs the windows never also run a batch. That took Stretch's p99.9 from 49.4%
to 38.5% of the engine block deadline for three lines and no new state, but
the search then finishes a block later, and rendered output **differs at Size
20%** (0.28% RMS) where `best_match()` is read before the search completes and
WSOLA settles for a worse alignment.

**What shipped is the split itself.** `LoadCorrelator()` now does the source
window on one `Prepare()` and the destination window plus `StartSearch()` on
the next, in a fork of `wsola_sample_player.h`. Measured at Size 85%, where
the burst is real, four paired runs:

| | mean | p99 | p99.9 | over deadline |
|---|---:|---:|---:|---:|
| before | 11.3-12.4% | 77-83% | 82-149% | 0.01-0.38% |
| after | 11.9-12.8% | **61-66%** | **81-96%** | **0.01-0.07%** |

About a fifth off p99, and the worst p99.9 across runs goes from 149% to 96%.
The `max` column moves between 115% and 1608% in *both* builds and is the
usual QEMU scheduling noise; it is not reported above for that reason.

Three things about it were decided by a 120-point differential sweep against
upstream -- size x position x pitch -- rather than by argument, because each
of them looked obviously right and two of them were wrong:

- **Which half is deferred.** Source first, destination second: 3 differing
  points. The other order: 12. `search_source_` is the window WSOLA is about
  to play, so it tracks the read position, which sits closer to the write head
  than `search_target_` does.
- **When to split at all.** A `window_size_` threshold, not a measured count
  of the blocks between windows. The measured version sounds better founded
  and is worse -- 18 differing points -- because the gap varies from window to
  window, so a long interval followed by a short one lets through a split that
  does not fit. The threshold is 1024, calibrated from the gap actually
  observed: ~500 blocks at window 2048 and unity pitch, 8 blocks at window
  2048 with pitch ratio 4, 8 at window 512, and 3 below window 256. The burst
  is O(`window_size_`) and the gap grows with it too, so the two move together
  in the helpful direction and the sizes excluded are the ones with nothing to
  win.
- **Not deferring a read that is about to be overwritten.** The destination
  window's top edge sits at `head - limit * POSITION`, so at POSITION 0 it *is*
  the write head, and deferring it reads a block of audio recorded after the
  window was scheduled -- 32 samples of a 2048-sample correlation window, and
  occasionally enough to flip the best match. That was the last residue in the
  sweep, at POSITION 0 with +-24 semitones. Requiring two blocks of margin
  clears it, at the cost of POSITION 0 not getting the split.

With all three in place the sweep is **120 of 120 bit-identical to upstream**.
At the units' defaults the split does not engage -- Size 50% puts
`window_size_` near 727, below the threshold -- so this is a fix for the large
Size settings, which is where Stretch was missing deadlines: 0.19% of renders
at Size 85% before, against 0.00-0.02% for the Granular control.

---

What was wrong, and what fixed it
---------------------------------

### The grain trigger (fixed)

The port used to set `parameters.trigger` on every block to work around
Clouds' DENSITY dead zone. `GranularSamplePlayer::Play()` treats `trigger` as
"seed one grain now", so that was 1500 grains/second: it pinned the grain
pool at maximum regardless of where DENSITY was set, put a 1500 Hz
(block-rate) buzz on everything, and held the engine at worst-case CPU
permanently. A trigger is now seeded once per note-on and the scheduler runs
the cloud.

### Control-thread callbacks corrupting audio-thread state (fixed)

See the "Control thread vs audio thread" section of the README. Two real
crashes: `unit_reset()` re-initializing the engine inline, and
`osc_adapter_reset()` clearing the render cursor pair inline (which underflows
`avail` to ~2^32 and walks the read position off the end of the buffer).

### Denormals (fixed)

The SDK compiles every unit with `-ffast-math -funsafe-math-optimizations`.
The drumlogue Makefile spells out the assumption:

```make
OPT += -funsafe-math-optimizations ## denormals assumed 0, so breaks IEEE754
```

The compiler assumes it; nothing tells the hardware. GCC normally arranges
that by linking `crtfastmath.o`, which sets FZ in FPSCR at startup — but
`crtfastmath.o` goes into executables, never into shared objects, and a
`.drmlgunit` is a plain `-shared -fPIC` ELF loaded with `dlopen()`. A unit
therefore inherits whatever FPU mode the host audio thread happens to be in.

Clouds is unusually exposed: the reverb and diffuser tails, the feedback
high-pass and the `ONE_POLE` smoothers on dry/wet, freeze and grain gain all
decay exponentially toward zero, so they sit in the denormal range for a long
time after the sound stops. Scalar VFP is far slower there, which makes a
quiet decaying tail the most expensive audio the unit ever renders.

`drumlogue_fpu.h` sets FZ and DN around `unit_render()` and restores the
caller's FPSCR on the way out. Corroboration that this is a real drumlogue
problem and not a theory: the SDK's own third-party `reverb_labirinto` sets
FPSCR itself at the top of its process function
(`NeonAdvancedLabirinto.h:823`).

> Note: `reverb_labirinto` sets bit 22 intending DN. On ARMv7 bit 22 is the
> low bit of the rounding mode, so that actually selects
> round-toward-plus-infinity. DN is bit 25; FZ is bit 24. This port uses
> `(1<<24)|(1<<25)`.

QEMU does not model the denormal penalty, so no test in this repo can
demonstrate either the failure or the fix. This one rests on the evidence
above, not on a reproduction.

### The sample-pointer race (fixed)

`load_sample()` published `ptr`, `frames` and `channels` as three separate
variables, writing the pointer last. That does not make them consistent for
the reader: the audio thread reads the pointer *first*, so it could take the
old pointer together with the new frame count, then index a 64-frame sample
with a 48000-frame length and read straight off the end. Any SampleNum or
SampleBank change while a note sounded could do it.

Each load now fills the slot that is not currently published and releases its
address; the reader acquires one record whose fields were written together.

---

DENSITY and CPU
---------------

The engine derives its grain count from DENSITY with a cubic law
(`granular_processor.cc`, `sample_player.cc`):

```
overlap            = (density - 0.53) * 2.12
target_num_grains  = max_num_grains * overlap^3
```

Mapping a linear knob straight onto `density` inherits that cube, which puts
almost all of the range — and almost all of the CPU — in the last fifth of
the travel. Measured, StereoHi, Granular:

| knob | grains (old) | mean (old) | grains (new) | mean (new) |
|------|--------------|-----------|--------------|-----------|
| 0%   | 1.0  | 12.19% | 1.0  | 12.28% |
| 20%  | 3.0  | 14.47% | 4.5  | 17.74% |
| 40%  | 6.6  | 22.74% | 7.9  | 24.52% |
| 60%  | 12.2 | 23.02% | 11.4 | 22.71% |
| 80%  | 20.4 | 28.15% | 14.8 | 23.23% |
| 100% | 31.7 | **38.45%** | 18.2 | **25.43%** |

The old top fifth of the knob added more grains (11) than the bottom three
fifths combined (12), and hardware started crackling above about 80% — right
where the curve turns up.

`density_to_clouds()` now inverts the cube so the knob is linear in *grain
count*, and caps the top at `kGrainsMax` (0.57 of the pool, ~18 of 32 grains
in StereoHi). Full knob is now 34% cheaper than before and sits below the
old 80% point where crackling was reported. The whole range is usable
instead of the top fifth being unaffordable and the rest bunched.

`kGrainsMax` is a fraction of the pool the engine allocated, so it tracks
Quality automatically (32 grains StereoHi, 40 mono, more when low-fi).

The mapping is computed on the control thread and cached in
`density_mapped_`, so the cube root never runs under the audio deadline.

**Note the large fixed cost.** Even at one grain a block costs ~12%. Clouds
runs `diffuser_.Process()` and `reverb_.Process()` unconditionally every
block, regardless of the Reverb knob (`granular_processor.cc:216-268`) — the
knob only sets an amount. Skipping them when the amount is zero would be a
real saving but needs a change inside `eurorack/`, which this port does not
modify.

---

Prepare() on the audio thread
-----------------------------

**This is the biggest remaining structural problem, and it is the reason
Stretch and Spectral crackle.** Hardware has since confirmed it is also what
freezes the instrument in Spectral, and the FFT has been shrunk in response
— see [Hardware results and the FFT size](#hardware-results-and-the-fft-size)
for the post-32 kHz measurements and what was changed. The rest of this
section is the original 48 kHz analysis that led there.

In the original firmware `Process()` runs in the audio interrupt
(`clouds.cc:90`) but `Prepare()` runs in the **main idle loop**
(`clouds.cc:133-135`), spinning in whatever cycles are left over. It is not
deadline work. This port calls `Prepare()` once per 32-frame block from the
audio thread.

What `Prepare()` does per mode:

| Mode | Work per call |
|------|---------------|
| Granular / Delay | Nothing, once buffers are set up |
| Stretch | `ws_player_.LoadCorrelator()` + `correlator_.EvaluateSomeCandidates()` |
| Spectral | `phase_vocoder_.Buffer()` |

`EvaluateSomeCandidates()` runs `(size >> 2) + 16` candidates, each XOR-ing
and popcounting `size >> 5` words — a burst of tens of thousands of word
operations, unbudgeted, in one audio block. Measured at density 50%,
StereoHi, **engine-level and with the engine still clocked at 48 kHz** (these
rows are what motivated the 32 kHz change; the end-to-end figures after it are
in the next section):

| Mode | Process mean | Prepare mean | Prepare **worst block** |
|------|--------------|--------------|-------------------------|
| Granular | 23.32% | 0.06% | 0.87% |
| Stretch  | 14.53% | 4.90% | **65.67%** |
| Delay    | 15.45% | 0.06% | 3.16% |
| Spectral | 8.28%  | 24.42% | **1028.83%** |

Spectral's worst `Prepare()` block costs ten times the entire block budget on
its own.

**Calling `Prepare()` less often does not fix it** — this was measured before
being assumed:

| Prepare() every | total mean | worst block |
|-----------------|-----------|-------------|
| 1 block  | 19.28% | 108.74% |
| 2 blocks | 18.81% | 129.43% |
| 4 blocks | 18.24% | 99.89%  |
| 8 blocks | 17.66% | 85.96%  |
| 16 blocks| 16.85% | 106.48% |

The correlator's total work is set by the splice rate, not the call rate, and
`EvaluateNextCandidate()` returns immediately once `done_` — so skipping calls
only defers the same work into a later block. The burst, which is what
actually breaks the deadline, is unchanged.

Fixing this properly needs one of:

1. **A background worker thread** calling `Prepare()`, mirroring the original
   firmware's idle loop. Architecturally faithful, but the original's
   main-loop/interrupt overlap is a data race that bare metal got away with
   and a preemptive scheduler may not. **Not done.**
2. **Running the engine at its native 32 kHz** with sample-rate conversion at
   the boundary. **Done — see the next section.** It does not make the burst
   go away; it makes it happen two-thirds as often and shrinks every other
   mode along with it.
3. **Shrinking the FFT**, which trades Spectral's analysis window for a
   smaller burst. Only applies to Spectral — Stretch's burst is the
   correlator, not the FFT. **Done, at `kMaxFftSize` 512, after hardware
   showed 4096 hanging the instrument.**
4. **Splitting the burst across the hop** rather than shrinking it: one
   channel per `Buffer()` call, spaced half a hop apart, so a stereo pair's
   two transforms never share a host render. Costs nothing sonically —
   steady-state output is bit-identical — and halves the spike again on top
   of (3). Spectral only, for the same reason as (3). **Done.**

Granular and Delay remain the modes with no burst at all. Stretch still has
one, and nothing on this branch removed it: it is smaller than Spectral's was
(worst block ~29% against ~400%) and has not been reported as a problem, but
it is the same structural mistake. Note that (4) does not generalise to it —
there is only one correlator, so there is nothing to take turns. The worker
thread remains the only fix for Stretch.

---

Running the engine at 32 kHz
----------------------------

`GranularProcessor::sample_rate()` returns `32000 / (low_fidelity ? 2 : 1)`.
Everything derived from it — the feedback high-pass corner, the phase
vocoder's frame rate, the grain scheduler's notion of time, the buffer's
capacity in seconds — assumes the engine is clocked there. This port used to
feed it 48 kHz directly, which cost 1.5x the work per second of audio Clouds
hardware ever did *and* left every one of those constants a third off.

`clouds_src.h` now converts at the boundary. 48 and 32 kHz are exactly 3:2, so
this is a fixed rational resampler — no drift, no position-dependent
interpolation error, coefficients computed once offline.

**The filter.** One prototype low-pass shared by both directions, designed at
the 96 kHz common multiple: 120 taps, Kaiser (β = 8), -6 dB at 14.4 kHz.
Measured response — flat to 13 kHz, **-42 dB at 16 kHz**, -82 dB at 17 kHz,
below -95 dB from 18 kHz. The 16 kHz figure is the one that matters: it is the
Nyquist of the 32 kHz engine, so it bounds both the aliasing folded in going
down and the imaging let through coming back up. Round-tripping a unit sine
48→32→48 kHz measures 0.00 dB to 12 kHz, -0.37 dB at 13 kHz, -27 dB at 15 kHz.

**Where the conversions are.** Not everywhere, because a conversion nobody
needs is pure cost:

| Path | Conversion |
|------|-----------|
| Synth, sawtooth source | **None.** Generated directly at 32 kHz. It is a naive ramp with no band limiting either way, so decimating it would spend two 60-tap filters cleaning up aliasing the generator puts straight back. |
| Synth, sample source | 48 → 32 kHz, stereo. Sample data is 48 kHz and genuinely needs anti-aliasing; stepping the read pointer 1.5 frames at a time is audible on anything bright. |
| Synth output | 32 → 48 kHz, mono (after the L+R mix, which is linear and therefore free to do first). |
| FX input and output | 48 → 32 → 48 kHz, stereo both ways. |

The FX converts its **dry** signal too. Keeping dry at 48 kHz and mixing it
outside the engine would preserve its top octave, but it would also put it
~1.2 ms ahead of the wet path, and a delayed copy summed with an undelayed one
is a comb filter — much more audible than a rolloff above 13 kHz. Clouds' own
codec band-limits its dry path the same way, so this is also the more faithful
option.

**Scheduling.** Both units are pull-driven: ask the upsampler how many 32 kHz
samples the next output block needs, then run engine blocks until the staging
FIFO holds that many. Two calls in three run a block; the third runs none. The
FX additionally buffers its incoming audio, because unlike the synth it cannot
generate input on demand — the rates are exactly rational so the deficit is
deterministic, and simulating the counters over buffer sizes from 16 to 128
frames puts the exact minimum cushion at 32 samples. It primes 48, for 1 ms of
latency plus the two conversions' group delay: **~2.2 ms** through the FX,
~1.2 ms through the synth.

**Measured, end-to-end, per 64-frame buffer** (Granular unless stated,
StereoHi, `bench_cycle.cc`):

| Setting | 48 kHz engine | 32 kHz engine | Change |
|---------|--------------:|--------------:|-------:|
| Density 0 %   | 15.30 % | 13.69 % | -11 % |
| Density 40 %  | 28.86 % | 23.20 % | -20 % |
| Density 60 %  | 28.90 % | 22.84 % | -21 % |
| Density 80 %  | 31.35 % | 23.25 % | -26 % |
| Density 100 % | 34.43 % | 25.64 % | -26 % |
| Stretch       | 22.84 % | 18.85 % | -17 % |
| Delay         | 19.14 % | 15.64 % | -18 % |
| Spectral      | 40.84 % | 29.69 % | -27 % |

The tail improves too: Granular's 99.9th percentile 68.6 % → 60.1 %, Stretch
46.4 % → 39.7 %, Spectral 585 % → 506 %.

Not the full third, because the upsampler is not free — it is 40 taps per
output sample, and `gprof` puts the whole port layer at 17.5 % of Granular
mode. That is also why the sawtooth path skips the input decimator: the first
version converted it too and gave back most of the win.

**Pitch is unaffected**, and there is a regression test that says so
(`make test-clouds-synth`). Getting this wrong in the obvious way — feeding
the engine 48 kHz samples and playing them back as if they were 32 kHz — still
runs, still produces plausible audio, and is a fifth flat. The test measures
the fundamental with a Goertzel and requires it to beat both the 1.5x and the
1/1.5x impostor by 2x; it currently beats them by about 100x.

What *does* change audibly, by design: SIZE, the delay times and the buffer's
capacity are all 1.5x longer in real time, which is what Clouds sounds like.
The recording buffer also takes 1.5x longer to fill, so a grain reading from a
high POSITION finds material later than it used to.

---

CloudsFX — reconfiguration off the audio thread
----------------------------------------------

Reported behaviour of the previous build: the first trigger produces correct
sound, then the audio interface goes silent.

### What is known

`unit_reset()` cannot re-initialize the engine inline — that rewrites the
buffer pointers and contents `Process()` is reading, and reverting the fix
reproduces `qemu: uncaught target signal 11` under `make test-arm`'s
control-thread race test. So the reset was deferred to the audio thread via
`pending_reset_`, which `clouds_fx_process()` applies at the top of its next
render.

**That trade is almost certainly the current bug.** `engine_reset()` calls
`GranularProcessor::Init()` + `Prepare()`, and a reallocating `Prepare()`
clears:

| Buffer | Bytes |
|--------|-------|
| diffuser (`FxEngine<2048, FORMAT_32_BIT>`) | 8,192 |
| reverb (`FxEngine<16384, FORMAT_12_BIT>`) | 32,768 |
| pitch shifter (`FxEngine<4096, FORMAT_16_BIT>`) | 8,192 |
| audio buffers (`AudioBuffer::Init` → `std::fill`) | 131,072 stereo / 118,784 mono |

`FxEngine::Init()` calls `Clear()`, which zero-fills the whole delay line, and
`AudioBuffer::Init()` zero-fills its buffer. That is **~180 KB written inside
a single audio callback**, on a Cortex-A7 whose whole-instrument budget for
that block is on the order of a millisecond — plus the cache eviction it
causes for every other unit in the kit.

So the shape "first sound is correct, then silence" fits: the first render is
fine, the reset lands on a later block, that block blows the deadline, and the
stream dies. The earlier symptom ("first press → immediate silence") became
"first correct sound, then silence" after the reset was deferred, which is
consistent with the fault simply moving one block later.

The same applies to the synth's Mode/Quality changes, which also reallocate —
but there the reconfiguration is a deliberate user gesture, and hardware
reports Quality switching as working.

### What was searched and not found

Before rewriting anything, `clouds-fx.cc` was driven under ASan + UBSan
through ~48,000 randomised iterations (every parameter, every mode, every
quality, resets interleaved) across several seeds. **The only memory error
found was one shared with the synth** — see `lut_window` under "Ruled out"
below — and it is benign in the shipping binary. There is no memory-safety
defect unique to the FX. That is what pointed at the deadline rather than at a
pointer bug.

### The fix

The reconfiguration happens on the **control thread**, where taking a
millisecond is fine, with the renderer held out of the engine while it runs —
rather than moving 180 KB of work onto the audio thread. One word carries the
protocol:

- `kRunning` — the audio thread owns the engine.
- `kParkReq` — the control thread wants it.
- `kParked` — the audio thread has stood down and will not touch the engine
  until the state is `kRunning` again.

The audio thread only ever writes `kParked`, only after reading a non-running
state, and having written it that call returns without going near the engine.
So once the control thread observes `kParked`, every render from that point on
is on the silent path. A render that read `kRunning` just before the request
landed completes normally and parks on its next call, costing the control
thread one extra block of waiting.

Three details that are easy to get wrong:

- **The transition is a compare-exchange**, `kParkReq → kParked`, not a plain
  store. A control thread that has given up and restored `kRunning` must not
  be pushed back into a park nobody will ever leave.
- **The wait is bounded twice.** `render_count_` decides "the audio thread is
  not running": if it has not moved in 10 ms then no callback happened and the
  engine is ours whatever the park state says (this covers a suspended unit, a
  unit not yet rendering, and a host that calls `unit_reset()` from the render
  thread). A separate 50 ms ceiling covers a renderer that is running but
  somehow never parks — there the reconfiguration is *abandoned*, because
  doing it anyway is exactly the race the handshake exists to prevent.
  `pending_mode_`/`pending_quality_` stay latched, so the next successful park
  picks up whatever was missed.
- **Mode and Quality go through the same park**, not just `unit_reset()`. Both
  set `reset_buffers_`, so both send the next `Prepare()` down the
  reallocating path; latching them for the audio thread would have left the
  spike exactly where it was.

While parked the FX emits silence rather than dry, because the gain a dry
passthrough would need depends on engine state the renderer must not read. A
reconfiguration measures 2-4 blocks.

`test_clouds_fx_reconfig.cc` (`make test-clouds-fx-reconfig`) drives this the
way the drumlogue does — one thread rendering, one turning knobs — at 32, 64
and 128 frames per buffer. Across ~230 reconfigurations per size it checks the
renderer makes progress, never emits non-finite output, never gets stuck
parked (longest observed silent run: 2-4 blocks), and that the last mode
request took effect.

### Divergence from the synth — resolved

`clouds-fx.cc` was previously left behind on the old linear-in-`density`
mapping. It now uses the same grain-linear mapping as `clouds-granular.cc`,
and both units run their engine at 32 kHz. The knobs behave the same way
again.

---

Where the time actually goes
----------------------------

`gprof`, x86 `-O2`, synth at density 50 %, StereoHi, self time. Absolute
values do not transfer to ARM; the ranking does.

**Granular**

| Function | Self |
|----------|-----:|
| `GranularSamplePlayer::Play` (grain overlap-add) | 42.1 % |
| `Reverb::Process` (unconditional — see the fork below) | 20.2 % |
| `OSC_CYCLE` (this port: SRC, staging, Q31 conversion) | 17.5 % |
| `GranularProcessor::Process` (conversion, feedback, dry/wet) | 12.0 % |
| `Diffuser::Process` (unconditional — see the fork below) | 6.6 % |
| everything else | < 2 % |

**Stretch**: `GranularProcessor::Process` 35.6 %, `Reverb::Process` 21.2 %,
port layer 12.7 %, `Prepare()` 9.3 %, `ProcessGranular` 9.3 %,
`Correlator::EvaluateNextCandidate` 6.8 %, `Diffuser::Process` 5.1 %. (The
diffuser cannot be skipped in Stretch: its amount there is `parameters_.density`,
which this port never maps below 0.68.)

**Spectral**: `STFT::Buffer` **56.1 %**, port layer 9.3 %, `Reverb::Process`
9.3 %, `GranularProcessor::Process` 7.8 %, `FrameTransformation::*` ~12 %.

### What was optimised in the port layer

The port layer's 17.5 % is the only column this repo owns outright. Two
changes:

1. **The sawtooth skips the input decimator** (above). Removing two 60-tap
   filters from the default signal path is most of the difference between the
   first 32 kHz build and the numbers in the table.
2. **The polyphase dot product runs both operands forward.** The tables in
   `clouds_src.h` are stored reversed at generation time, so the NEON inner
   loop is load / load / `vmla` with no lane shuffling. The previous version
   ran coefficients forward and samples backward, which needed a `vrev64q` per
   multiply-accumulate. Counted from the disassembly (`-Os`, `neon-vfpv4`):

   | Version | NEON ops per tap |
   |---------|-----------------:|
   | coefficients forward, samples reversed | 1.00 |
   | both forward, two accumulators | 0.75 |

   That is a 25 % reduction in issued NEON instructions, plus the second
   accumulator breaking the multiply-accumulate dependency chain. **This is an
   instruction count, not a cycle count** — QEMU prices every NEON instruction
   about the same and cannot rank the two, and there is no hardware here to
   measure on. The change is justified by what it issues, not by a stopwatch.

### The engine fork — what was done

`eurorack/` is a submodule this repo does not edit, so changing engine code
means forking the file into `eurorack-opt/` and dropping the submodule's copy
from the build. That is a real maintenance cost — MIT-licensed and legally
fine with attribution, but it forks code upstream may still fix — so only two
things were judged worth it. `eurorack-opt/README.md` has the build wiring and
the re-sync procedure; this is the reasoning and the measurements.

**1. Reverb and diffuser early-out** (`granular_processor.{h,cc}`). Both are
literally `in_out += amount * (wet - in_out)`, so at `amount == 0` the output
is *bit-identical* to the input — and both ran unconditionally, every block,
whatever the knobs said. Reverb defaults to 0 in both units, and Granular's
diffusion is 0 for any TEXTURE at or below 75 %, so this is the common case,
not a corner. It is less an optimisation than a missing early-out.

Skipping is not simply "don't call it", though: the delay lines freeze, and
content stale by however long the skip lasted gets released the moment the
amount comes back up — a preset load can take REVERB from 0 to full in one
block. The two effects need different treatment:

- The **reverb** has an input gain, so it can be flushed. Before idling it
  runs 5120 samples with the input muted and both recirculating gains
  (`reverb_time`, `diffusion`) forced to zero. Measured on the real `Reverb`:
  that empties every line to below -100 dBFS in 143 blocks of 32 samples,
  *independent of the reverb time in force* — against 1769 blocks if you just
  mute the input and let it decay naturally at the amount-0 reverb time. 5120
  samples leaves margin over the longest line (4782). Output is unchanged
  throughout, because the amount is already zero.
- The **diffuser** has no input gain to mute, so its all-pass states just
  freeze. Its amount is ramped in over 8192 samples on resume instead; the
  chain decays to inaudibility in about 0.27 s, so the frozen smear is gone
  before the amount is loud enough to hear. This is the only place the fork's
  output deviates from upstream, and only for a quarter second after a resume.

**2. LUT twiddle factors for the FFT** (`pvoc/stft.h`, one line).
`stmlib::RotationPhasor` → `stmlib::LutPhasor`. Both are stmlib and both
produce the same sequence; `RotationPhasor` advances by complex multiplication
(four multiplies and two adds per butterfly group) where `LutPhasor` walks a
table built once in `Init()`. It is also more accurate, since repeated
rotation drifts. Costs 8176 bytes of BSS against the 184 KB each unit already
reserves. Measured with Granular as a control for QEMU noise, the
Spectral/Granular ratio went 1.24-1.33 → 1.13-1.16 across three paired runs:
about half the FFT's excess cost.

**Measured, all three builds in one QEMU session**, end-to-end per 64-frame
buffer, at the units' defaults (REVERB 0, TEXTURE 50 %):

| Setting | 48 kHz, stock | 32 kHz, stock | 32 kHz + fork | Total |
|---------|--------------:|--------------:|--------------:|------:|
| Density 100 % | 33.60 % | 28.43 % | **21.91 %** | -35 % |
| Granular | 29.60 % | 24.19 % | **18.85 %** | -36 % |
| Stretch | 23.46 % | 19.65 % | **14.98 %** | -36 % |
| Delay | 19.91 % | 17.51 % | **11.57 %** | -42 % |
| Spectral | 40.55 % | 31.08 % | **23.58 %** | -42 % |

**CloudsFX gets all of it too** — same forked sources, same include order,
same `CLOUDS_OPT_ENGINE` define in `drumlogue/clouds_fx/config.mk`, and its
defaults (Reverb 0 %, Texture 50 %) put it squarely in the case the early-out
is for. Measured with its own harness, per 64-frame `unit_render`, three
paired runs averaged:

| Mode | stock | fork | | with Reverb 60 % + Texture 90 % |
|------|------:|-----:|--:|--:|
| Granular | 24.51 % | **20.82 %** | -15 % | 23.77 % → 24.02 % (flat) |
| Stretch | 21.45 % | **17.80 %** | -17 % | 22.30 % → 21.67 % (flat) |
| Delay | 18.21 % | **15.01 %** | -18 % | 19.18 % → 20.03 % (flat) |
| Spectral | 29.67 % | **24.97 %** | -16 % | 33.30 % → **29.16 %** (-12 %) |

The right-hand column is the control, and it is the useful half of the table:
with both effects turned up the early-out cannot fire, and the first three
modes come out flat within the run-to-run spread — which is what says the
saving on the left is really the skipped reverb and diffuser rather than
something else that moved. Spectral improves in *both* columns, because the
two FFT changes do not care where the Reverb knob is.

`make test-clouds-engine-opt` compiles the same rendering against both engines
and compares it sample for sample: bit-identical with both effects active,
bit-identical with both skipped, and on the REVERB 0 → full jump the fork's
peak must not exceed upstream's — which is what a flush that failed to empty
the lines would look like.

### Considered and not done

- **A NEON FFT for Spectral — done.** `stmlib/fft/shy_fft.h` is forked and the
  butterfly loop that dominates both transforms is vectorised, four at a time.
  See `eurorack-opt/README.md`; the short version is 33 instructions per
  butterfly down to 9.25, i.e. 3.6x fewer issued, with output unchanged to
  135 dB.

  It was vectorised in place rather than replaced by a radix-4 kernel, and the
  reasoning is worth keeping because it is the general shape of this kind of
  decision. **Numerical drift was never the obstacle**, and the margin is not
  close. Measured at N = 4096 on windowed, int16-quantised input:

  | | |
  |---|---|
  | `RotationPhasor` twiddle error, worst over the longest pass | 1.57e-5 (132 ulp) |
  | `LutPhasor` twiddle error | 2.98e-8 (0 ulp — exact on the float grid) |
  | Resulting spectrum difference between the two | 101.7 dB below the peak bin |
  | Float FFT `Direct` + `Inverse` round trip | 104.5 dB below signal peak |
  | One int16 LSB, same signal | **87.1 dB** below signal peak |

  The last two rows are the argument: `STFT` keeps its analysis and synthesis
  buffers as `short`, so the FFT sits inside a 16-bit quantiser and its own
  error is **17.4 dB below** it. An FFT would have to get roughly 7x less
  accurate before it contributed anything those buffers were not contributing
  already, and a radix-4 kernel would if anything be *more* accurate — six
  passes instead of twelve, so fewer rounding steps. NEON single precision on
  ARMv7 always flushes denormals and does not honour every rounding mode,
  which is irrelevant here on both counts: the port already forces FZ/DN for
  the whole render, and a denormal in a spectrum of int16 audio is hundreds of
  dB below the signal.

  **The risk was the interface, not the arithmetic** — and specifically that
  its failure mode is silent. Spectral mode's job is to blur, randomise phases
  and warp magnitudes, so a wrong spectrum still sounds like a working one;
  unlike the reverb flush, where a mistake was audible as a burst. Four
  specific ways to get it wrong, all of which vectorising in place avoids by
  construction and which `make test-clouds-fft` now pins for anything that
  comes later:

  1. **Layout.** `FrameTransformation` reads a split spectrum: `real =
     &fft_data[0]`, `imag = &fft_data[fft_size >> 1]`
     (`frame_transformation.cc:110-111`), and the imaginary half carries the
     *negated* imaginary part of the usual e^-jwt convention — a cosine at bin
     k gives `real[k] = N/2`, a sine gives `imag[k] = +N/2`. ShyFFT produces
     that; CMSIS produces interleaved, which is why `stft.cc` has an explicit
     de-interleave under `USE_ARM_FFT`. Worth taking seriously: the reference
     DFT written to produce the table above got the sign wrong first time.
  2. **Scaling.** Both directions are unnormalised and `stft.cc` compensates
     with `1/(fft_size * fft_size / hop_size >> 1)`, against
     `1/(fft_size / hop_size >> 1)` on the CMSIS path. Getting this wrong is a
     factor of 4096, and one of the two directions is the loud one.
  3. **Aliasing.** `ifft_in_ = fft_in_` and `ifft_out_ = fft_out_` are the
     same buffers, and "fft_in is lost" is part of the contract.
  4. **Workspace.** The FFT buffers come out of `BufferAllocator` in
     `Prepare()`, sharing the workspace with the grain pool and the correlator.

  Two facts make it a smaller job than it looks: `fft_size` is **always
  4096** — `PhaseVocoder::Init` takes `largest_fft_size` and never reduces it,
  `hop_ratio` is fixed at 4 — so there is one size, it is a pure power of four,
  and `STFT`'s variable-size path is dead code in this port.

  **What is still unmeasured** is the speedup. Cortex-A7's NEON datapath is
  64 bits wide, so a q-register operation occupies it for two beats and the
  cycle-level gain will be smaller than 3.6x. QEMU cannot settle it: it
  emulates NEON through helper calls that cost more than the scalar
  equivalents, so an isolated Direct+Inverse benchmark there puts the
  vectorised version within 0-2 % of scalar in either direction, which is an
  artefact of the emulator. The whole-mode benchmark is likewise inconclusive
  (Spectral 24.7 % scalar vs 24.0 % vector, inside the run-to-run spread).
  **Spectral is the one figure in this document that needs hardware to
  confirm.**

- **Vectorising the grain overlap-add.** 42 % of Granular, so tempting, but
  the return is poorer than the number suggests: `RenderEnvelope` is
  contiguous and vectorises cleanly, while the buffer read underneath it is a
  per-grain gather at that grain's own phase increment through a circular
  int16 buffer, which does not. Expect maybe a quarter of the 42 %.
- **NEON popcount in the correlator.** ARM has `VCNT`, and
  `EvaluateNextCandidate` is XOR + popcount over bit-packed words — but it is
  6.8 % of Stretch, so even a 3x buys ~4 %. Not worth a fork on its own.
- The port layer's own conversions and staging are already NEON or
  memcpy-bound, and `Prepare()` outside Stretch/Spectral rounds to zero.

---

Ruled out — do not re-investigate without new evidence
------------------------------------------------------

- **CPU overload as the cause of the original crash.** Measured on the real
  ARM binaries: VirtAnalog 81.9 µs/block, **Rings 295.7 µs**, Clouds 260.7 µs,
  CloudsFX 261.2 µs. Clouds was *cheaper than Rings*, which runs fine on the
  device. (This does not contradict the DENSITY and Stretch findings above,
  which are about specific settings, not the baseline.)
- **Symbol interposition between units.** Was a real bug, fixed with
  `--version-script`; but the user confirmed the crash persisted with all
  other units removed, so it was not the cause of this one.
- **Memory errors on quality/mode change.** ASan + UBSan across all 16
  mode × quality combinations, 1500 blocks each with a sample loaded: clean.
  Repeated for `clouds_fx` over ~48,000 randomised iterations across several
  seeds, single-threaded and with a live render thread: clean apart from the
  `lut_window` read below.
- **`lut_window[4097]` — a real out-of-bounds read, and harmless here.** ASan
  flags `Grain::RenderEnvelope` reading one element past `lut_window` (4097
  floats) via `stmlib::Interpolate`, which always touches `table[i]` and
  `table[i+1]`. It fires whenever a grain's envelope phase lands on exactly
  1.0 — reachable, because the phase increment is `2/width` and typical widths
  make that an exact binary fraction. It is the same defect class as the
  `lut_xfade_in` overrun that `clip_param()` was written for, but this one
  cannot be fixed from the port layer: `gain` is computed inside the grain
  from its own phase, with nothing to clamp from outside.

  It is benign in the shipping binary, which was checked rather than assumed.
  In `clouds_fx.drmlgunit` the read-only `LOAD` segment spans `0x0-0x13f24`
  and `.rodata` ends at `0x13cb4` with `.unit_header`, `.ARM.exidx` and
  `.eh_frame` after it in the same segment, so four bytes past any object in
  `.rodata` land inside mapped read-only memory. The value perturbs one sample
  of one grain envelope at its exact midpoint, blended by `envelope_smoothness_`.
  ASan only reports it because ASan inserts redzones between globals that the
  real link does not have. **Do not chase this as a crash cause**; fix it if
  and only if the engine gets forked for one of the reasons above.
- **Buffer over-allocation.** Workspace is 53,248 bytes (stereo) against
  42,520 used, with the pitch shifter aliasing to 49,152. It fits, and
  `BufferAllocator` bounds-checks and returns NULL anyway.
- **Grain pool overflow.** `kMaxNumGrains` is 64; the computed maximum is 57.
- **Parameter routing.** drumlogue id 11 → `OSC_PARAM` index 10 → Quality,
  verified; all indices explicitly handled with `default: break`.
- **Output clipping.** Output is scaled by `kOutGain` (~-3 dB) and passed
  through `SoftConvert`; levels were checked and are not the problem.

---

## Where both units stand now

Everything above is a change made in response to something. This is the state
after all of them, measured the same way for every row so the modes can be
compared with each other rather than only with their own history:
`make bench-units`, the real `.drmlgunit` binaries loaded with `dlopen()` and
driven through `unit_render()`, 64-frame renders against a 1.33 ms deadline,
4000 renders per row, ARM under QEMU. Three runs; the spread is the run-to-run
variation, which is host scheduling and not the unit.

| Mode | `clouds` p99.9 | `clouds_fx` p99.9 | over deadline |
|------|---------------:|------------------:|--------------:|
| 0 Granular | 41% | 54% | 0.00% |
| 1 Stretch | 57-66% | **69-79%** | 0.00% |
| 2 Looping Delay | 21-29% | 37-40% | 0.00% |
| 3 Spectral | 37-40% | 26-36% | 0.00% |

Two things in that table are worth saying out loud.

**Spectral is no longer the expensive mode.** On CloudsFX it is now the
*cheapest* of the four, below Granular. It began this work at 380-442% of
deadline with 3.12% of blocks missed, and hung the instrument. The FFT size,
the hop ratio and the worker thread each took a piece of that, and the
remainder is no longer the thing to look at.

**Stretch is, and its margin is the smallest anywhere in either unit.** Nothing
misses a deadline -- 0.00% over, in every run of every mode -- but CloudsFX
Stretch at p99.9 79% is the one row where a busier kit could plausibly change
the answer. The correlator split covers the burst; what is left is the
mode's ordinary per-window cost.

### What is left to optimise, and why it has not been

One identified block of cost remains, and it is CloudsFX's alone: the
48 kHz <-> 32 kHz conversion. Driving the exact call pattern of
`clouds_fx_process()` with the engine removed -- both `SrcDown`s, both
`SrcUp`s, and the FIFO bookkeeping around them -- costs **6.7-7.8% mean,
17.3-21.0% p99.9** of the same deadline over five runs
(`make bench-clouds-src`). That is essentially the whole of CloudsFX's
overhead over the synth, which runs 4.6-6.7 points of mean higher across the
four modes and one converter instead of four.

So it is real, and it is about a third of the FX's mean cost. It has been left
alone for two reasons. It is mean cost, not tail cost -- a p99.9 of ~20% under
a p99.9 that sits at 79% -- and the tail is what drops audio. And every way of making it
cheaper is a quality trade: a shorter kernel, or dropping the dry path's
conversion and accepting the comb filter that
[the note on the SRC in `clouds-fx.cc`](../clouds-fx.cc) rejects. Neither is
worth spending while nothing misses a deadline.

The same applies to the port's unsmoothed parameters. Upstream's `cv_scaler`
smooths every parameter before `GranularProcessor` sees it; both units here
feed the panel value straight through. `make test-clouds-stretch-clicks`
says that is not currently audible as an edge -- POSITION, SIZE and PITCH
sweeps measure within 2% of holding the same knob still -- so adding
smoothing would change how every knob feels in exchange for fixing something
that has not been shown to be broken.

### The Quality knob is not a ladder

Quality is the control anyone reaches for when a unit is struggling, and it is
the obvious fallback if Clouds ever needs one on hardware. It is labelled
StHi / MoHi / StLo / MoLo, which reads as most expensive to least. It is not.

`make bench-clouds-quality`, both units, all four modes, 64-frame renders.
Mean is the statistic to read -- the QEMU tail moves 20% run to run, the mean
does not, and the ordering below held over four independent runs:

| Mode | | Q0 StHi | Q1 MoHi | Q2 StLo | Q3 MoLo |
|------|-|--------:|--------:|--------:|--------:|
| 0 Granular | `clouds` | 14.2% | **12.2%** | 14.9% | 13.6% |
| | `clouds_fx` | 20.7% | **18.7%** | 20.7% | 19.2% |
| 1 Stretch | `clouds` | 12.3% | **10.0%** | 11.9% | 11.2% |
| | `clouds_fx` | 17.6% | **16.3%** | 18.4% | 17.5% |
| 2 Looping Delay | `clouds` | 10.2% | **9.4%** | 12.2% | 11.5% |
| | `clouds_fx` | **15.1%** | 15.6% | 17.4% | 17.3% |
| 3 Spectral | `clouds` | 6.9% | **5.0%** | 7.7% | 7.4% |
| | `clouds_fx` | **11.1%** | 11.7% | 13.9% | 13.6% |

**The Lo half costs more than the Hi half in all eight rows.** Going StHi ->
StLo -- the intuitive "turn the quality down" -- makes the unit *more*
expensive every time. Mono is where the saving is: MoHi is the cheapest
setting in six of eight rows and within half a point in the other two.

The mechanism is visible in `GranularProcessor::Process()`. `low_fidelity_`
runs `ProcessGranular()` at half rate through an extra `SrcDown`/`SrcUp` pair
and switches the buffers to `RESOLUTION_8_BIT_MU_LAW`, so every interpolated
tap becomes a dependent LUT load through `lut_ulaw` instead of a raw int16.
What it does *not* downsample is the diffuser, the pitch shifter or the
filters -- those run on `out_` at full `size`, outside the branch. So it
halves one stage and adds three costs around it.

That trade made sense on the original hardware, where the constraint was
64 KB of SRAM and 8-bit buffers bought twice the recording time. On the
drumlogue the buffer is a static array in a process with room to spare, and
the memory saving buys nothing the CPU does not pay for twice.

Sizing which of the three added costs dominates would be a separate job. What
matters for using the instrument is the direction, and the direction is
reproducible: **if Clouds needs headroom, the setting to reach for is MoHi,
not either Lo.**

### A shorter sample-rate converter, for when CloudsFX needs the headroom

The conversion is CloudsFX's largest single cost after the engine itself, and
`-DCLOUDS_SRC_TAPS=60` halves it. It is off by default; 120 taps is what
ships.

Measured on the real `.drmlgunit`, at the settings from the hardware report
(Stretch, Position 67, Size 26, Density 55, Texture 37, Feedback 14, Dry/Wet
50, Reverb 16, MoHi), three runs of 4000 renders each:

| | mean | p99 | p99.9 |
|---|-----:|----:|------:|
| 120 taps | 24.5 / 24.8 / 24.6% | 48.6 / 56.5 / 49.3% | 66.4 / 82.4 / 69.2% |
| 60 taps | 20.4 / 19.6 / 20.7% | 48.6 / 39.7 / 44.8% | 64.6 / 58.0 / 60.6% |

About 18% off the mean and 16% off the tail. The converters in isolation
(`make bench-clouds-src`) go 10.6% -> 5.6% mean in the same session, which is
the halving the tap count predicts; the rest of the unit is unchanged.

**What it costs is passband, not alias rejection**, and that is a choice
rather than a consequence. A 60-tap Kaiser has about twice the transition
width of a 120-tap one, and that width has to be spent: either the corner
stays at 14.4 kHz and rejection at the engine's 16 kHz Nyquist falls from
-42 dB to -17 dB, or the corner comes down and the rejection is kept. The
short set takes the second, at beta 7 and a 12.5 kHz corner, so it rejects
aliases *better* than the shipped filter.

Measured round trip through the real converters (`make
test-clouds-src-response`), which is two filter passes:

| | 10 kHz | 11 kHz | 12 kHz | 13 kHz | 14 kHz | 15 kHz |
|---|------:|-------:|-------:|-------:|-------:|-------:|
| 120 taps | 0.0 | 0.0 | 0.0 | -0.4 | -6.0 | -27.5 |
| 60 taps | -0.5 | -2.5 | -7.8 | -17.8 | -34.7 | -62.5 |

So: identical below 9 kHz, then a progressive darkening -- 2.5 dB down at
11 kHz, 8 dB at 12 kHz. On a drum bus that is audible on cymbals and hats as
a duller effect return. It is the right way round for this instrument: a dark
return is a tone change a player can compensate for, while aliasing is grit
that was not in the source and does not land anywhere musical.

Both table sets come from `tools/generate_src_tables.py`. That script exists
because the shipped tables were computed offline and the script was not kept,
which left the only description of the filter being a comment about it. Its
first job is `--verify`: it regenerates the 120-tap tables and diffs them
against the header, agreeing to 5e-10 -- the header's own 9-decimal print
rounding. Nothing it emits is trusted until that passes.

To build the units with it:

```
make -C drumlogue/clouds_fx CROSS_COMPILE=arm-linux-gnueabihf- \
    USE_CXXOPT="... -DCLOUDS_SRC_TAPS=60"
```
