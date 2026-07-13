# plaits_patches

Patched copies of three Mutable Instruments Plaits speech-synthesis
sources from the `eurorack/` submodule. The Mussola build compiles these
**instead of** the upstream files (the headers are still the upstream
ones — only `.cc` files are replaced, so no include-path shadowing is
needed).

Why not patch the submodule? The submodule pin must stay fetchable from
the upstream fork, and these fixes are only required by the Mussola
drumlogue unit, which drives the speech engines much harder than Plaits
itself does (4 unison voices, live parameter sweeps).

## Patches

`lpc_speech_synth_patched.cc`
- Clamp `excitation_pulse_sample_index_` to ≥ 0. Upstream subtracts
  `reset_sample` (0–31) right after `Init()` leaves the index at 0,
  producing a negative index into `lut_lpc_excitation_pulse` (a read
  up to 31 bytes before the table).

`lpc_speech_synth_controller_patched.cc`
- File-local phoneme table padded to 16 entries. `PlayFrame()` always
  reads `frames[integral + 1]`; triggering the top consonant plays
  frame 14 of the 15-entry table, so upstream read `phonemes_[15]`
  one past the end. The class-static `phonemes_` is now unreferenced
  and `lpc_speech_synth_phonemes.cc` is dropped from the build.
- `LoadNextWord()` can no longer write past `kLPCSpeechSynthMaxFrames`
  (1024) frames, and `Load()` can no longer write past
  `kLPCSpeechSynthMaxWords` word boundaries, regardless of what the
  word-bank bitstream contains. Upstream trusted the data blindly; a
  decode overrun would corrupt the adjacent voice's engine arena.
- **Crash fix**: `Render()` resets to scan mode when the harmonics
  crossing drops from the word-bank region into the phoneme region
  (`bank == -1`) while a word is playing. Upstream keeps
  `playback_frame_` pointing deep into the word bank but swaps
  `frames` to the 16-entry phoneme table, then reads kilobytes past
  it — reproduced under ASan with the exact hardware recipe
  (Model=Blend, 4 voices, Phoneme 83%, Timbre 100%, Harmonics sweep)
  and the direct cause of the "moving Harmonics while playing"
  SIGSEGV on drumlogue.

`sam_speech_synth_patched.cc`
- Clamp the second interpolation phoneme index. The top consonant sets
  `phoneme = 16` on the 17-entry table and upstream read
  `phonemes_[17]`. The fractional weight is 0 there, so the clamp is
  behavior-identical.

All three out-of-bounds reads are "usually harmless" on hosted builds
(they hit adjacent `.rodata`) but are undefined behavior and can fault
when the table ends a mapped page — the prime suspects for the
hardware-only crashes seen when sweeping Harmonics/Timbre with 4 voices.
