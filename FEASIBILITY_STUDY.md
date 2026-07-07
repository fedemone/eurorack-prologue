# Feasibility Study: Additional Module Ports to Drumlogue

## Overview

This document assesses the feasibility of porting three additional synthesis modules
to the drumlogue platform using the existing adapter layer (`drumlogue_osc_adapter`
+ `drumlogue_unit_wrapper`).

**Target Platform**: KORG drumlogue (ARM Cortex-A7 @ 1 GHz, NEON VFPv4, 48 kHz, Linux)
**Adapter API**: logue-sdk v2.0 Synth Module (unit_init/unit_render/unit_set_param_value)
**Existing Ports**: Plaits (12 oscillators), Elements (3 variants)

---

## 1. Mutable Instruments Clouds (Texture Synthesizer)

**Source**: `eurorack/clouds/dsp/`
**License**: MIT (Olivier Gillet / Mutable Instruments)

### Architecture

The `GranularProcessor` class implements four playback modes:

| Mode | Description | CPU Cost |
|------|-------------|----------|
| Granular | Up to 40 concurrent grains with overlap-add | Medium |
| Stretch (WSOLA) | Time-stretching with correlation-based pitch tracking | Medium |
| Looping Delay | Synchronizable delay with pitch shifting | Low-Medium |
| Spectral | Phase vocoder with 4096-point FFT | High |

Processing chain: Input capture -> Feedback HP filter -> Sample rate conversion
(48->32 kHz) -> Mode-specific processing -> Diffusion -> Pitch shifting -> Reverb
-> Dry/wet crossfade with soft limiting.

### Memory Requirements

| Component | Size |
|-----------|------|
| Main sample buffer | 116 KB |
| CCM workspace | 64 KB |
| Reverb (16k x uint16) | 32 KB |
| Diffuser (2k x float) | 8 KB |
| FFT buffers (spectral only) | 32 KB |
| Resource LUTs | ~213 KB |
| **Total** | **~400 KB** (with spectral) / **~300 KB** (without) |

All allocations happen in `Init()`/`Prepare()` -- no runtime dynamic allocation.

### Porting Assessment

**Strengths**:
- Pure C++ DSP with zero hardware dependencies (all in `dsp/` directory)
- Clean interface: `ShortFrame` input/output + `Parameters` struct
- Pre-allocated memory model matches drumlogue constraints
- Existing stmlib dependencies already available in this project

**Challenges**:
- Internal sample rate is 32 kHz (needs SRC bypass or re-tuning for 48 kHz)
- Fixed 32-sample block size (adapter buffering needed, straightforward)
- Spectral mode uses ShyFFT (header-only, but CPU-heavy on Cortex-A7)
- ~400 KB total memory footprint (verify drumlogue .drmlgunit limits)
- Resource tables add ~213 KB to binary size

**Parameter Mapping** (suggested 10-12 drumlogue params):

| Param | Clouds Control | Range |
|-------|---------------|-------|
| 0 | Position (grain start) | 0-100% |
| 1 | Size (grain length) | 0-100% |
| 2 | Density (grain rate) | 0-100% |
| 3 | Texture (grain overlap/window) | 0-100% |
| 4 | Pitch (transposition) | -24..+24 semitones |
| 5 | Dry/Wet | 0-100% |
| 6 | Stereo Spread | 0-100% |
| 7 | Feedback | 0-100% |
| 8 | Reverb | 0-100% |
| 9 | Mode Select | enum 0-3 |
| 10 | Freeze (toggle) | 0-1 |
| 11 | Base Note | MIDI 0-127 |

**Estimated Effort**: 2-3 weeks (core DSP trivial; SRC adaptation + testing substantial)

### Verdict: FEASIBLE (Medium Complexity)

Recommended approach: Start with Granular + Looping modes (skip spectral initially).
The 48 kHz internal processing change is the main risk. Consider running internal
DSP at 32 kHz with SRC in the adapter, or modifying grain size constants for 48 kHz.

---

## 2. Mutable Instruments Rings (Resonator)

**Source**: `eurorack/rings/dsp/`
**License**: MIT (Olivier Gillet / Mutable Instruments)

### Architecture

The `Part` class implements six resonator models:

| Model | Description | CPU Cost |
|-------|-------------|----------|
| Modal | Bank of 4-60 bandpass filters | Low-Medium |
| Sympathetic String | Physical string with sympathetic resonance | High |
| Karplus-Strong | String with dispersion and stiffness | Medium-High |
| FM Voice | FM synthesis with envelope follower | Low |
| Sympathetic Quantized | Strings with chord quantization | High |
| String + Reverb | Integrated Griesinger reverb | Medium-High |

### Memory Requirements

| Component | Size |
|-----------|------|
| Resonator bank (4 voices x 64 SVF) | ~8 KB |
| String delay lines (8 x 2048 float) | ~65 KB |
| Stiffness delay lines (8 x 1024 float) | ~32 KB |
| Reverb buffer (32k x int16) | 64 KB |
| Plucker instances (4 x 1.5 KB) | 6 KB |
| Other state | ~5 KB |
| **Total** | **~150-200 KB** |

All allocations are static -- no runtime dynamic allocation.

### Porting Assessment

**Strengths**:
- Pure DSP with zero hardware dependencies
- **Sample rate is 48 kHz** -- perfect match for drumlogue (no SRC needed!)
- Block size is 24 samples (same as Plaits, adapter already handles this)
- Clean API: `Part::Process(perf_state, patch, in, out, aux, size)`
- Float I/O (drumlogue native format)
- Well-separated from hardware code (`drivers/` directory not needed)

**Challenges**:
- Reverb requires 64 KB contiguous pre-allocation (passed to `Part::Init()`)
- Sympathetic string modes are CPU-intensive (8 parallel strings)
- String delay lines use Hermite interpolation (moderate per-sample cost)
- ~200 KB memory footprint (manageable)

**Parameter Mapping** (suggested 10 drumlogue params):

| Param | Rings Control | Range |
|-------|--------------|-------|
| 0 | Structure (freq ratio/inharmonicity) | 0-100% |
| 1 | Brightness (spectral tilt) | 0-100% |
| 2 | Damping (decay time) | 0-100% |
| 3 | Position (excitation point) | 0-100% |
| 4 | Model Select | enum 0-5 |
| 5 | Polyphony (1-4 voices) | enum 0-3 |
| 6 | Internal Exciter (on/off) | enum 0-1 |
| 7 | Base Note | MIDI 0-127 |
| 8 | LFO Target | enum |
| 9 | LFO2 Rate/Depth | 0-100% |

**Estimated Effort**: 1-2 weeks (excellent API fit, minimal adaptation needed)

### Verdict: HIGHLY FEASIBLE (Low Complexity)

Rings is the **best candidate** for the next port:
- 48 kHz native sample rate (zero SRC work)
- 24-sample block size (existing adapter works as-is)
- Clean Part::Process() API maps directly to the adapter pattern
- Rich sonic variety (6 models) in a single unit
- Moderate memory footprint

Recommended: Start with Modal and Karplus-Strong models. Disable StringSynthPart
(easter egg) to save CPU. Can later add as a separate unit.

---

## 3. MUSS3640 Vocal Synth (Formant Synthesizer)

**Source**: [github.com/cairnsynth/MUSS3640_Vocal_Synth](https://github.com/cairnsynth/MUSS3640_Vocal_Synth)
**License**: Not explicitly stated (academic project)

### Architecture

A formant-wave-function (FOF) + bandpass vocal synthesizer implementing:
- Rosenberg glottal pulse model (controllable pressure, T0, Te, noise)
- 5-formant bandpass filter bank (frequency, bandwidth, gain per formant)
- Three excitation sources: square wave (PWM), sawtooth, glottal pulse
- Fricative noise source with bandpass filtering
- Vibrato with ASR envelope
- Separate ADSR envelopes for voiced/fricative components
- XML phoneme preset system

### Technology Stack

| Component | Technology |
|-----------|-----------|
| DSP Core | **Faust** (.dsp files) |
| Plugin Framework | JUCE |
| C++ Integration | Faust-generated `DspFaust.cpp/h` |
| Phoneme Data | XML preset files |
| Analysis Tool | Praat script |

### Porting Assessment

**Significant Barriers**:

1. **Faust dependency**: The core DSP is written in Faust, not C++. Porting requires either:
   - (a) Using the Faust compiler to generate standalone C++ from the .dsp files, then
     adapting the output for the OSC adapter pattern, or
   - (b) Manually translating the Faust DSP to C++ (error-prone, loses maintainability)

2. **JUCE dependency**: The project is built around JUCE for audio I/O, MIDI, and
   GUI. The DSP must be extracted from this framework.

3. **No clear license**: The repository lacks an explicit open-source license,
   creating legal uncertainty for redistribution.

4. **Academic project**: Single developer, no community, uncertain maintenance status.

5. **XML phoneme loading**: The phoneme preset system relies on runtime XML parsing,
   which is impractical on drumlogue. Would need to be converted to compiled-in
   lookup tables.

**Alternative: Plaits Speech Engines** (already in `eurorack/plaits/dsp/speech/`):

The Plaits module already includes three speech synthesis engines that are
much better candidates for drumlogue:

| Engine | File | Description |
|--------|------|-------------|
| LPC Speech | `lpc_speech_synth.cc` | Linear Predictive Coding with word/phoneme databases |
| SAM | `sam_speech_synth.cc` | Software Automatic Mouth (classic phoneme synth) |
| Naive | `naive_speech_synth.cc` | Simple formant-based speech |

These are:
- Already in C++ (no Faust translation needed)
- MIT licensed
- Designed for embedded platforms (no JUCE, no XML)
- Part of the same eurorack codebase (stmlib compatibility guaranteed)
- Include pre-compiled phoneme/word databases

### Verdict: NOT RECOMMENDED as-is / USE PLAITS SPEECH INSTEAD

The MUSS3640 project has too many barriers (Faust dependency, no license, JUCE
entanglement) for practical drumlogue porting. The Plaits speech engines in
`eurorack/plaits/dsp/speech/` provide equivalent vocal synthesis capability with
zero porting friction.

**Recommended alternative**: Create a dedicated "Vocal" oscillator variant using
the existing `macro-oscillator2.cc` architecture with the Plaits SpeechEngine,
similar to how VA/FM/Granular variants are already built. Estimated effort: 3-5 days.

---

## Priority Ranking

| # | Module | Feasibility | Effort | Sonic Value | Recommendation |
|---|--------|-------------|--------|-------------|----------------|
| 1 | **Rings** | High | 1-2 weeks | High (6 resonator models) | **Port next** |
| 2 | **Clouds** | Medium | 2-3 weeks | High (granular textures) | Port after Rings |
| 3 | **Plaits Speech** | High | 3-5 days | Medium (vocal/percussion) | Quick add via existing macro-oscillator2 |
| 4 | MUSS3640 Vocal | Low | 4-6 weeks | Medium | Not recommended |

---

## Shared Infrastructure Improvements

Before porting additional modules, consider these improvements to the adapter layer:

### 1. Generalize unit_render for Stereo DSP

Rings and Clouds both produce **native stereo output**. The current `unit_render`
in `drumlogue_unit_wrapper.cc` assumes mono->stereo duplication. For these modules,
extend `osc_adapter_render()` to support stereo output:

```cpp
// New signature:
void osc_adapter_render_stereo(float *left, float *right, uint32_t frames);
// Or: void osc_adapter_render(float *output, uint32_t frames, bool stereo);
```

### 2. Reverb Buffer Management

Both Clouds and Rings require large pre-allocated reverb buffers (32-64 KB).
Add a static allocation pattern to the adapter:

```cpp
// In osc_adapter_init():
static uint16_t reverb_buffer[32768];  // 64 KB
```

### 3. Block Size Flexibility

The current `OSC_NATIVE_BLOCK_SIZE` compile-time constant works well.
Clouds (32) and Rings (24) both fit this pattern.

---

## Appendix: Source File Counts

| Module | DSP .cc files | DSP .h files | Resources | Total Lines |
|--------|--------------|-------------|-----------|-------------|
| Clouds | 5 | 15 | 1 (213 KB) | ~3,500 |
| Rings | 5 | 20 | 1 | ~5,000 |
| Plaits Speech | 6 | 5 | 2 (phoneme DBs) | ~2,000 |
