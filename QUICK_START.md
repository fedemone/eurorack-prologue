# Quick Start: Building Drumlogue Units

**Status:** ✅ Docker image built and tested, ready to build Drumlogue units!

## What Just Happened

You have successfully:
1. ✅ Cloned the eurorack-prologue project
2. ✅ Updated logue-sdk submodule to `main` branch (with Drumlogue support)
3. ✅ Built the Docker image with ARM cross-compiler
4. ✅ Built and tested `dummy_synth.drmlgunit`

## Test the Build System (One-liner)

```bash
cd /mnt/d/Fede/drumlogue/logue-sdk-eurorack-drumlogue-port/eurorack-prologue
./logue-sdk/docker/run_cmd.sh build drumlogue/dummy-synth
```

Check the output:
```bash
ls logue-sdk/platform/drumlogue/dummy-synth/dummy_synth.drmlgunit
```

## Next: Port Your First Oscillator

### Example: Port macro-oscillator2 to Drumlogue

```bash
# 1. Create unit directory
cd logue-sdk/platform/drumlogue
mkdir macro-oscillator2-synth
cd macro-oscillator2-synth

# 2. Copy template files
cp ../dummy-synth/config.mk .
cp ../dummy-synth/header.c .
cp ../dummy-synth/manifest.json .

# 3. Create unit.cc with oscillator code
# (See EURORACK_DRUMLOGUE_PORTING.md for detailed instructions)
```

### Build the Unit

```bash
cd /mnt/d/Fede/drumlogue/logue-sdk-eurorack-drumlogue-port/eurorack-prologue
./logue-sdk/docker/run_cmd.sh build drumlogue/macro-oscillator2-synth
```

### Test on Drumlogue

1. Connect Drumlogue via USB
2. Power on while holding MODE → "USB STORAGE"
3. Copy `dummy_synth.drmlgunit` to `Units/Synths/`
4. Eject USB, restart device

---

## Key Commands

| Task | Command |
|------|---------|
| **Build an existing unit** | `./logue-sdk/docker/run_cmd.sh build drumlogue/UNIT_NAME` |
| **Interactive shell** | `./logue-sdk/docker/run_interactive.sh` |
| **List available platforms** | `./logue-sdk/docker/run_cmd.sh build -l` |
| **List Drumlogue units** | `./logue-sdk/docker/run_cmd.sh build -l --drumlogue` |

---

## Directory Structure

```
eurorack-prologue/
├── logue-sdk/
│   ├── platform/drumlogue/
│   │   ├── dummy-synth/          ← Template synth
│   │   ├── dummy-delfx/          ← Template delay FX
│   │   ├── dummy-revfx/          ← Template reverb FX
│   │   ├── dummy-masterfx/       ← Template master FX
│   │   └── macro-oscillator2-synth/  ← Your port (to create)
│   └── docker/
│       ├── build_image.sh        ← Build Docker image
│       ├── run_cmd.sh            ← Run single build
│       └── run_interactive.sh    ← Interactive shell
├── macro-oscillator2.cc          ← Oscillator source
├── modal-strike.cc
├── osc_add.mk, osc_fm.mk, ...    ← Build rules
├── EURORACK_DRUMLOGUE_PORTING.md ← Detailed porting guide
└── WINDOWS_WSL2_SETUP.md         ← Full setup instructions
```

---

## Porting Checklist

For each oscillator you port:

- [ ] Create `logue-sdk/platform/drumlogue/[name]-synth/` directory
- [ ] Copy `config.mk`, `header.c`, `manifest.json` from `dummy-synth`
- [ ] Create `unit.cc` adapting oscillator code:
  - [ ] Extract core algorithm from `.cc` file
  - [ ] Implement `SYNTH_INIT()`
  - [ ] Implement `SYNTH_PROCESS_LOOP()`
  - [ ] Handle note on/off events
- [ ] Update `manifest.json` with unit name/description
- [ ] Build: `./logue-sdk/docker/run_cmd.sh build drumlogue/[name]-synth`
- [ ] Test on Drumlogue device

---

## Resources

- [Detailed Porting Guide](./EURORACK_DRUMLOGUE_PORTING.md)
- [Full Setup Guide](./WINDOWS_WSL2_SETUP.md)
- [Logue SDK Docs](https://korginc.github.io/logue-sdk/)
- [Drumlogue Docs](https://korginc.github.io/logue-sdk/drumlogue/)

---

## Troubleshooting

**Q: Docker build failed**
A: Retry the build. Network timeouts are common on first run.

```bash
cd logue-sdk/docker && ./build_image.sh
```

**Q: Build command says "drumlogue not found"**
A: Make sure you updated to the main branch (Step 3 of WINDOWS_WSL2_SETUP.md)

```bash
cd logue-sdk && git reset --hard origin/main
```

**Q: No .drmlgunit file in build output**
A: Check `manifest.json` is valid JSON.

```bash
python3 -m json.tool logue-sdk/platform/drumlogue/[unit-name]/manifest.json
```

---

## Next Steps

1. Read [EURORACK_DRUMLOGUE_PORTING.md](./EURORACK_DRUMLOGUE_PORTING.md) for detailed code adaptation
2. Choose which oscillator to port first
3. Create the unit directory and adapt the code
4. Build and test with Docker
5. Test on Drumlogue device

**Happy porting!** 🎛️
