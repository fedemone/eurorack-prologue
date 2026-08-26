eurorack-opt — forked Mutable Instruments sources
=================================================

Forked copies of a few files from the `eurorack/` submodule, used by the
`clouds`, `clouds_fx` and `rings` drumlogue units.

`eurorack/` is a submodule this repository does not edit, so the only way to
change engine code is to fork the file here and take it out of the submodule's
side of the build. That is a real cost — upstream fixes will not reach these
copies on their own — so the bar for adding a file is that the change is worth
more than the maintenance, and that is recorded below with the measurement
behind it.

All files keep Mutable Instruments' MIT licence and Olivier Gillet's copyright
notice, with a block underneath saying what was changed. Forked at submodule
commit **58b9125**.

| File | Change | Affects |
|------|--------|---------|
| `clouds/dsp/granular_processor.{h,cc}` | reverb + diffuser early-out | every mode |
| `clouds/dsp/grain.h` | **grain envelope no longer reads past `lut_window`** | Granular, high quality |
| `clouds/dsp/correlator.cc` | **no shift by 32 when a candidate lands on a word boundary** | Stretch |
| `clouds/dsp/pvoc/frame_transformation.cc` | **spectral warp skipped when its polynomial is the identity** | Spectral |
| `clouds/dsp/pvoc/stft.h` | LUT twiddle factors, **smaller FFT** | Spectral |
| `clouds/dsp/pvoc/stft.cc` | **`Buffer()` can take the `Parameters` to use** | Spectral |
| `clouds/dsp/pvoc/phase_vocoder.{h,cc}` | **one channel per call, spread across the hop**; **`CLOUDS_PVOC_HOP_RATIO` 2**; **transform moved to a worker thread** | Spectral, stereo |
| `clouds/dsp/wsola_sample_player.h` | **`LoadCorrelator()` split across two blocks**, including at POSITION 0 | Stretch |
| `stmlib/fft/shy_fft.h` | NEON butterfly | Spectral |
| `rings/dsp/part.cc`, `rings/dsp/performance_state.h` | **three chords added**, table sized by `kNumChords` | Rings' Chord parameter |

Why forked, and what changed
----------------------------

### `clouds/dsp/granular_processor.{h,cc}` — reverb and diffuser early-out

Both effects are exactly `out += amount * (wet - out)`, so at `amount == 0`
their output is bit-identical to their input. Both ran unconditionally, every
block, whatever the knobs said. Profiled with `gprof` on the port:

| Effect | Share of a block | When the amount is zero |
|--------|-----------------:|-------------------------|
| `Reverb::Process` (Granular) | 20.2 % | REVERB knob at 0, no feedback, not frozen |
| `Reverb::Process` (Stretch) | 21.2 % | same |
| `Diffuser::Process` (Granular) | 6.6 % | TEXTURE at or below 75 % |

Those are the default settings of both units, so this is the common case, not
a corner. Skipping is not simply "don't call it", though — the delay lines
freeze, and content stale by however long the skip lasted would be released
the moment the amount comes back up. A preset load can take REVERB from 0 to
full in a single block, so that matters. Each effect handles it differently:

- **Reverb** can be flushed, because it has an input gain. Before idling it
  runs for 5120 samples with the input muted and both recirculating gains
  (`reverb_time`, `diffusion`) taken to zero. Measured on the real `Reverb`,
  that empties every delay line to below -100 dBFS in 143 blocks of 32
  samples, independent of the reverb time that was in force; 5120 leaves
  margin over the longest line (4782). Audible output is unchanged during the
  flush, because the amount is already zero.
- **Diffuser** has no input gain to mute, so its all-pass states simply
  freeze. Instead its amount is ramped in over 8192 samples on resume. The
  chain (126 + 180 + 269 + 444 taps at kap = 0.625) decays to inaudibility in
  about 0.27 s, so whatever was frozen has gone before the amount is large
  enough to hear. **This is the only place where output deviates from
  upstream**, and only for a quarter second after the diffuser resumes.

When the amount is non-zero, both paths are byte-for-byte the original code.

### `clouds/dsp/grain.h` — grain envelope endpoint

`Grain::RenderEnvelope()` folds the envelope phase to a gain in `[0, 1]` and
hands it to `Interpolate(lut_window, gain, 4096.0f)`. `lut_window` holds 4097
entries and `Interpolate` reads both `table[i]` and `table[i + 1]`, so a grain
whose envelope lands exactly on gain 1.0 reads `lut_window[4097]` — one past
the end. Any grain width that divides 2.0 into an exact binary fraction gets
there, which is most of them.

It is arithmetically harmless. An integral index of exactly 4096 means a
fractional part of zero, so the stray element is multiplied by zero, and the
table is followed by more `.rodata`, so the read lands in the next table
rather than faulting. The reason to fix it anyway is the drumlogue's loader:
every unit in `Units/` is `dlopen`'d into one address space, so "reads a
neighbouring object" is a property of today's link order, not a guarantee.

The fix folds the endpoint back inside the table — index 4095 with a
fractional part of 1.0, which interpolates to `lut_window[4096]` exactly, the
same value upstream computes from one element earlier. `make
test-clouds-grain-window` checks both halves of that: 9216 envelope samples
across eight widths and two window shapes, fork against stock, must compare
byte-identical, and the fork build must then come back clean under
AddressSanitizer. Building the same test against the submodule header is what
reports the overrun, so the test has a live canary rather than an assertion
about the past.

Rings had three overruns of the same shape — `lut_stiffness`, `lut_4_decades`
and `lut_fm_frequency_quantizer`, all reachable at Structure or Damping 100 %.
Those are fixed in the port rather than the engine, so no fork was needed; see
`kLutSafeMax` in `rings-resonator.cc`.

### `clouds/dsp/correlator.cc` — the word-straddling shift

The correlator packs sign bits into 32-bit words and scores a splice candidate
by counting matching bits, reassembling each destination word from two:

```c
destination_bits  = destination[i]     << offset_bits;
destination_bits |= destination[i + 1] >> (32 - offset_bits);
```

`offset_bits` is `candidate_ & 0x1f`, so it is zero for every candidate that
falls on a word boundary — about one in 32, reached on ordinary settings. The
second shift is then `>> 32`, which C++ leaves undefined, and the two targets
this repository builds for disagree about it in the worst possible way: ARM's
variable shift produces 0, which is what the algebra wants, while x86 masks the
count to five bits, shifts by zero, and returns the whole word — corrupting the
score for that candidate.

**This changes nothing on hardware.** The device was already getting ARM's
answer. It is forked because of where the evidence about Clouds comes from:
the WSOLA split was settled by a 120-point differential sweep, Stretch is the
mode that sweep exercises, and it ran on the host. A host that scores splice
candidates differently from the device is not standing in for it. Making the
zero case explicit gives both targets the same answer and takes the undefined
behaviour out of the loop, so the sweep means what it says.

Found by `make test-asan` — UndefinedBehaviorSanitizer, on the ordinary
parameter sweeps, no new scenario required.

### `clouds/dsp/wsola_sample_player.h` — the correlator load, split across two blocks

Stretch schedules a WSOLA window and then packs sign bits for a
`window_size_` source window and a `2 * window_size_` destination window —
around 6144 interpolated buffer reads at the default size, doubled in stereo.
Upstream does all of it in whichever block follows, and can afford to, because
it runs from the idle loop. This port drives `Prepare()` from the audio
thread, so it lands as one spike per window, and with Spectral's transform now
on a worker it is the largest burst left in the engine.

The fork does the source window on one call and the destination window on the
next, which leaves the worst block paying about two thirds of what it did. The
split only engages above `kCorrelatorSplitWindow` (1024): the burst scales with
`window_size_` and so does the gap between windows, so at small sizes there is
both nothing worth splitting and no room to split into.

**The head-margin guard was removed after hardware reported Stretch
clicking.** It refused the split whenever the destination window's top edge
was within two blocks of the write head — which at POSITION 0 it always is,
because that edge *is* the head. The reasoning was sound and the cost was
never measured: POSITION 0 is not an edge case, it is the setting that
stretches the most recent audio, which is what the mode is for. Measured with
`make bench-clouds-stretch`, ARM under QEMU, per 32-sample block:

| | p99 | p99.9 | max | over deadline |
|---|---|---|---|---|
| SIZE 0.80, POSITION 0.00, guard on | 63.81% | 84.45% | 109.85% | **0.05%** |
| SIZE 0.80, POSITION 0.00, guard off | 49.78% | 55.19% | 102.38% | 0.00% |
| SIZE 0.80, POSITION 0.25 (reference) | 50.48% | 62.02% | 89.41% | 0.00% |

with the same gap at SIZE 0.65 and 1.00. POSITION 0 was the only setting in
the sweep that missed the deadline, and with the guard off it now tracks the
rest of the knob. A missed deadline is a dropout; what the guard prevented is
a splice landing a few samples further along in a signal that is being spliced
anyway.

What the guard bought is now measurable, and it is nothing that shows.
`make test-clouds-wsola-split` sweeps 200 points of SIZE × POSITION × PITCH ×
quality against a build that never splits. With the guard off the split
engages 42110 times against 33703 with it on — 8407 more, all at POSITION 0 —
and every one of the 200 points is still bit-identical.

That test is also the answer to a gap in this file. The numbers the notes in
`wsola_sample_player.h` quote for the stage order — "12 of 90 points" against
"3 of 90", and "18 of 90" for a rejected guard — came from a sweep run once by
hand and never committed, so nothing here could be re-checked. It is committed
now, and it reports how many times the split actually engaged, because a
differential that comes out identical because the split never ran would be no
evidence at all.

One caveat worth carrying: a *shorter* settle in that test does find
differences — three of 200 points at PITCH +24, none larger than 0.25% rms.
They are real, and they are transient-state. `window_size_` is not set from
SIZE but slewed toward it, by `(target - current) >> 5` and only inside
`ScheduleAlignedWindow()`, so at large SIZE it converges over tens of
thousands of blocks. The guard was protecting a condition that exists on the
way to a setting rather than at it. Build with
`-DCLOUDS_WSOLA_HEAD_MARGIN="(2 * kMaxBlockSize)"` to restore it.

### `clouds/dsp/pvoc/frame_transformation.cc` — the identity warp

The spectral warp is a cubic in the bin index, crossfaded between rows of
`kWarpPolynomials` by the SIZE knob. Row 2 is `{ 0, 0, 1, 0 }` — the identity —
and SIZE at 50 %, the knob's default and its centre detent, lands on it
exactly: size 0.5 makes warp 0.5, times four is 2.0 with no fractional part, so
the crossfade returns row 2 untouched. Every bin is then read back from where
it already was, through a per-bin `Interpolate`, for nothing.

`gprof` puts `WarpMagnitudes` at 18.6 % of `FrameTransformation::Process`, the
second largest step in it. End to end with `make bench-clouds-spike`, three
runs a side:

| | mean | p99 |
|---|---:|---:|
| Spectral, before | 6.92 % | 32.6 % |
| Spectral, after | 6.43 % | 28.9 % |

with the Granular control row flat across all six runs. About 7 % of
Spectral's cost, at the setting the unit loads in.

**It is not bit-identical**, and the reason is instructive. `size_` is
`(fft_size / 2) - kHighFrequencyTruncation` — 240, not a power of two — so
`bin_width` is 1/240, inexact in binary, and the accumulator drifts. Upstream's
identity warp is really a slightly smeared identity; the copy is the exact one.
The change makes the warp *more* correct, and different.

`make test-clouds-warp` measures the difference rather than asserting it away,
rendering Spectral both ways with SIZE swept across its range: **283 of 80640
samples differ, none by more than 2 LSB, and the error sits 111.6 dB below
signal peak where one int16 LSB is 87.2 dB down** — two dozen dB under the
quantiser the output passes through either way. Same standard as the FFT
change.

Both sides of that comparison are fork builds, with `CLOUDS_WARP_IDENTITY_SKIP`
switched off on one. Comparing against the submodule would prove nothing:
Spectral there runs a 4096-point transform at a different hop.

### `stmlib/fft/shy_fft.h` — NEON butterfly

The loop that dominates both transforms, vectorised four butterflies at a
time, selected by a new `kUseNeon` template parameter on `ShyFFT`. Upstream's
scalar code is untouched and is still what builds off ARM and what
`kUseNeon = false` selects, so the differential test has a reference that
cannot drift away from the original.

Vectorising the existing passes rather than writing a radix-4 kernel from
scratch was deliberate. The risk in replacing this FFT was never accuracy —
its round-trip error is 104.5 dB below signal peak against an int16 quantiser
at 87.1 dB on either side of it — it was the interface: a split real/imag
spectrum with a particular sign convention, unnormalised in both directions
with the factor of N made up in `stft.cc`, and an input buffer that may be
destroyed and aliases the output. Get any of those wrong and Spectral mode,
whose job is to smear and randomise, still sounds like it is working. Working
inside the existing passes keeps all of it right by construction; only the
arithmetic changes.

**Measured on the generated code**, `-O2`, `-mfpu=neon-vfpv4`, from the
disassembled inner loop:

| | instructions per butterfly |
|---|---:|
| upstream scalar | 33 |
| NEON, four at a time | 37/4 = **9.25** |

3.6x fewer instructions issued. Writing the loop with running pointers rather
than `base + j` was worth 47 instructions per iteration down to 37 on its own:
with the index form gcc recomputed eight addresses every pass.

**This is an instruction count, not a speedup**, and the distinction matters
more here than usual. Cortex-A7's NEON datapath is 64 bits wide, so a
q-register operation occupies it for two beats — the cycle-level gain will be
smaller than 3.6x, by how much cannot be determined without the hardware. QEMU
cannot answer it either, in either direction: it emulates NEON through helper
calls that cost *more* than the scalar equivalents, so an isolated
Direct+Inverse benchmark there shows the vectorised version at 0 to 2 % either
side of scalar, which is an artefact of the emulator and says nothing about
the SoC. What can be said with confidence is that the work issued went down by
a factor of 3.6 and the output is unchanged to 135 dB.

Correctness is covered by `make test-clouds-fft`, which runs on the host (both
paths scalar there, so it only proves the refactor left upstream alone) and
again on ARM under QEMU, which is the run that exercises the vector code. It
pins the interface contract against hand-computed expectations rather than
against the other implementation, so those still hold if someone later swaps
in a different FFT entirely.

### `clouds/dsp/pvoc/stft.h` — smaller FFT (`kMaxFftSize` 4096 → 512)

This is the one change here that alters what a mode sounds like, and it was
made because upstream's 4096 hung the hardware.

Upstream computes the whole transform in one `STFT::Buffer()` call from the
idle loop, where it is not deadline work. This port has no idle loop and runs
it on the audio thread, so it arrives as a single burst once per hop —
forward FFT, spectral modifier, inverse FFT, twice over in stereo, inside one
audio block. Peak cost goes as `(N/2)·log2(N)` while the hop only goes as
`N`, so a smaller transform cuts the burst by more than it raises the burst
rate, and the average barely moves.

Measured with `make bench-clouds-spike` (ARM under `qemu-arm`, per 32-sample
engine block at 32 kHz = a 1.00 ms deadline, two runs per size). Granular is
there as a reference: it has no burst mechanism at all, so it calibrates the
harness and the host.

| `kMaxFftSize` | mean | p99 | p99.9 | over deadline | window @ 32 kHz |
|---|---:|---:|---:|---:|---|
| 4096 (upstream) | 11.1% | 298% | 380-463% | **3.12%** | 128 ms, 7.8 Hz bins |
| 2048 | 10.3% | 147% | 168-178% | **6.25%** | 64 ms, 15.6 Hz |
| 1024 | 9.9% | 74-79% | 94-96% | 0.01-0.05% | 32 ms, 31.2 Hz |
| **512 (default)** | **9.2%** | **35-44%** | **42-50%** | **0.00%** | 16 ms, 62.5 Hz |
| *Granular, for reference* | *15.1%* | *22-24%* | *28-30%* | *0.00-0.01%* | — |

Two things to read out of that. The over-deadline column at 4096 and 2048 is
*exactly* 1/32 and 1/16 in every run — that is structural, one block per hop,
not noise. And 2048 is **worse** than 4096 by that measure: halving the
transform did not bring the burst under the deadline, it just doubled how
often it missed. The obvious "one step down" would have looked like progress
on the mean while making the actual failure twice as frequent.

The table above is engine-level. The size was chosen on the end-to-end
numbers, per whole render callback, because that is the deadline that exists
— and the two units do not agree:

| | Spectral p99.9 | over deadline |
|---|---:|---:|
| `clouds` synth @ 1024 | 79% | 0.05% |
| `clouds` synth @ 512 | 60% | 0.00% |
| `clouds_fx` @ 1024 | 126% | **0.93%** |
| `clouds_fx` @ 512 | 75-82% | 0.00% |

**1024 is enough for the synth and not for the FX.** CloudsFX runs stereo
resampling in both directions, dry path included, so less of its 64-frame
buffer is left over for the burst; at 1024 it still missed 0.93% of renders,
which at ~750 renders a second is about seven dropouts a second. 512 clears
both, and puts the FX's Spectral tail alongside its other modes (Granular
48-51%, Stretch 51-54%) instead of above them.

A synth-only build can have the longer window back — one line, and nothing
else depends on it:

```c
// eurorack-opt/clouds/dsp/pvoc/stft.h
#define CLOUDS_FFT_SIZE 1024
```

Set it in the header, not with `-D` — the value has to reach every
translation unit or `sizeof(GranularProcessor)` disagrees between objects,
and the `CLOUDS_OPT_ACTIVE` guard cannot catch that because both sides are
this same header.

Lowering `kMaxFftSize` is deliberate, rather than the more obvious route of
passing a smaller `largest_fft_size` to `PhaseVocoder::Init()`. `STFT::Buffer()`
switches to `fft_->Direct(in, out, num_passes)` the moment
`fft_size != FFT::max_size` — a runtime-sized path that has never executed in
this port and that the NEON butterfly does not cover, so that route would
activate untested code and throw away the vectorisation in the same move.
Keeping the two equal is why `granular_processor.cc` also had its literal
`4096` replaced with `kMaxFftSize`; the two must not drift apart.

**It also fixes POSITION in Spectral mode, by accident.** `num_textures` is
`min(free buffer space / texture_size, kMaxNumTextures)` and
`FrameTransformation` reserves one of them for phases, so at 4096 the port
got 2 textures — one usable magnitude buffer. `ReplayMagnitudes` computes
`index = position * (num_textures_ - 1)`, which with one texture is always
zero: **the POSITION knob did nothing at all in Spectral**. At 512 the
allocation reaches the `kMaxNumTextures = 7` cap, so POSITION scans six
magnitude textures, as upstream intends.

Sonically, 512 is a 16 ms window with 62.5 Hz bins against 4096's 128 ms and
7.8 Hz: much tighter, far less smeared, more transient detail, and very
little of the long frozen-pad character the mode used to have. Different, not
worse — but different, and worth knowing before wondering why a patch does
not sound like it used to. This is the real price of the fix, and it is paid
in Spectral only; the other three modes are untouched.

### `clouds/dsp/pvoc/phase_vocoder.{h,cc}` — one channel per call, spread across the hop

The FFT-size fix above attacks the size of the once-per-hop burst. This one
attacks the fact that it is *one* burst: upstream's `Buffer()` loops over the
channels, so in stereo a forward FFT, the spectral modifier and an inverse
run **twice, back to back, in a single audio block**. Upstream can afford
that because it calls `Buffer()` from the main idle loop, where it is not
deadline work. This port has no idle loop and calls it from the audio thread.

Serving one channel per call halves the spike. Where the two calls land
matters more than it looks:

| CloudsFX, Spectral, p99.9 per render | defaults | reverb 60 % + texture 90 % |
|---|---:|---:|
| upstream scheduling | 95–111 % (0.05–0.53 % over) | 101–111 % (0.10–0.53 % over) |
| one channel per call, adjacent blocks | 85–100 % (0.05–0.08 % over) | 107–121 % (0.62–0.97 % over) |
| one channel per call, spread across the hop | **63 %** (0.00 % over) | **69 %** (0.00 % over) |

Adjacent blocks barely helped, and the reason is a mismatch of granularity.
The deadline that exists is one 64-frame host render; the engine runs at
32 kHz, so a render covers 1.33 audio blocks. Two transforms one block apart
still share a render about a third of the time. Half a hop apart they never
do — and a hop is 128 samples at `kMaxFftSize` 512, so there is room to
spare. `Init()` computes the spacing as `hop_blocks / num_channels`.

On the synth the same change takes Spectral's p99.9 from 84 % of deadline to
44 %; at the engine level, from ~71 % to 36 %, which puts it below Stretch
(38 %) rather than at twice everything else.

**What this does not buy is a longer window.** 1024 with the split still
misses 1.12 % of renders on the FX, so `CLOUDS_FFT_SIZE` stays at 512 and the
sonic cost above stands. The split is margin, not headroom to spend: roughly
a factor of two between Spectral and its deadline on both units, on a mode
whose burst was hanging the hardware three commits ago. (The synth alone
clears 1024 comfortably — p99.9 90 %, no misses — which is what makes the
synth-only override above worth keeping.)

Steady-state output is **bit-identical** to upstream's scheduling, not merely
close, and `make test-clouds-pvoc-rr` pins that: the same rendering compiled
with `CLOUDS_PVOC_ROUND_ROBIN` 1 and 0, compared sample for sample at every
FFT size from 256 to 4096, stereo and mono, hi-fi and lo-fi. The deferral is
exact because `STFT`'s analysis ring is `fft_size + hop_size` long while
`Buffer()` reads `fft_size` of it — exactly one hop of slack between the
write pointer and the window being transformed, of which spreading uses half.

Two details are worth knowing before touching this code:

- **The channel order is load-bearing.** `FrameTransformation` writes only the
  lowest `(fft_size / 2) - kHighFrequencyTruncation` bins of `ifft_in`, and
  `ifft_in` is scratch *shared by both channels*, so every transform inherits
  its top 16 bins from whichever transform ran last. That is upstream's
  behaviour, not something introduced here, but it means a rotation that comes
  up in the opposite phase feeds each channel the other one's residue and
  changes the output. The turn therefore advances only when a transform
  actually runs — an earlier version advanced on idle calls too, and the
  output then depended on whether Spectral was entered by `Init()` or by a
  mode change, which is how this was found.
- **Transitions can land in the gap.** FREEZE engaging, or a mode change
  reallocating the workspace, can arrive between the two channels' transforms
  and leave them one frame apart in what they captured. At 512 the gap is two
  blocks and the test measures no difference at all; it is visible only at
  2048 and 4096, where the gap is 8 or 16 blocks, and then only as a fraction
  of a percent of level.

#### `CLOUDS_PVOC_HOP_RATIO` — the overlap, and the one lever worth a factor

Hardware confirmed the smaller FFT removed the freeze and also said Spectral
is still too heavy to be comfortable. Everything that could be shaved off it
after that — skipping the identity warp polynomial, flattening the window LUT,
vectorising the windowed I/O — measured between 1% and 5% each. The overlap is
the only constant in the engine that changes the answer by a factor, because
it changes how *often* a transform runs rather than how much one costs.
`make bench-clouds-spike`, ARM under QEMU, per 32-sample engine block:

| | Spectral mean | of which `Prepare()` | Spectral p99.9 |
|---|---:|---:|---:|
| `hop_ratio` 4 (upstream, default) | 16.15% | 13.33% | 55.45% |
| `hop_ratio` 2 | **9.08%** | **6.45%** | 55.99% |

-44% overall, -52% on the phase vocoder. The peak does not move, and should
not: it is the same transform, half as often. The other three modes never
touch the phase vocoder and do not move either.

Reconstruction is unaffected, and that is measured rather than argued.
The window is applied at analysis *and* synthesis, so the effective window is
sine² = Hann, and Hann sums to exactly 1 at 50% overlap as it sums to 2 at
75%; `inverse_window_size` in `stft.cc` is already derived from `hop_size_`,
so the normalisation follows on its own. `make test-clouds-cola` drives the
real STFT with the modifier disabled and measures the ripple of a steady tone:
**0.62 dB at ratio 4 and 0.62 dB at ratio 2**, identical to two decimal
places, against **9.48 dB at ratio 1** — the last being the control, without
which a test that reported no ripple everywhere would look like a pass rather
than a broken measurement.

**The default is 2, not upstream's 4.** It was 4 here for exactly the reason
below, and hardware overruled it: Spectral still clicked with the smaller FFT
in place, and this is the only lever left that changes the cost by a factor.

What halving the overlap costs is not reconstruction but phase-vocoder
artifacts on *modified* spectra — fewer overlapping frames means less
averaging in the phase reconstruction, so transients smear differently and
heavy Warp/Quantize/Pitch settings get grainier. That is most of what Spectral
is for, and Spectral's character had already been cut once by the 4096 → 512
change. The judgement made here is that a mode which clicks is worth less than
a mode which is grainier. Build with `-DCLOUDS_PVOC_HOP_RATIO=4` to get
upstream's overlap back, at upstream's cost.

#### `CLOUDS_PVOC_WORKER` — the transform on a worker thread

The other three levers on Spectral made the work smaller. This one moves work
that is already small enough off the deadline, and it is the last of any size.

What overran was never the total. Spectral's mean is a few percent of a block.
It is that the whole transform lands inside *one* audio block, once per hop,
and that block has to fit in a render alongside everything else. Upstream does
not have this problem because it calls `Buffer()` from its main idle loop,
where a spike costs nothing; the port put it on the audio thread because it
had no idle loop. A thread at `SCHED_OTHER` is an idle loop.

Enabled automatically on `__linux__`, which is the drumlogue and nothing else —
prologue, minilogue-xd and NTS-1 are bare-metal Cortex-M4 with no pthreads and
no scheduler to exploit. It is gated on a predefined macro rather than a `-D`
on purpose: it changes `sizeof(PhaseVocoder)`, so it has to be identical in
every translation unit, and that is the footgun `stft.h` warns about for
`CLOUDS_FFT_SIZE`.

The safety argument is the slack the round-robin already spends. A transform
may run any time within one hop of being scheduled and still read and write
exactly what it would have — `make test-clouds-pvoc-defer` defers every
transform by N calls, on the audio thread so the result is deterministic, and
the fixed-parameter hashes do not move at any N it sweeps. Three properties
carry it, and each is checked rather than argued:

* **The audio thread never blocks on the worker, and never takes a lock to
  reach it.** It posts with a release store and a `sem_post`; if the slot is
  busy it returns and tries again next call. There is deliberately no path
  where the render waits on a `SCHED_OTHER` thread — on a single core that is
  a livelock, the renderer burning the budget the worker needs in order to
  release it. `Quiesce()` is the one exception and it is a mode change, not
  deadline work.
* **A transform runs on exactly one thread at a time.** Both channels share
  the FFT scratch, so this is not a formality. `make test-tsan` runs the
  handoff under ThreadSanitizer and reports nothing. It also runs CloudsFX's
  park-and-reconfigure against a live worker, which is the one place a
  *third* thread reaches the job slot: the control thread parks the renderer
  and then calls `Init()`, and `Quiesce()` is what stops it reallocating
  under a transform it never posted.
* **The worst case is the previous behaviour.** If the thread cannot be
  created every transform runs inline, exactly as before. If the worker falls
  behind, the catch-up valve takes back *one* transform — capped, because an
  earlier version drained the whole backlog in one call and that was measurably
  worse than having no worker at all.

Measured with `make bench-clouds-spike`, ARM under QEMU, Spectral, per
32-sample block against a 1 ms deadline. The worker is starved in this bench —
it drives blocks flat out, so there is no wall clock for a lower-priority
thread to run in — which makes these the *pessimistic* numbers, the fallback
path rather than the intended one:

| | mean | p99 | p99.9 | max | over deadline |
|---|---|---|---|---|---|
| worker off | 7.50% | 34.02% | 40.02% | 41.35% | 0.00% |
| worker on, valve capped | 5.45% | 22.74% | 29.06% | 39.72% | 0.00% |
| worker on, uncapped | 5.49% | 60.56% | 80.56% | **540.22%** | 0.04% |

The third row is the version that drained the backlog in one call. It is kept
because it is the reason the cap exists, and because it is the shape a
plausible-looking design had before it was measured.

Given real time the picture changes again. `make test-clouds-pvoc-worker`
drives the engine at the sample rate: the worker takes **every** transform,
none forced back, and the output is bit-identical to the same build with the
worker switched off. That is what this change stands on, and QEMU cannot show
it, because what it depends on is the scheduler.

Two members that look like they could be plain are not, and both were caught
by ThreadSanitizer rather than by reading the code. `worker_stop_` is read by
the worker on every loop iteration: the obvious argument is that `StopWorker()`
writes it and then `sem_post()`s, and sem_post/sem_wait synchronise — but the
semaphore *counts*, so the worker can be working through wakeups
`BufferWorker()` posted earlier, and those iterations read the flag ordered
against their own post and nothing else. The scheduling counters
(`g_pvoc_worker_ran`, `g_pvoc_worker_forced`) are incremented from both
threads by construction. Both are now relaxed atomics: the effect of a race on
either was always harmless — one extra turn round the loop, a misreported
total — but harmless is not the same as defined, and a suppression in the
target that exists to find worker races is not a trade worth making.

Build with `-DCLOUDS_PVOC_WORKER=0` to put the transform back on the audio
thread. That is the first thing to try if Spectral misbehaves on hardware, and
it restores exactly the code that shipped before this existed.

### `clouds/dsp/pvoc/stft.cc` — `Buffer()` can take its `Parameters`

Upstream stores a `const Parameters*` in `Process()` and dereferences it in
`Buffer()`, so a transform reads the knobs as they are at the moment it runs.
Fine when both are on one thread; a plain data race once the transform moves to
a worker, because the audio thread rewrites that struct at the top of every
block.

Taking the `Parameters` by value at the point the transform is *scheduled*
removes the race and, less obviously, removes a difference that was already
there. The old round-robin's one non-bit-identical effect was inter-channel
skew from exactly this live read; snapshotting means a deferred or offloaded
transform uses precisely the values the synchronous path would have used, so
`test_clouds_pvoc_rr.cc`'s swept scenarios go from a small non-zero difference
to 0.000%.

The fork also adds `BufferReady()`, which skips re-checking that a hop is
pending. That check reads `ready_`, which the audio thread writes — the last
genuine race in the handoff, and unnecessary, since the scheduler established
`pending() > 0` before handing the job over. Not reading it removes the race
rather than excusing it. The no-argument `Buffer()` is untouched, so a build
with the worker and the deferral both off behaves exactly as before.

### `clouds/dsp/pvoc/stft.h` — LUT twiddle factors

One line: the FFT's twiddle generator, `stmlib::RotationPhasor` →
`stmlib::LutPhasor`. Both are stmlib, both produce the same sequence
`cos(k·π/2^pass)`. `RotationPhasor` advances by complex multiplication — four
multiplies and two adds per butterfly group — while `LutPhasor` walks a table
built once in `Init()`. It is also the more accurate of the two, since
repeated rotation drifts.

Costs 8176 bytes of BSS for the table (`num_passes` = 12 at `kMaxFftSize`
4096), against the 184 KB of sample buffers each unit already reserves.
Measured under `qemu-arm` with Granular mode as a control for run-to-run
noise, the Spectral/Granular cost ratio went 1.24–1.33 → 1.13–1.16 across
three paired runs: roughly half of the FFT's excess cost.

### `rings/dsp/part.cc`, `rings/dsp/performance_state.h` — three more chords

The only fork here that adds a feature rather than saving time. Rings' Chord
parameter selects a row of string tunings; upstream ships eleven, and three
more are appended: `4ths` (stacked perfect fourths), `Just7` (a just-intoned
dominant seventh, harmonics 4:5:6:7) and `Slendro` (the gamelan pentatonic,
five equal steps to the octave). Two of the three are microtonal, which is the
reason for picking them: on a resonator each string is tuned and rings
independently, so a septimal seventh audibly stops beating against the root
where a tempered one does not. `kNumChords` goes 11 → 14 in the header, and
`rings-resonator.cc` carries the matching arpeggiator intervals.

There is a second reason this had to be a fork rather than a header tweak.
Upstream states the table's length twice — once as `kNumChords` in
`performance_state.h`, once as a literal `11` in the table's own dimension in
`part.cc` — and nothing ties the two together. `performance_state.chord`
reaches the subscript from the panel with no bounds check in between, so
raising the parameter's range while the dimension stayed at 11 would index
past the row into the next polyphony block, on the audio thread. The fork
dimensions both tables with `kNumChords`, which is what makes them move
together.

One file keys off the constant and is deliberately *not* forked:
`rings/dsp/string_synth_part.cc` has a parallel table of its own, so its three
new rows initialise to zero — a unison on the root, not garbage. Nothing in
this port instantiates `StringSynthPart` (it is Rings' polyphonic string-synth
easter egg), so that is dead data; if anything ever reaches it, those rows are
the thing to fill in.

Verified by `make test-arm`, which strums each of the fourteen chords on the
shipped binary and measures the result with a Goertzel per probe interval. The
checks are within-recording comparisons of adjacent bins — `Just7` must ring
9.69 semitones rather than 10.0, `min7` and `4ths` the other way round,
`Slendro` 2.4 rather than 2.0 — because comparing whole spectra between chords
does not work on this engine: Rings' strings are not sinusoids, every one puts
a second harmonic an octave up, and a bare fifth came out closer to a major
ninth than one chord did to a second recording of itself. Two bins a third of
a semitone apart in the same recording share whatever overtones land there, so
what is left between them is the fundamental. Margins are 60× to 11000×.

Build wiring — read this before touching it
-------------------------------------------

Most of these files are **headers**, and several change `sizeof` of types the
port layer instantiates (`GranularProcessor` directly; `FFT`, hence
`PhaseVocoder`, hence `GranularProcessor` again). A build where some
translation units see these headers and others see the submodule's links
without complaint and then corrupts memory at run time. The Rings pair has the
same shape for a different reason: a `part.cc` compiled against the
submodule's `kNumChords` would index a fourteen-row parameter into an
eleven-row table.

So the rule is all-or-nothing, and it is enforced:

1. `eurorack-opt/` must come **before** `eurorack/` on the include path.
2. `eurorack-opt/clouds/dsp/granular_processor.cc`,
   `eurorack-opt/clouds/dsp/correlator.cc` and `eurorack-opt/rings/dsp/part.cc`
   replace the submodule's in the source list — the submodule's must not also
   be compiled, and so does
   `eurorack-opt/clouds/dsp/pvoc/frame_transformation.cc`. (The rest are
   headers, so they need no source-list change, only the include order.) The
   Clouds sources are listed in five places: `Makefile` (`CLOUDS_FX_ENGINE`),
   `osc_clouds.mk`, both `drumlogue/clouds*/config.mk`, and
   `generate_sdk_projects.sh`.
3. The build defines `CLOUDS_OPT_ENGINE`. The Clouds headers define
   `CLOUDS_OPT_ACTIVE`. `clouds-granular.cc` and `clouds-fx.cc` `#error` if the
   first is set without the second, so a half-configured build fails at compile
   time instead of at run time.
4. Rings has no `#error` guard, and it turns out to need one more than the
   note here used to admit. `part.cc` is the only file that reads the chord
   table and it is the file that is forked, so a build that lists the
   submodule's `part.cc` alongside the forked `performance_state.h` compiles
   and links, then reads past the end of the chord table at run time.

   That is not hypothetical: `generate_sdk_projects.sh` emitted
   `eurorack/rings/dsp/part.cc` for exactly this reason, so anyone
   regenerating the projects silently reverted the fork. It was caught by
   `make test-asan` — `index 12 out of bounds for type 'float [11][8]'` in the
   submodule's `part.cc` — which is also the answer to how it would be caught
   again. `make test-arm` walks the Chord parameter over its declared range
   and requires every value to name a chord, and would catch it too.

   **Both source lists have to name the fork**: `drumlogue/rings/config.mk`
   and `RINGS_SOURCES` in `generate_sdk_projects.sh`, which regenerates it.

Wired up in `Makefile` (`CLOUDS_OPT_FLAGS`, `CLOUDS_FX_ENGINE`),
`osc_clouds.mk`, `makefile.inc` (`DINCDIR`, which is why the entry goes
*before* the `eurorack` one rather than in `UINCDIR`) and
`generate_sdk_projects.sh` (both the generic template and the hand-written
`clouds_fx` config). Re-run `./generate_sdk_projects.sh` after changing that
script.

Re-syncing when the submodule moves
-----------------------------------

```
git -C eurorack log --oneline 58b9125..HEAD -- \
    clouds/dsp/granular_processor.h clouds/dsp/granular_processor.cc \
    clouds/dsp/pvoc/stft.h clouds/dsp/pvoc/phase_vocoder.h \
    clouds/dsp/pvoc/phase_vocoder.cc clouds/dsp/wsola_sample_player.h \
    clouds/dsp/grain.h clouds/dsp/correlator.cc \
    clouds/dsp/pvoc/frame_transformation.cc stmlib/fft/shy_fft.h \
    rings/dsp/part.cc rings/dsp/performance_state.h
```

If that is empty, nothing to do. If not, diff upstream against the fork and
re-apply the changes above — they are small and each is marked with a comment
explaining what it is for. Then re-run `make test-all` and `make test-arm`.

The one change with a behavioural signature to check afterwards is the
diffuser ramp: `make test-clouds-synth` covers all four modes, and
`docs/CLOUDS_DRUMLOGUE_AUDIO_NOTES.md` has the numbers to compare against.
`make test-clouds-fft`, `make test-clouds-engine-opt` and `make
test-clouds-grain-window` are the three that compare fork against original
directly, so run all of them.

For the Rings pair the re-sync is mechanical — the fork is the upstream file
with three rows appended to each of the two chord tables and the dimension
named rather than spelled `11`. If upstream ever changes those tables, the
rows to keep are the ones marked "Added for the drumlogue port".

Note that `shy_fft.h` is a *stmlib* file, not a Clouds one, so upstream churn
there could affect anything else that uses stmlib's FFT. Nothing else in this
repo does today.
