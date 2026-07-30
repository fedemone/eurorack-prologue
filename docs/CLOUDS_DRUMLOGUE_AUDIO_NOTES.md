Clouds on drumlogue — audio-thread notes
========================================

> ## ⚠ Spectral mode hung the drumlogue; a fix is in but unconfirmed
>
> **Was confirmed on hardware:** `clouds` in **Mode 3 (Spectral)** crackled
> and, after a few seconds of continuous use, **froze the whole instrument,
> recoverable only by a power-cycle.** `clouds_fx` no longer crashes but is
> expensive, and was unstable in Spectral and at Position 100% + Density 100%.
>
> The cause is measured, and is **not memory corruption** — it is the phase
> vocoder's FFT running on the audio thread as one burst per hop. The FFT has
> since been shrunk from upstream's 4096 to 1024, which removes the
> structural deadline overrun in every measurement available here. **That has
> not yet been checked on hardware**, so treat Spectral as suspect until it
> has been. See
> [Hardware results and the FFT size](#hardware-results-and-the-fft-size).

Working notes from debugging audio dropouts ("audio interface crash") in the
`clouds` synth unit and the `clouds_fx` insert effect on real hardware.

Read this before changing anything in `clouds-granular.cc`, `clouds-fx.cc` or
the drumlogue wrappers. It records what was measured, what was fixed, what is
still broken, and — importantly — a few plausible-sounding theories that
turned out to be wrong.

**Status summary**

| Unit | State |
|------|-------|
| `clouds` (synth) | Modes 0-2 working on hardware. Mode 3 (Spectral) hung the instrument at FFT size 4096; now 1024, which fits the deadline under QEMU but is **unconfirmed on hardware**. |
| `clouds_fx` (delfx) | Crash **fixed** on hardware. CPU cost high; was unstable in Spectral and at Position 100% + Density 100%. Shares the engine fork, so it gets the smaller FFT too. |

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
- **End-to-end** (`bench_cycle.cc`): times whole `OSC_CYCLE` calls in pairs,
  because the drumlogue asks the adapter for 64 frames and the adapter fills
  that with two 32-sample calls. **This is the deadline that actually
  exists**, and since the 32 kHz pipeline deliberately makes one call in three
  free, per-call figures are misleading — a pair is the smallest honest unit.

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

The one taken was to **shrink the transform**: `kMaxFftSize` 4096 → 1024,
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
| **1024 (chosen)** | **9.9%** | **74-79%** | **94-96%** | **0.01-0.05%** |
| 512 | 9.2% | 35-44% | 42-50% | 0.00% |
| *Granular, reference* | *15.1%* | *22-24%* | *28-30%* | *0.00-0.01%* |

The over-deadline column is the one that matters. At 4096 and 2048 it is
*exactly* 1/32 and 1/16 in every run — structural, one block per hop, not
noise. At 1024 it collapses into the same range as Granular, which is known
to work on hardware. Note also that **2048 is worse than 4096** by that
measure: halving the transform did not bring the burst under the deadline, it
only doubled how often the deadline was missed. The obvious "one step down"
would have made things worse and looked like progress on the mean.

1024 rather than 512 because it is the largest size where the structural
overrun disappears, and the size sets what Spectral *is*, so the change
should be the smallest one that works. 512 remains a one-line fallback with
about 2x more margin if hardware still crackles — see
`eurorack-opt/clouds/dsp/pvoc/stft.h`.

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
of this port to date. At 1024 the allocation reaches the `kMaxNumTextures = 7`
cap, so POSITION scans six magnitude textures as upstream intends. This was
found while checking that the smaller FFT did not break the allocator, not by
noticing it in use — which is a fair illustration of the point made at the
top of `test_clouds_fft.cc`: in a mode whose job is to smear and randomise, a
dead control does not announce itself.

### Still unconfirmed

Everything above is QEMU. The prediction is specific and falsifiable: at 1024
Spectral's per-block cost distribution is statistically indistinguishable from
Granular's, and Granular does not crackle or hang on hardware. If Spectral
still misbehaves, the model is wrong somewhere and the worker thread is the
next thing to try.

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
   correlator, not the FFT. **Done, at `kMaxFftSize` 1024, after hardware
   showed 4096 hanging the instrument.**

Granular and Delay remain the modes with no burst at all. Stretch still has
one, and nothing on this branch removed it: it is smaller than Spectral's was
(worst block ~29% against ~400%) and has not been reported as a problem, but
it is the same structural mistake and the same worker thread would fix it.

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
