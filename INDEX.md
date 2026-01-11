# 📂 Project Documentation Index

## Quick Navigation

### 🎯 I need to...

**Get started quickly**
→ Read [00_READ_ME_FIRST.md](00_READ_ME_FIRST.md)

**Set up on Windows**
→ Follow [WINDOWS_WSL2_SETUP.md](WINDOWS_WSL2_SETUP.md)

**Set up on macOS/Linux**
→ Follow [DRUMLOGUE_SETUP.md](DRUMLOGUE_SETUP.md)

**Understand the architecture**
→ Read [OFFICIAL_SDK_INTEGRATION.md](OFFICIAL_SDK_INTEGRATION.md)

**Build my first unit**
→ Use commands in [START_HERE.md](START_HERE.md)

**Understand what changed**
→ Check [LEGACY_MAKEFILE_NOTES.md](LEGACY_MAKEFILE_NOTES.md)

**See all documentation**
→ Browse [DOCUMENTATION_GUIDE.md](DOCUMENTATION_GUIDE.md)

**Check project status**
→ Read [INTEGRATION_COMPLETE.md](INTEGRATION_COMPLETE.md)

---

## 📄 All Documentation Files

| File | Purpose | Size | Read Time |
|------|---------|------|-----------|
| [00_READ_ME_FIRST.md](00_READ_ME_FIRST.md) | Complete overview and checklist | 10.6 KB | 5 min |
| [START_HERE.md](START_HERE.md) | Quick reference and next steps | 7.0 KB | 5 min |
| [DRUMLOGUE_SETUP.md](DRUMLOGUE_SETUP.md) | Drumlogue SDK overview | 3.4 KB | 3 min |
| [WINDOWS_WSL2_SETUP.md](WINDOWS_WSL2_SETUP.md) | Windows setup guide | 6.8 KB | 15 min |
| [OFFICIAL_SDK_INTEGRATION.md](OFFICIAL_SDK_INTEGRATION.md) | Technical architecture | 6.1 KB | 10 min |
| [LEGACY_MAKEFILE_NOTES.md](LEGACY_MAKEFILE_NOTES.md) | Reference documentation | 5.1 KB | 5 min |
| [INTEGRATION_COMPLETE.md](INTEGRATION_COMPLETE.md) | Project summary | 7.4 KB | 5 min |
| [DOCUMENTATION_GUIDE.md](DOCUMENTATION_GUIDE.md) | Documentation roadmap | 8.2 KB | 5 min |

**Total:** ~54 KB of documentation

---

## 🚀 Build Commands

### For Drumlogue (Official SDK)
```bash
# Build Docker image (one-time)
cd logue-sdk/docker && ./build_image.sh

# Build a specific project
../run_cmd.sh build drumlogue/dummy-synth

# Build all Drumlogue projects
../run_cmd.sh build --drumlogue

# List available projects
../run_cmd.sh build -l --drumlogue

# Interactive development shell
../run_interactive.sh

# Clean a project
../run_cmd.sh build --clean drumlogue/dummy-synth
```

### For Legacy Platforms (Prologue/Minilogue XD/NTS-1)
```bash
make                    # Build all platforms
make prologue          # Build Prologue only
make minilogue-xd      # Build Minilogue XD only
make nutekt-digital    # Build NTS-1 only
make clean             # Clean all builds
make drumlogue         # Build only Drumlogue (legacy, reference only)
```

---

## 📦 File Organization

### Documentation
- `00_READ_ME_FIRST.md` - Overview (start here!)
- `START_HERE.md` - Quick start
- `DRUMLOGUE_SETUP.md` - General setup
- `WINDOWS_WSL2_SETUP.md` - Windows guide
- `OFFICIAL_SDK_INTEGRATION.md` - Technical details
- `LEGACY_MAKEFILE_NOTES.md` - Reference
- `INTEGRATION_COMPLETE.md` - Summary
- `DOCUMENTATION_GUIDE.md` - This map
- `INDEX.md` - You are here

### Build Configuration
- `Makefile` - Modified (legacy Prologue build)
- `makefile.inc` - Modified (legacy Prologue config)
- `drumlogue_build.sh` - Helper script

### Project Source
- `eurorack/` - Oscillator DSP code
- `logue-sdk/` - Official Korg SDK (git submodule)
  - `platform/drumlogue/` - Drumlogue units
  - `docker/` - Build environment
  - `tools/` - SDK tools

---

## ✅ Setup Checklist

- [ ] Read 00_READ_ME_FIRST.md
- [ ] Choose setup guide (Windows or macOS/Linux)
- [ ] Install Docker
- [ ] Install WSL2 (Windows only)
- [ ] Build Docker image
- [ ] Test first build
- [ ] Verify .drmlgunit output
- [ ] Load onto Drumlogue
- [ ] Success! 🎉

---

## 🔗 Resources

### Official
- **GitHub**: https://github.com/korginc/logue-sdk
- **Drumlogue**: https://www.korg.com/products/drums/drumlogue
- **Eurorack-Prologue**: https://github.com/peterall/eurorack-prologue

### In This Project
- `logue-sdk/platform/drumlogue/README.md` - Official Drumlogue docs
- `logue-sdk/docker/README.md` - Docker setup documentation
- `logue-sdk/developer_ids.md` - Register your developer ID

---

## 📞 Support

### Common Issues

**Permission denied on scripts**
```bash
chmod +x logue-sdk/docker/*.sh
```

**Docker not found**
- Install Docker Desktop or Docker Engine
- Ensure Docker is running

**Cannot find submodules**
```bash
git submodule update --init --recursive
```

### More Help
See troubleshooting sections in:
- WINDOWS_WSL2_SETUP.md
- OFFICIAL_SDK_INTEGRATION.md
- DRUMLOGUE_SETUP.md

---

## 🎯 Getting Started

### Step 1: Choose Your Setup Guide
- Windows 10/11 → [WINDOWS_WSL2_SETUP.md](WINDOWS_WSL2_SETUP.md)
- macOS → [DRUMLOGUE_SETUP.md](DRUMLOGUE_SETUP.md)
- Linux → [DRUMLOGUE_SETUP.md](DRUMLOGUE_SETUP.md)

### Step 2: Follow the Setup
Install Docker and build the image:
```bash
cd logue-sdk/docker
./build_image.sh
```

### Step 3: Build Your First Unit
```bash
cd ../..
./logue-sdk/docker/run_cmd.sh build drumlogue/dummy-synth
```

### Step 4: Find Your Output
```
logue-sdk/platform/drumlogue/dummy-synth/build/dummy_synth.drmlgunit
```

### Step 5: Load onto Drumlogue
Copy `.drmlgunit` to appropriate Units folder and restart.

---

**Happy developing! 🚀**
