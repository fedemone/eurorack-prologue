Clouds on drumlogue — audio-thread notes
========================================

Working notes from debugging audio dropouts ("audio interface crash") in the
`clouds` synth unit and the `clouds_fx` insert effect on real hardware.

Read this before changing anything in `clouds-granular.cc`, `clouds-fx.cc` or
the drumlogue wrappers. It records what was measured, what was fixed, what is
still broken, and — importantly — a few plausible-sounding theories that
turned out to be wrong.

**Status summary**

| Unit | State |
|------|-------|
| `clouds` (synth) | Working. Granular mode is within budget; Stretch and Spectral are not — see [Prepare() on the audio thread](#prepare-on-the-audio-thread). |
| `clouds_fx` (delfx) | **Work in progress — still crashes on hardware.** Do not treat as usable. |

---

Measurement method
------------------

Numbers below come from the real ARM engine build (`arm-linux-gnueabihf-g++`,
the SDK's own flags: `-march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4
-Os -ffast-math -ftree-vectorize`) running under `qemu-arm`, timing
`GranularProcessor::Prepare()` and `::Process()` separately per 32-frame
block and expressing each as a percentage of one block's wall-clock budget at
48 kHz (667 µs).

**These percentages are relative, not absolute.** QEMU is roughly an order of
magnitude slower than the real SoC, so a row reading "25%" does not mean the
unit uses a quarter of the drumlogue's CPU. Comparisons *between* rows are
meaningful; the absolute values are not. QEMU also does not model memory
bandwidth or the denormal penalty, so anything dominated by those is
understated here.

Mean and worst-case block are both reported, because dropouts are a
worst-case phenomenon: a mean comfortably under budget with a 20x spike still
drops audio.

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
Stretch and Spectral crackle.**

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
StereoHi:

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
   and a preemptive scheduler may not.
2. **Running the engine at its native 32 kHz** with sample-rate conversion at
   the boundary. Clouds was designed for 32 kHz and this port feeds it 48 kHz
   directly, so it does 1.5x the work per second of audio that Clouds
   hardware ever did. This would cut *every* mode by about a third and make
   the `sample_rate()`-derived constants (feedback high-pass cutoff, reverb
   LFO rates) correct. It changes pitch/time behaviour, so it is a deliberate
   sound change, not a transparent optimization.

Neither is done. Granular and Delay are unaffected and are the modes to use.

---

CloudsFX — work in progress
---------------------------

**`clouds_fx` still crashes on hardware and should be treated as unfinished.**

Reported behaviour: the first trigger produces correct sound, then the audio
interface goes silent.

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
but there the reconfiguration is a deliberate user gesture, and hardware now
reports Quality switching as fixed.

### Divergence from the synth

`clouds-fx.cc` was deliberately left untouched while the crash is open, so its
`density_to_clouds()` still uses the old linear-in-`density` mapping with its
cubic grain law. The two units' DENSITY knobs therefore behave differently
right now. Bring `clouds-fx.cc` in line with `clouds-granular.cc` once the
crash is resolved.

### Suggested direction (not implemented)

Do the reconfiguration on the **control thread**, where taking milliseconds is
fine, and keep the audio thread out of the engine while it happens, rather
than moving the work onto the audio thread. A Dekker-style handshake is
enough and needs no locks in the renderer:

- audio thread: store `in_engine = 1` (seq_cst), then load `request`; if set,
  clear `in_engine`, emit silence, return.
- control thread: store `request = 1` (seq_cst), then spin-with-sleep until
  `in_engine` reads 0, do the heavy `Init()`/`Prepare()`, clear `request`.

Under sequential consistency at least one side observes the other, so the
control thread never reallocates while the renderer is inside the engine, and
the renderer never blocks. Emitting silence for the few blocks a
reconfiguration takes matches what the engine already does — `Process()`
returns zeros while `reset_buffers_` or `silence_` is set.

A bounded wait is needed so the control thread cannot hang if audio is not
running; if `in_engine` is 0 because no render is in flight, the handshake
completes immediately.

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
- **Buffer over-allocation.** Workspace is 53,248 bytes (stereo) against
  42,520 used, with the pitch shifter aliasing to 49,152. It fits, and
  `BufferAllocator` bounds-checks and returns NULL anyway.
- **Grain pool overflow.** `kMaxNumGrains` is 64; the computed maximum is 57.
- **Parameter routing.** drumlogue id 11 → `OSC_PARAM` index 10 → Quality,
  verified; all indices explicitly handled with `default: break`.
- **Output clipping.** Output is scaled by `kOutGain` (~-3 dB) and passed
  through `SoftConvert`; levels were checked and are not the problem.
