# Drumlogue SDK Setup Guide

This project has been updated to use the **Official Korg Drumlogue SDK** for building custom units.

## Prerequisites

### Windows (WSL2 Required)
1. Install **Windows Subsystem for Linux 2 (WSL2)**
   - https://learn.microsoft.com/en-us/windows/wsl/install

2. Install **Docker Desktop** or **Docker Engine**
   - https://docs.docker.com/get-docker/

3. Configure Git (WSL2 shell):
   ```bash
   git config core.autocrlf input
   ```

### macOS / Linux
1. Install **Docker**
   - https://docs.docker.com/get-docker/

## Build Instructions

### Option 1: Using Docker (Recommended)

1. **Build the Docker image** (first time only):
   ```bash
   cd logue-sdk/docker
   ./build_image.sh
   ```

2. **Interactive Build Shell** (for development):
   ```bash
   cd logue-sdk/docker
   ./run_interactive.sh

   # Inside the container:
   user@logue-sdk $ build drumlogue/dummy-synth
   user@logue-sdk $ build -l --drumlogue  # List all drumlogue projects
   ```

3. **Single Command Build** (for CI/automation):
   ```bash
   cd logue-sdk
   ./docker/run_cmd.sh build drumlogue/dummy-synth
   ./docker/run_cmd.sh build --drumlogue  # Build all drumlogue units
   ```

### Option 2: Manual Docker Setup
```bash
cd logue-sdk/docker
docker build -t logue-sdk:latest .
docker run -it -v $(pwd)/..:/workspace logue-sdk:latest bash
```

## Project Structure

```
eurorack-prologue/
├── logue-sdk/                     # Official Korg SDK
│   ├── platform/
│   │   ├── drumlogue/            # Drumlogue units
│   │   │   ├── common/           # Shared headers
│   │   │   ├── dummy-synth/      # Template synth unit
│   │   │   ├── dummy-delfx/      # Template delay effect
│   │   │   ├── dummy-revfx/      # Template reverb effect
│   │   │   └── dummy-masterfx/   # Template master effect
│   │   ├── minilogue-xd/
│   │   ├── prologue/
│   │   └── ...
│   └── docker/                    # Docker build environment
└── Makefile                       # Legacy Prologue build system
```

## Creating New Drumlogue Units

1. Copy a template (e.g., `logue-sdk/platform/drumlogue/dummy-synth`)
2. Customize:
   - `config.mk` - Project settings
   - `header.c` - Unit metadata and parameters
   - `unit.cc` - Audio processing implementation

3. Build:
   ```bash
   ./logue-sdk/docker/run_cmd.sh build drumlogue/your-project
   ```

## Output Files

- `.drmlgunit` - Compiled unit file (load onto Drumlogue)
- `build/your-project.hex` - Hex file
- `build/your-project.bin` - Binary file
- `build/your-project.dmp` - Disassembly

## Loading Units onto Drumlogue

1. Connect Drumlogue in USB Mass Storage mode
2. Copy `.drmlgunit` files to:
   - `Units/Synths/` for synth units
   - `Units/DelayFXs/` for delay effects
   - `Units/ReverbFXs/` for reverb effects
   - `Units/MasterFXs/` for master effects
3. Restart Drumlogue

## Official Documentation

- Drumlogue README: `logue-sdk/platform/drumlogue/README.md`
- Docker Setup: `logue-sdk/docker/README.md`
- Full SDK: https://github.com/korginc/logue-sdk

## Notes

- Firmware >= 1.00 required for SDK v2.0-0
- See `logue-sdk/developer_ids.md` for developer ID assignment
- All paths are relative to this directory
