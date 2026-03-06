# Feasibility Study: Vocal Synth Engine for drumlogue

**Goal**: Abstract choral vocalization (male/female/robotic/alien) — not realistic voice,
not real speech. Based on Mutable Instruments Plaits SpeechEngine with possible extensions.

## Source Architecture (Plaits SpeechEngine)

Plaits provides three speech synthesis sub-models blended via a single `harmonics` parameter:

### 1. NaiveSpeechSynth (Formant Filters)
- **How**: Pulse oscillator → 5 parallel SVF bandpass filters tuned to formant frequencies
- **Data**: 5 phonemes × 5 registers × 5 formants (~250 bytes static table)
- **Params**: `frequency`, `phoneme` (0-1), `vocal_register` (0-1)
- **Character**: Warm, resonant, synthetic vowel sounds. Best for "choir pad" textures.
- **CPU**: ~30-50 FLOPS/sample (5 SVF filters). Moderate.

### 2. SAMSpeechSynth (Software Automatic Mouth)
- **How**: 3 formant oscillators (uint32 phase accumulators) + pulse excitation
- **Data**: 17 phonemes (9 vowels + 8 consonants, ~102 bytes)
- **Params**: `frequency`, `vowel` (0-1), `formant_shift`, `consonant` flag
- **Character**: Retro 8-bit speech, robotic. Good for alien/robotic vocalization.
- **CPU**: ~15-20 FLOPS/sample. Light.

### 3. LPCSpeechSynth (10th-order Linear Predictive Coding)
- **How**: 10-pole all-pole filter driven by pulse/noise excitation, frame-based at 40 FPS
- **Data**: Word banks (packed bitstream, ~50-100KB for full set, up to 32 words/1024 frames)
- **Params**: `f0`, `prosody_amount`, `speed`, `word_bank` selection
- **Character**: Most speech-like, vocal fragments. Can sound eerie/alien at extreme settings.
- **CPU**: ~20-30 FLOPS/sample (10 MACs per sample). Moderate.

### Blending
SpeechEngine cross-fades between models based on `harmonics` parameter:
- 0.0-0.33: NaiveSpeechSynth (pure) → SAMSpeechSynth
- 0.33-0.67: SAMSpeechSynth → LPCSpeechSynth
- 0.67-1.0: LPCSpeechSynth (switches word banks)
- Quintic smoothing (`blend *= blend * (3.0f - 2.0f * blend)` applied twice)

## drumlogue Platform Constraints

| Resource | Available | Required |
|----------|-----------|----------|
| CPU | ARM Cortex-A7 @ 1GHz | ~50 FLOPS/sample × 48kHz ≈ 2.4 MFLOPS (single engine) |
| Static RAM | ~512KB usable | ~250B (Naive) + ~102B (SAM) + ~50-100KB (LPC banks) |
| Block size | 32 samples | Compatible with all three engines |
| Sample rate | 48 kHz | Plaits designed for 32 kHz; frequency scaling needed |
| Output | Stereo Q31 | SpeechEngine produces stereo float (main + aux) |

**Verdict**: All three engines fit comfortably within drumlogue's CPU and memory budget.

## Proposed Parameter Mapping (drumlogue Synth Module, 16 params max)

| id | Name | Range | Maps to |
|----|------|-------|---------|
| 0 | BaseNote | 0-127 | MIDI note (pitch) |
| 1 | Position | shape knob | Phoneme/vowel selection |
| 2 | Timbre | shiftshape | Vocal register / formant shift |
| 3 | Morph | 0-100% | Cross-model blend (Naive↔SAM↔LPC) |
| 4 | Color | 0-100% | Brightness / formant spread |
| 5 | Speed | -100..+100 | LPC playback speed |
| 6 | Decay | 0-100% | Amplitude envelope decay |
| 7 | Reverb | 0-100% | Built-in reverb mix (if budget allows) |
| 8 | Model | 0-2 | Force: Naive/SAM/LPC (vs blend) |
| 9 | WordBank | 0-31 | LPC word bank selection |
| 10 | Gender | 0-100% | Formant shift up/down for male↔female |
| 11 | Vibrato | 0-100% | Pitch vibrato depth |
| 12 | Detune | 0-100% | Unison detune for choral effect |
| 13 | Voices | 1-4 | Unison voice count |
| 14 | Spread | 0-100% | Stereo spread of unison voices |
| 15 | Attack | 0-100% | Amplitude envelope attack |

## Choral Vocalization Strategy

For abstract choral sound (the primary use case), the key technique is **unison detuning
with formant variation**:

1. **Multiple voices** (2-4): Each voice runs its own SpeechEngine instance with slight
   pitch detuning (±5-15 cents) and formant offset.
2. **Stereo spread**: Voices panned across stereo field.
3. **Slow modulation**: LFO on phoneme parameter creates evolving vowel movement ("aah-ooh-eeh").
4. **Gender control**: Formant shift parameter transposes formants independently of pitch,
   enabling male/female/child character without changing musical pitch.

**CPU budget for 4 voices**: ~4 × 50 FLOPS/sample × 48kHz ≈ 9.6 MFLOPS.
Cortex-A7 @ 1GHz handles ~500 MFLOPS (with NEON). Plenty of headroom.

## Implementation Approach

### Phase 1: Direct Port (Minimal)
- Port SpeechEngine as-is from Plaits source
- Adapt to drumlogue's OSC API (same pattern as Clouds/Rings/Elements ports)
- Single voice, basic parameter mapping
- **Effort**: ~2-3 days, following established porting pattern
- **Files**: `speech-vocal.cc`, additions to `header.c`, `drumlogue_unit_wrapper.cc`

### Phase 2: Choral Extensions
- Add unison voice engine (2-4 instances of SpeechEngine)
- Implement Gender (formant shift independent of pitch)
- Add amplitude envelope (attack/decay)
- Stereo spread for voice panning
- **Effort**: ~3-5 days

### Phase 3: Additional Timbres (Optional)
- Custom phoneme tables for non-human vocalization (alien, robotic)
- NEON-optimized SVF filter bank for Naive model
- LPC word bank editor/loader (custom vocal fragments)
- **Effort**: ~5-10 days

## Key Dependencies
- `eurorack/plaits/dsp/engine/speech_engine.h/cc`
- `eurorack/plaits/dsp/speech/naive_speech_synth.h/cc`
- `eurorack/plaits/dsp/speech/sam_speech_synth.h/cc`
- `eurorack/plaits/dsp/speech/lpc_speech_synth.h/cc`
- `eurorack/plaits/dsp/speech/lpc_speech_synth_controller.h/cc`
- `eurorack/stmlib/dsp/dsp.h` (SVF, oscillator utilities)

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| 48kHz vs 32kHz sample rate | Formant tuning shifts | Scale frequency params by 32/48 ratio |
| 4-voice CPU budget | Potential audio glitches | Fall back to 2 voices; NEON vectorize filters |
| LPC word banks large | Memory pressure | Ship subset (8-12 banks); lazy-load from storage |
| Gender shift at extremes | Unnatural artifacts | Clamp formant shift range; use musical intervals |

## References
- Mutable Instruments Plaits source: `eurorack/plaits/dsp/speech/`
- Klatt, D.H. (1980). "Software for a cascade/parallel formant synthesizer" — formant synthesis foundation
- Software Automatic Mouth (SAM, 1982) — inspiration for SAMSpeechSynth
- Atal & Hanauer (1971). "Speech Analysis and Synthesis by LPC" — LPC codec foundation
- MUSS3640 topics: formant synthesis, source-filter model, vocal tract modeling
