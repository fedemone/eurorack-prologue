eurorack-opt — forked Mutable Instruments sources
=================================================

Optimized copies of a few files from the `eurorack/` submodule, used by the
`clouds` and `clouds_fx` drumlogue units.

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
| `clouds/dsp/pvoc/stft.h` | LUT twiddle factors | Spectral |
| `stmlib/fft/shy_fft.h` | NEON butterfly | Spectral |

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

Build wiring — read this before touching it
-------------------------------------------

Three of the four files are **headers**, and two of them change `sizeof` of
types the port layer instantiates (`GranularProcessor` directly; `FFT`, hence
`PhaseVocoder`, hence `GranularProcessor` again). A build where some
translation units see these headers and others see the submodule's links
without complaint and then corrupts memory at run time.

So the rule is all-or-nothing, and it is enforced:

1. `eurorack-opt/` must come **before** `eurorack/` on the include path.
2. `eurorack-opt/clouds/dsp/granular_processor.cc` replaces the submodule's in
   the source list — the submodule's must not also be compiled. (`shy_fft.h`
   and `stft.h` are headers, so they need no source-list change, only the
   include order.)
3. The build defines `CLOUDS_OPT_ENGINE`. These headers define
   `CLOUDS_OPT_ACTIVE`. `clouds-granular.cc` and `clouds-fx.cc` `#error` if
   the first is set without the second, so a half-configured build fails at
   compile time instead of at run time.

Wired up in `Makefile` (`CLOUDS_OPT_FLAGS`, `CLOUDS_FX_ENGINE`),
`osc_clouds.mk`, `makefile.inc` (`DINCDIR`, which is why the entry goes
*before* the `eurorack` one rather than in `UINCDIR`) and
`generate_sdk_projects.sh` (both the generic template and the hand-written
`clouds_fx` config). Re-run `./generate_sdk_projects.sh` after changing that
script.

Re-syncing when the submodule moves
-----------------------------------

```
git -C eurorack log --oneline 58b9125..HEAD -- clouds/dsp/granular_processor.h \
    clouds/dsp/granular_processor.cc clouds/dsp/pvoc/stft.h stmlib/fft/shy_fft.h
```

If that is empty, nothing to do. If not, diff upstream against the fork and
re-apply the changes above — they are small and each is marked with a comment
explaining what it is for. Then re-run `make test-all` and `make test-arm`.

The one change with a behavioural signature to check afterwards is the
diffuser ramp: `make test-clouds-synth` covers all four modes, and
`docs/CLOUDS_DRUMLOGUE_AUDIO_NOTES.md` has the numbers to compare against.
`make test-clouds-fft` and `make test-clouds-engine-opt` are the two that
compare fork against original directly, so run both.

Note that `shy_fft.h` is a *stmlib* file, not a Clouds one, so upstream churn
there could affect anything else that uses stmlib's FFT. Nothing else in this
repo does today.
