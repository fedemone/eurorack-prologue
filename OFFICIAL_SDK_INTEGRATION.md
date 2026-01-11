# Official Drumlogue SDK Integration

## Overview

Your project now integrates the **Official Korg Drumlogue SDK** alongside the legacy Prologue/Minilogue XD/NTS-1 build system.

### Two Separate Build Systems

#### 1. **Legacy Prologue Build System** (for Prologue/Minilogue XD/NTS-1)
- Location: Root `Makefile` and `makefile.inc`
- Build command: `make` (requires GNU Make and ARM GCC toolchain)
- Output: `.prlgunit`, `.mnlgxdunit`, `.ntkdigunit` files
- Oscillators: Macro Oscillator 2 with multiple engines

#### 2. **Official Drumlogue SDK** (for Drumlogue) ✅ RECOMMENDED
- Location: `logue-sdk/` (official Korg repository)
- Build command: Docker-based (`logue-sdk/docker/run_cmd.sh`)
- Output: `.drmlgunit` files
- Features: Synth, Delay Effect, Reverb Effect, Master Effect units

---

## Quick Start for Drumlogue

### Prerequisites
- **Windows**: WSL2 + Docker Desktop
- **macOS/Linux**: Docker

### Step 1: Build Docker Image (First Time)
```bash
cd logue-sdk/docker
./build_image.sh
```

### Step 2: Build a Unit
```bash
# Build a specific project
logue-sdk/docker/run_cmd.sh build drumlogue/dummy-synth

# Build all drumlogue units
logue-sdk/docker/run_cmd.sh build --drumlogue

# Interactive shell for development
logue-sdk/docker/run_interactive.sh
```

### Step 3: Get Your `.drmlgunit` File
Output location: `logue-sdk/platform/drumlogue/<project>/build/<project>.drmlgunit`

---

## Project Structure

```
eurorack-prologue/
├── README.md                          # Main project README
├── DRUMLOGUE_SETUP.md                 # Drumlogue setup guide
├── drumlogue_build.sh                 # Quick build script
├── Makefile                           # Legacy Prologue build
├── makefile.inc                       # Legacy Prologue config (now with drumlogue)
│
├── logue-sdk/                         # Official Korg SDK (git submodule)
│   ├── platform/
│   │   ├── drumlogue/                # Drumlogue units directory
│   │   │   ├── common/               # Shared headers
│   │   │   ├── dummy-synth/          # Synth template
│   │   │   ├── dummy-delfx/          # Delay FX template
│   │   │   ├── dummy-revfx/          # Reverb FX template
│   │   │   └── dummy-masterfx/       # Master FX template
│   │   ├── prologue/
│   │   ├── minilogue-xd/
│   │   └── nutekt-digital/
│   ├── docker/
│   │   ├── build_image.sh            # Build Docker image
│   │   ├── run_interactive.sh        # Interactive shell
│   │   ├── run_cmd.sh                # Single command execution
│   │   ├── Dockerfile                # Docker configuration
│   │   └── README.md                 # Docker documentation
│   ├── tools/
│   ├── developer_ids.md              # Developer ID registry
│   └── README.md                     # Official SDK docs
│
├── eurorack/                          # Eurorack oscillator source code
├── credits.txt
└── manifest_*.json                   # Prologue manifests
```

---

## Important Notes

### About Your Makefile Changes
The changes to `Makefile` and `makefile.inc` that add "drumlogue" are **not used** by the official SDK. They were added to the legacy Prologue build system but won't work for Drumlogue because:
- Drumlogue uses a different CPU architecture (ARMv7 instead of STM32F4)
- Drumlogue requires different headers and build process
- The official SDK handles all compilation and linking

**These changes can be safely kept for reference** but use the official SDK for actual Drumlogue builds.

---

## Creating Your First Drumlogue Unit

### Option A: Modify Existing Template
1. Copy `logue-sdk/platform/drumlogue/dummy-synth/` to your project name
2. Edit `config.mk`:
   ```makefile
   PROJECT = my_project
   PROJECT_TYPE = synth  # or delfx, revfx, masterfx
   ```
3. Edit `header.c`: Update unit metadata, parameters
4. Edit `unit.cc`: Implement audio processing with `unit_render()` callback
5. Build: `logue-sdk/docker/run_cmd.sh build drumlogue/my_project`

### Option B: Port from Prologue
1. Take audio DSP code from `eurorack/plaits/` or other engines
2. Create new Drumlogue unit project structure
3. Wrap DSP code with Drumlogue unit API (see `dummy-synth/unit.cc`)
4. Define parameters in `header.c`
5. Build with Docker

---

## Key Differences: Drumlogue vs Prologue

| Aspect | Prologue | Drumlogue |
|--------|----------|-----------|
| **Build System** | GNU Make (cross-compile) | Docker + official build |
| **File Format** | `.prlgunit` | `.drmlgunit` |
| **Unit Type** | Multi-engine oscillator | Synth or Effect |
| **Parameters** | Variable | Up to 24 parameters |
| **Architecture** | ARM Cortex-M4 (STM32F4) | ARM Cortex-A7 (Linux) |
| **Development** | Local toolchain needed | Docker container |
| **API** | Minimal manifest.json | Full C/C++ SDK with headers |

---

## Build Troubleshooting

### "Permission denied" on `.sh` files (Windows WSL2)
```bash
chmod +x logue-sdk/docker/*.sh
```

### Docker not found
- Install Docker Desktop or Docker Engine
- On WSL2, ensure Docker Desktop is running on Windows

### Git line endings (CRLF errors)
```bash
git config core.autocrlf input
```

---

## Resources

- **Official Drumlogue SDK**: https://github.com/korginc/logue-sdk
- **Drumlogue Documentation**: `logue-sdk/platform/drumlogue/README.md`
- **Docker Setup**: `logue-sdk/docker/README.md`
- **API Reference**: See `unit.h` and example implementations in templates

---

## Next Steps

1. ✅ Verify Docker is installed and working
2. Build the Docker image: `cd logue-sdk/docker && ./build_image.sh`
3. Build a template: `./docker/run_cmd.sh build drumlogue/dummy-synth`
4. Check output: `logue-sdk/platform/drumlogue/dummy-synth/build/dummy_synth.drmlgunit`
5. Load onto Drumlogue and test!

