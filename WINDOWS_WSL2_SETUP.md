# Eurorack-Drumlogue: Windows WSL2 Setup Guide

This guide shows how to set up the Drumlogue SDK on Windows 10/11 and build `.drmlgunit` files from Eurorack Prologue oscillators.

**Current Status:** ✅ SDK updated, ✅ Docker building units successfully

## Prerequisites Installation

### 1. Install WSL2 (Windows Subsystem for Linux 2)

**Windows 10 Version 2004+ or Windows 11 required**

Open PowerShell as Administrator and run:
```powershell
wsl --install
```

This installs:
- WSL2 with Ubuntu Linux distribution
- Virtual Machine Platform
- Linux kernel

**Then restart your computer.**

Verify installation:
```powershell
wsl --list --verbose
```

### 2. Install Docker Desktop

1. Download: https://www.docker.com/products/docker-desktop
2. Install with default settings
3. Restart computer
4. Open PowerShell and verify:
```powershell
docker --version
docker run hello-world
```

### 3. Clone Your Repository

```powershell
cd C:\Users\YourUsername\Documents
git clone <your-repo-url>
cd eurorack-prologue
```

---

## Drumlogue Build Setup

### ⚠️ Important: PowerShell vs WSL2 Bash

Throughout these instructions, **bash code blocks should be run in WSL2 bash**, not PowerShell. PowerShell has different syntax and may not recognize bash commands like `git`.

**If you see a code block with `bash` label:**
```bash
# This should run in WSL2 bash, not PowerShell!
```

**If you see a code block with `powershell` label:**
```powershell
# This should run in PowerShell
```

### Step 1: Enter WSL2 Bash Shell

From PowerShell in your project directory:
```powershell
wsl
bash
```

You should see:
```
user@computer:/mnt/c/Users/YourUsername/Documents/eurorack-prologue$
```

### Step 2: Configure Git (One-time)

**Option A: In WSL2 Bash (Recommended)**
```bash
cd logue-sdk
git config core.autocrlf input
cd ..
```

**Option B: In PowerShell (Windows)**
```powershell
cd logue-sdk
& "C:\Program Files\Git\cmd\git.exe" config core.autocrlf input
cd ..
```

If git is in your PATH, you can also use:
```powershell
cd logue-sdk
& git config core.autocrlf input
cd ..
```

### Step 3: Initialize Git Submodules (Required!)

The official SDK is included as a git submodule. You must initialize it to get the Drumlogue SDK files:

```bash
cd ..
git submodule update --init --recursive
```

### ⚠️ Critical: Update SDK to Drumlogue-Enabled Version

The submodule may be pinned to an old SDK version WITHOUT Drumlogue support. **Update to the main branch:**

```bash
cd logue-sdk
git reset --hard origin/main
cd ..
```

**Verify Drumlogue content is present:**
```bash
ls logue-sdk/platform/drumlogue/
```

Should show:
```
common  dummy-delfx  dummy-masterfx  dummy-revfx  dummy-synth  README.md
```

If empty, the submodule didn't update. Try:
```bash
cd logue-sdk
git fetch origin
git checkout main
cd ..
```

---

### Step 4: Build Docker Image

The Docker image contains the ARM cross-compiler and build tools. **This step is required:**

```bash
cd logue-sdk/docker
./build_image.sh
```

**First run may take 10-15 minutes.** If it fails, run again:

```bash
./build_image.sh
```

Docker builds often fail first time due to network timeouts—retrying usually works.

**Verify build succeeded:**
```bash
docker images | grep logue-sdk
```

Should show an image with size ~3.7GB:
```
logue-sdk-dev-env   latest    2f2b35e55dd0   2 minutes ago   3.77GB
```

Should show an image with size ~3.7GB:
```
logue-sdk-dev-env   latest    2f2b35e55dd0   2 minutes ago   3.77GB
```

---

### Step 5: Test Build System

Build the dummy-synth example to verify everything works:

```bash
cd ../../../  # Back to eurorack-prologue root
./logue-sdk/docker/run_cmd.sh build drumlogue/dummy-synth
```

**Expected output:**
```
>> Initializing drumlogue development environment.
>> Building /workspace/drumlogue/dummy-synth
...
Done
```

**Verify the unit file was created:**
```bash
ls -lh logue-sdk/platform/drumlogue/dummy-synth/dummy_synth.drmlgunit
```

Should show a file of a few kilobytes:
```
-rwxrwxrwx  1 fede  fede  5.0K  Jan 10 11:04 dummy_synth.drmlgunit
```

✅ **If this worked, the build system is ready!**

---

### Step 5 Alternative: Build the Docker Image (If Available)

If `logue-sdk/docker/` exists:

```bash
cd logue-sdk/docker
./build_image.sh
```

This will take 5-10 minutes and download ~2GB of files.

**Expected output:**
```
Successfully tagged logue-sdk:latest
```

### Step 5 Alternative: Build Locally (If Docker Not Available)

If `logue-sdk/docker/` doesn't exist, you can build directly using the platform Makefile:

```bash
cd /mnt/d/Fede/drumlogue/logue-sdk-eurorack-drumlogue-port/eurorack-prologue
cd logue-sdk/platform/drumlogue/dummy-synth
make
```

The compiled `.drmlgunit` file will be in the `build/` directory.

---

### Step 6: Verify Build Success (Docker Method)

If using Docker:

```bash
docker images | grep logue-sdk
```

Should show:
```
logue-sdk    latest    xxxxx    2 minutes ago    2.5GB
```

---

## Building Your First Unit

### If Docker is Available

#### Option A: Quick Build (Single Command)

```bash
cd /mnt/d/Fede/drumlogue/logue-sdk-eurorack-drumlogue-port/eurorack-prologue
./logue-sdk/docker/run_cmd.sh build drumlogue/dummy-synth
```

**Output:**
```
>> Initializing drumlogue development environment.
>> Building /workspace/drumlogue/dummy-synth
Compiling header.c
Compiling _unit_base.c
Compiling unit.cc

Linking build/dummy_synth.drmlgunit
...
Done
>> Installing /workspace/drumlogue/dummy-synth
Deploying to /workspace/drumlogue/dummy-synth/dummy_synth.drmlgunit
Done
```

#### Option B: Interactive Shell (Development)

```bash
./logue-sdk/docker/run_interactive.sh

user@logue-sdk $ build -l --drumlogue
- drumlogue/dummy-delfx
- drumlogue/dummy-masterfx
- drumlogue/dummy-revfx
- drumlogue/dummy-synth

user@logue-sdk $ build drumlogue/dummy-synth
user@logue-sdk $ build drumlogue/dummy-delfx
user@logue-sdk $ exit
```

### If Docker is NOT Available (Local Makefile Build)

If `logue-sdk/docker/` doesn't exist in your SDK version, build locally:

```bash
cd /mnt/d/Fede/drumlogue/logue-sdk-eurorack-drumlogue-port/eurorack-prologue/logue-sdk/platform/drumlogue/dummy-synth
make
```

**Build output:**
```
Compiling header.c ...
Compiling _unit_base.c ...
Compiling unit.cc ...
Linking build/dummy_synth.drmlgunit
```

---

## Finding Your Build Output

The compiled unit file is located at:

```bash
logue-sdk/platform/drumlogue/dummy-synth/build/dummy_synth.drmlgunit
```

**Verify the file was created:**
```bash
ls -la logue-sdk/platform/drumlogue/dummy-synth/build/
```

**Open in Windows Explorer (from PowerShell):**
```powershell
explorer.exe .\logue-sdk\platform\drumlogue\dummy-synth\build\
```

---

## Next Steps: Loading onto Drumlogue

1. **Connect Drumlogue** to computer via USB
2. **Power on Drumlogue** while holding **MODE button** until "USB STORAGE" appears
3. **Copy the `.drmlgunit` file** from your build output to:
   - For synth units: `Units/Synths/`
   - For delay effects: `Units/DelayFXs/`
   - For reverb effects: `Units/ReverbFXs/`
   - For master effects: `Units/MasterFXs/`
4. **Eject USB** safely
5. **Restart Drumlogue** - new units appear in menu

---

## Workflow Summary

### Development Cycle

```bash
# 1. Enter WSL2 shell
wsl
bash

# 2. Make code changes in VS Code
cd logue-sdk/platform/drumlogue/my-project

# 3. Build (choose one method)
make                    # Use project Makefile
cd /path/to/workspace
./logue-sdk/docker/run_cmd.sh build drumlogue/my-project

# 4. Test on Drumlogue
# Copy build/my-project.drmlgunit to Drumlogue USB Units folder
```

### Using VS Code with WSL

1. Install extension: "Remote - WSL" by Microsoft
2. Open VS Code in WSL directory:
   ```powershell
   wsl code .
   ```
3. Edit files in VS Code (automatically sync with WSL)
4. Open integrated terminal (Ctrl+`) - already in WSL bash

---

## Troubleshooting

### "No such file or directory: logue-sdk/docker"

**Problem:** The docker directory doesn't exist in logue-sdk

**Solution:** Initialize git submodules:
```bash
cd eurorack-prologue
git submodule update --init --recursive
```

Verify it worked:
```bash
ls logue-sdk/docker/
```

Should show the build scripts.

### "Permission denied: ./run_cmd.sh"
```bash
chmod +x logue-sdk/docker/*.sh
```

### "Docker daemon is not running"
- Open Docker Desktop application on Windows
- Wait for "Docker Desktop is running" message

### "file not found" or "No such file or directory"
- Check you're in the correct directory: `pwd`
- Use `/mnt/c/` for Windows C: drive paths
- Don't use backslashes - use forward slashes: `/path/to/file`

### "Cannot find module 'xyz'"
- Update submodules: `git submodule update --init --recursive`
- Rebuild Docker image: `cd logue-sdk/docker && ./build_image.sh`

### CRLF line ending errors
```bash
cd logue-sdk
git config core.autocrlf input
git rm --cached -r .
git reset --hard
```

### Port already in use
- Docker might still be cleaning up from previous build
- Wait a minute, then try again
- Or restart Docker Desktop

---

## Useful Commands

```bash
# List all available drumlogue projects
./logue-sdk/docker/run_cmd.sh build -l --drumlogue

# Build all drumlogue units at once
./logue-sdk/docker/run_cmd.sh build --drumlogue

# Clean a project
./logue-sdk/docker/run_cmd.sh build --clean drumlogue/dummy-synth

# Interactive development mode
./logue-sdk/docker/run_interactive.sh

# Check Docker setup
docker --version
docker ps

# Navigate in WSL
cd /mnt/c/path/to/windows/folder
ls -la

# Exit WSL shell
exit
```

---

## Resources

- **Official Drumlogue SDK**: https://github.com/korginc/logue-sdk
- **WSL2 Documentation**: https://learn.microsoft.com/en-us/windows/wsl/
- **Docker Documentation**: https://docs.docker.com/
- **Drumlogue Manual**: https://www.korg.com/products/drums/drumlogue

---

## Quick Reference Card

| Task | Command |
|------|---------|
| Enter WSL bash | `wsl` then `bash` |
| Build Docker image | `cd logue-sdk/docker && ./build_image.sh` |
| Build unit | `./logue-sdk/docker/run_cmd.sh build drumlogue/dummy-synth` |
| List projects | `./logue-sdk/docker/run_cmd.sh build -l --drumlogue` |
| Interactive shell | `./logue-sdk/docker/run_interactive.sh` |
| Clean build | `./logue-sdk/docker/run_cmd.sh build --clean drumlogue/dummy-synth` |
| Find output | `logue-sdk/platform/drumlogue/dummy-synth/build/` |
| Fix permissions | `chmod +x logue-sdk/docker/*.sh` |

