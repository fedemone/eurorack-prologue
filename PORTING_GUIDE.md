# Next Steps: Port Your First Oscillator

**Current Status**: Build system ready ✅
**What's next**: Choose an oscillator and port it to Drumlogue

---

## Quick Decision Tree

### What kind of oscillator do you want to port first?

**Option A: Versatile harmonic oscillator**
→ Port `macro-oscillator2.cc`
- Produces various tones (sine, triangle, square, etc.)
- Good first choice - common in synthesizers

**Option B: Physical modeling**
→ Port `modal-strike.cc`
- Models physical resonances
- More complex, interesting for percussion

**Option C: Use the Makefile targets**
→ Build from existing `osc_*.mk` files
- Examples: `osc_add.mk`, `osc_fm.mk`, `osc_wta.mk`
- See `makefile.inc` for available oscillators

---

## Port Your First Oscillator: Step-by-Step

### 1. Prepare the Directory

Replace `MYOSCILLATOR` with your choice (e.g., `macro-oscillator2`):

```bash
# In WSL2 bash:
cd /mnt/d/Fede/drumlogue/logue-sdk-eurorack-drumlogue-port/eurorack-prologue
cd logue-sdk/platform/drumlogue

# Create new unit directory
mkdir MYOSCILLATOR-synth
cd MYOSCILLATOR-synth

# Copy template files from dummy-synth
cp ../dummy-synth/config.mk .
cp ../dummy-synth/header.c .
cp ../dummy-synth/manifest.json .
```

### 2. Update config.mk

Edit `config.mk`:

```makefile
PROJECT = my_oscillator
PROJECT_TYPE = synth
PLATFORM = drumlogue
```

Replace `my_oscillator` with your unit name.

### 3. Update manifest.json

Edit `manifest.json`:

```json
{
  "version": "1.0-0",
  "type": "synth",
  "category": "Oscillator",
  "name": "My Oscillator",
  "description": "Eurorack oscillator ported to Drumlogue",
  "author": "Your Name",
  "license": "BSD-3-Clause",
  "engine_version": "2.1.0",
  "url": "https://github.com/yourusername/eurorack-drumlogue",
  "parameters": [
    {
      "id": 6,
      "name": "Shape",
      "range": [0, 127],
      "default": 0
    }
  ]
}
```

Customize the name, description, and author.

### 4. Create unit.cc - The Hard Part

This is where you adapt your oscillator code. Here's a template:

```cpp
// unit.cc - Drumlogue oscillator unit

#include "common.h"

// ============================================================================
// YOUR OSCILLATOR STATE
// ============================================================================

typedef struct {
  float phase;
  float pitch;
  uint32_t osc_phase;  // Fixed-point phase accumulator
  // ... add other oscillator state variables
} OscState;

static OscState g_state;

// ============================================================================
// INITIALIZATION (called once at startup)
// ============================================================================

void SYNTH_INIT() {
  g_state.phase = 0.0f;
  g_state.pitch = 0.0f;
  g_state.osc_phase = 0;
  // Initialize your oscillator here
}

// ============================================================================
// NOTE ON (MIDI note pressed)
// ============================================================================

void NOTE_ON(uint8_t note, uint8_t velocity) {
  // Convert MIDI note number to frequency
  // note: 0-127 (C-1 to G9)
  // velocity: 0-127 (1-127 actually, 0 is note off)

  float freq = 440.0f * powf(2.0f, (note - 69.0f) / 12.0f);
  g_state.pitch = freq;
}

// ============================================================================
// NOTE OFF (MIDI note released)
// ============================================================================

void NOTE_OFF(uint8_t note) {
  // Handle note release (can stop oscillator, trigger envelope, etc.)
  // For now, just silence
  g_state.pitch = 0.0f;
}

// ============================================================================
// AUDIO PROCESSING LOOP
// ============================================================================

void SYNTH_PROCESS_LOOP(const user_oscillator_sample_t* in,
                         user_oscillator_sample_t* out,
                         uint32_t frames) {
  // in: input samples (usually ignored for synth)
  // out: output samples to fill
  // frames: number of samples to generate (usually 64 or 128)

  for (uint32_t i = 0; i < frames; ++i) {
    // Generate your oscillator sample
    float sample = 0.0f;

    if (g_state.pitch > 0.0f) {
      // Your oscillator code here
      // Example: simple sine wave
      float phase_inc = g_state.pitch / 48000.0f;  // 48kHz sample rate
      g_state.phase += phase_inc;
      if (g_state.phase >= 1.0f) {
        g_state.phase -= 1.0f;
      }
      sample = sinf(2.0f * M_PI * g_state.phase);
    }

    // Convert float sample (-1.0 to +1.0) to int32 output
    out[i].s = f32_to_s32(sample);
  }
}
```

### 5. Extract Your Oscillator Algorithm

Look at your original oscillator code (e.g., `macro-oscillator2.cc`):

```bash
# In your text editor or IDE, open:
cat ../../macro-oscillator2.cc
```

Look for:
- **Initialization code** → Goes in `SYNTH_INIT()`
- **Core algorithm** → Goes in `SYNTH_PROCESS_LOOP()`
- **State variables** → Goes in `struct OscState`
- **Parameter handling** → Goes in `NOTE_ON()` and update loop

### 6. Build the Unit

From project root:

```bash
cd /mnt/d/Fede/drumlogue/logue-sdk-eurorack-drumlogue-port/eurorack-prologue

# Build your unit
./logue-sdk/docker/run_cmd.sh build drumlogue/MYOSCILLATOR-synth
```

Should output:
```
>> Building /workspace/drumlogue/MYOSCILLATOR-synth
Compiling header.c
Compiling unit.cc
...
Done
```

### 7. Verify Output

Check the compiled unit file:

```bash
ls -lh logue-sdk/platform/drumlogue/MYOSCILLATOR-synth/*.drmlgunit
```

Should show a file a few KB in size.

### 8. Test on Drumlogue

1. **Connect** Drumlogue to Windows via USB
2. **Enter USB mode**: Power on while holding MODE until "USB STORAGE" appears
3. **Copy file**:
   - Open Windows Explorer
   - Navigate to Drumlogue storage
   - Open folder: `Units/Synths/`
   - Copy your `.drmlgunit` file there
4. **Eject**: Safely eject USB
5. **Restart**: Power cycle Drumlogue
6. **Test**: Your oscillator should appear in the synth menu!

---

## Helpful Resources

**For code adaptation:**
- See [EURORACK_DRUMLOGUE_PORTING.md](EURORACK_DRUMLOGUE_PORTING.md) for detailed examples
- Check `logue-sdk/platform/drumlogue/dummy-synth/` for reference implementation
- Look at other Drumlogue units in `logue-sdk/platform/drumlogue/` for inspiration

**For understanding Drumlogue SDK:**
- `logue-sdk/platform/drumlogue/README.md` - Official documentation
- `logue-sdk/platform/drumlogue/common/` - Header definitions and utilities

**For the original oscillators:**
- `eurorack/*/` directories contain original Eurorack code
- `macro-oscillator2.cc` and `modal-strike.cc` are entry points
- `makefile.inc` shows oscillator configurations

---

## Common Issues & Solutions

### Issue: "Cannot find unit after building"
→ Check the output directory:
```bash
ls -la logue-sdk/platform/drumlogue/MYOSCILLATOR-synth/
# Should show: *.drmlgunit file
```

### Issue: "Build says files not found"
→ Make sure you're in the right directory:
```bash
pwd  # Should end with: eurorack-prologue
```

### Issue: "My code doesn't work on device"
→ Start with a sine wave test:
```cpp
sample = sinf(2.0f * M_PI * g_state.phase);
```
Once that works, replace with your oscillator code.

### Issue: "Build fails with compiler errors"
→ Check the error message carefully
→ Common mistakes:
  - Missing `#include` statements
  - Wrong function signatures
  - Using incompatible data types

---

## Simplest First Port: Sine Oscillator

If you want to test the build system with minimal code, here's a complete `unit.cc`:

```cpp
#include "common.h"

typedef struct {
  float pitch;
  float phase;
} OscState;

static OscState g_state;

void SYNTH_INIT() {
  g_state.pitch = 0.0f;
  g_state.phase = 0.0f;
}

void NOTE_ON(uint8_t note, uint8_t velocity) {
  g_state.pitch = 440.0f * powf(2.0f, (note - 69.0f) / 12.0f);
}

void NOTE_OFF(uint8_t note) {
  g_state.pitch = 0.0f;
}

void SYNTH_PROCESS_LOOP(const user_oscillator_sample_t* in,
                         user_oscillator_sample_t* out,
                         uint32_t frames) {
  for (uint32_t i = 0; i < frames; ++i) {
    float sample = 0.0f;
    if (g_state.pitch > 0.0f) {
      g_state.phase += g_state.pitch / 48000.0f;
      while (g_state.phase >= 1.0f) g_state.phase -= 1.0f;
      sample = sinf(2.0f * 3.14159f * g_state.phase) * 0.5f;
    }
    out[i].s = f32_to_s32(sample);
  }
}
```

This builds, compiles, and runs correctly.

---

## Recommended Order

1. **Test**: Build the simple sine oscillator above (verify system works)
2. **Port**: `macro-oscillator2.cc` (versatile, good complexity)
3. **Port**: `modal-strike.cc` (learn physical modeling)
4. **Port**: Others as needed (FM, wavetables, etc.)

---

## You're Ready!

Choose an oscillator, follow the steps, and start porting. The build system is ready and tested. Happy coding! 🎛️

Questions? See:
- [EURORACK_DRUMLOGUE_PORTING.md](EURORACK_DRUMLOGUE_PORTING.md) - Detailed guide
- [QUICK_START.md](QUICK_START.md) - Quick reference
- [BUILD_STATUS.md](BUILD_STATUS.md) - Current status
