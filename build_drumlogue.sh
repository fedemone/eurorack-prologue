#!/bin/bash
#
# build_drumlogue.sh
#
# Convenience wrapper for building drumlogue oscillator units using
# the official logue-sdk Docker build system.
#
# Works on Linux, macOS, and Windows (Git Bash / MSYS2).
#
# Prerequisites:
#   - Docker installed and running
#   - logue-sdk submodule initialized (git submodule update --init)
#   - Docker image built: cd logue-sdk && docker/build_image.sh
#
# Usage:
#   ./build_drumlogue.sh                  # Build all oscillators
#   ./build_drumlogue.sh mo2_va           # Build one oscillator
#   ./build_drumlogue.sh mo2_va mo2_fm    # Build specific oscillators
#   ./build_drumlogue.sh --clean          # Clean all build artifacts
#   ./build_drumlogue.sh --list           # List available projects
#   ./build_drumlogue.sh --interactive    # Enter Docker shell
#   ./build_drumlogue.sh --collect        # Collect .drmlgunit files
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_DIR="${SCRIPT_DIR}/logue-sdk"
SDK_DRUMLOGUE="${SCRIPT_DIR}/drumlogue"
OUTPUT_DIR="${SCRIPT_DIR}/build/drumlogue"

# Docker image names (matches SDK's run_cmd.sh logic)
IMAGE_NAME_DEFAULT="xiashj/logue-sdk"
IMAGE_NAME_FALLBACK="logue-sdk-dev-env"
IMAGE_VERSION="latest"

# All available oscillator projects
ALL_PROJECTS=(
    mo2_va mo2_wsh mo2_fm mo2_grn mo2_add mo2_string
    mo2_wta mo2_wtb mo2_wtc mo2_wtd mo2_wte mo2_wtf
    modal_strike modal_strike_16_nolimit modal_strike_24_nolimit
    elements_full
)

# Colors (disabled if not a terminal)
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    NC='\033[0m'
else
    RED='' GREEN='' YELLOW='' NC=''
fi

##############################################################################
# Windows (Git Bash / MSYS2) detection and path helpers
#
# Problem: Git Bash auto-converts POSIX paths in command arguments.
#   /app/cmd_entry  ->  C:/Program Files/Git/app/cmd_entry  (wrong!)
#
# Fix: call `docker run` directly (not via run_cmd.sh) with:
#   - MSYS_NO_PATHCONV=1   disables all POSIX->Windows conversion
#   - volume path in //d/path form  (double slash = not converted by Git Bash)
##############################################################################

IS_WINDOWS=false
if [[ "${OSTYPE:-}" == msys* ]] || [[ "${OSTYPE:-}" == cygwin* ]] || \
   [[ "${OS:-}" == "Windows_NT" ]]; then
    IS_WINDOWS=true
fi

# Convert an absolute POSIX path to the form Docker Desktop on Windows expects.
# /d/foo/bar  ->  //d/foo/bar
# Git Bash skips paths starting with // so they reach Docker unmangled.
# Docker Desktop on Windows then maps //d/ -> D:\.
# On Linux/macOS this is a no-op (double slash at root is identical to single).
platform_path_for_docker() {
    local path="$1"
    if $IS_WINDOWS; then
        # /d/foo -> //d/foo
        echo "$path" | sed 's|^/\([a-zA-Z]\)/|//\1/|'
    else
        echo "$path"
    fi
}

##############################################################################
# Docker image detection (mirrors SDK run_cmd.sh logic)
##############################################################################

detect_image() {
    if docker image inspect "${IMAGE_NAME_DEFAULT}:${IMAGE_VERSION}" \
           >/dev/null 2>&1; then
        echo "${IMAGE_NAME_DEFAULT}:${IMAGE_VERSION}"
    elif docker image inspect "${IMAGE_NAME_FALLBACK}:${IMAGE_VERSION}" \
             >/dev/null 2>&1; then
        echo "${IMAGE_NAME_FALLBACK}:${IMAGE_VERSION}"
    else
        return 1
    fi
}

##############################################################################
# Core docker run helper
#
# Runs a single command inside the SDK container.
# Bypasses the SDK's run_cmd.sh / run_interactive.sh to handle Windows paths.
#
# Usage: sdk_docker_run [--tty] <cmd> [args...]
#   --tty   allocate a pseudo-TTY (use for interactive shells, not builds)
##############################################################################

sdk_docker_run() {
    local tty_flag=""
    if [[ "${1:-}" == "--tty" ]]; then
        tty_flag="-t"
        shift
    fi

    local image
    if ! image=$(detect_image); then
        echo -e "${RED}ERROR: Docker image not found.${NC}"
        echo "Build it first: cd logue-sdk && docker/build_image.sh"
        return 1
    fi

    # Repo root mounted as /workspace inside the container.
    # Project dirs are at /workspace/drumlogue/<project>/ and source files
    # at /workspace/ (header.c, macro-oscillator2.cc, eurorack/, etc.).
    local platform_path
    platform_path=$(platform_path_for_docker "${SCRIPT_DIR}")

    # Merge stderr into stdout so compilation errors are visible in build logs.
    if $IS_WINDOWS; then
        # MSYS_NO_PATHCONV=1  - stop Git Bash converting /app/cmd_entry etc.
        # MSYS2_ARG_CONV_EXCL="*" - belt-and-suspenders for MSYS2
        MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*" \
        docker run --rm -i ${tty_flag} \
            -v "${platform_path}:/workspace" \
            -h logue-sdk \
            "${image}" \
            "$@" 2>&1
    else
        docker run --rm -i ${tty_flag} \
            -v "${platform_path}:/workspace" \
            -h logue-sdk \
            "${image}" \
            "$@" 2>&1
    fi
}

##############################################################################
# User-facing functions
##############################################################################

usage() {
    echo "Usage: $0 [options] [project_name ...]"
    echo ""
    echo "Options:"
    echo "  --list          List available projects"
    echo "  --clean         Clean all build artifacts"
    echo "  --interactive   Enter Docker shell for manual builds"
    echo "  --collect       Collect .drmlgunit files to build/drumlogue/"
    echo "  -h, --help      Show this help"
    echo ""
    echo "If no project names given, builds all oscillators."
    echo ""
    echo "Examples:"
    echo "  $0                   # Build all"
    echo "  $0 mo2_va            # Build Virtual Analog only"
    echo "  $0 mo2_va mo2_fm     # Build VA and FM"
    echo "  $0 --collect         # Gather all .drmlgunit files"
    echo ""
    if $IS_WINDOWS; then
        echo "  (Windows / Git Bash mode active)"
    fi
}

list_projects() {
    echo "Available drumlogue oscillator projects:"
    echo ""
    echo "  Plaits-based (block size 24):"
    echo "    mo2_va       - Virtual Analog"
    echo "    mo2_wsh      - Waveshaping"
    echo "    mo2_fm       - FM Synthesis"
    echo "    mo2_grn      - Granular"
    echo "    mo2_add      - Additive"
    echo "    mo2_string   - Physical Modelling String"
    echo "    mo2_wta      - Wavetable A"
    echo "    mo2_wtb      - Wavetable B"
    echo "    mo2_wtc      - Wavetable C"
    echo "    mo2_wtd      - Wavetable D"
    echo "    mo2_wte      - Wavetable E"
    echo "    mo2_wtf      - Wavetable F"
    echo ""
    echo "  Elements-based (block size 32):"
    echo "    modal_strike             - Modal Strike (24 modes, limiter)"
    echo "    modal_strike_16_nolimit  - Modal Strike (16 modes, no limiter)"
    echo "    modal_strike_24_nolimit  - Modal Strike (24 modes, no limiter)"
    echo "    elements_full            - Elements Full (64 modes, full DSP)"
}

check_prerequisites() {
    if [ ! -d "$SDK_DIR" ]; then
        echo -e "${RED}ERROR: logue-sdk directory not found.${NC}"
        echo "Run: git submodule update --init"
        exit 1
    fi

    if [ ! -d "${SDK_DIR}/platform/drumlogue/common" ]; then
        echo -e "${RED}ERROR: logue-sdk missing drumlogue platform.${NC}"
        echo "Make sure the logue-sdk submodule is on the main branch."
        exit 1
    fi

    if ! command -v docker &> /dev/null; then
        echo -e "${RED}ERROR: Docker is not installed or not in PATH.${NC}"
        exit 1
    fi
}

build_project() {
    local project="$1"
    local project_dir="${SDK_DRUMLOGUE}/${project}"

    if [ ! -d "$project_dir" ]; then
        echo -e "${RED}ERROR: Project directory not found: ${project_dir}${NC}"
        echo "Run ./generate_sdk_projects.sh to create project directories."
        return 1
    fi

    echo -e "${YELLOW}Building ${project}...${NC}"

    # The SDK container's /app/cmd_entry expects: build drumlogue/<project>
    # We call it directly via sdk_docker_run (no TTY needed for builds).
    #
    # NOTE: The container's build_project() always returns 0 even when make
    # fails, so we cannot rely on exit codes.  Instead, check for the output
    # .drmlgunit file to determine success.
    #
    # The container runs `make -j` then `make install`.  The install target
    # MOVES the .drmlgunit from build/ to the project root, so check both.
    sdk_docker_run /app/cmd_entry build "drumlogue/${project}" || true

    local unit=""
    if [ -f "${project_dir}/${project}.drmlgunit" ]; then
        unit="${project_dir}/${project}.drmlgunit"
    elif [ -f "${project_dir}/build/${project}.drmlgunit" ]; then
        unit="${project_dir}/build/${project}.drmlgunit"
    fi

    if [ -n "$unit" ]; then
        echo -e "${GREEN}Successfully built ${project}${NC}"
        echo "  Output: ${unit}"
        return 0
    else
        echo -e "${RED}FAILED to build ${project} (no .drmlgunit output)${NC}"
        return 1
    fi
}

clean_projects() {
    echo "Cleaning build artifacts..."
    for project in "${ALL_PROJECTS[@]}"; do
        local project_dir="${SDK_DRUMLOGUE}/${project}"
        if [ -d "${project_dir}/build" ] || [ -f "${project_dir}/${project}.drmlgunit" ]; then
            echo "  Cleaning ${project}"
            rm -rf "${project_dir}/build"
            rm -rf "${project_dir}/.dep"
            rm -f "${project_dir}/${project}.drmlgunit"
        fi
    done
    if [ -d "$OUTPUT_DIR" ]; then
        rm -rf "$OUTPUT_DIR"
    fi
    echo -e "${GREEN}Done.${NC}"
}

collect_units() {
    mkdir -p "$OUTPUT_DIR"
    local count=0
    echo "Collecting .drmlgunit files to ${OUTPUT_DIR}/"
    for project in "${ALL_PROJECTS[@]}"; do
        # make install moves .drmlgunit to project root; fall back to build/
        local unit=""
        if [ -f "${SDK_DRUMLOGUE}/${project}/${project}.drmlgunit" ]; then
            unit="${SDK_DRUMLOGUE}/${project}/${project}.drmlgunit"
        elif [ -f "${SDK_DRUMLOGUE}/${project}/build/${project}.drmlgunit" ]; then
            unit="${SDK_DRUMLOGUE}/${project}/build/${project}.drmlgunit"
        fi
        if [ -n "$unit" ]; then
            cp "$unit" "${OUTPUT_DIR}/"
            echo "  ${project}.drmlgunit"
            count=$((count + 1))
        fi
    done
    echo -e "${GREEN}Collected ${count} unit files.${NC}"
}

##############################################################################
# Main
##############################################################################

if [ $# -eq 0 ]; then
    # Build all oscillators
    check_prerequisites
    echo "Building all ${#ALL_PROJECTS[@]} oscillator projects..."
    if $IS_WINDOWS; then echo "(Windows / Git Bash mode)"; fi
    echo ""
    failed=0
    for project in "${ALL_PROJECTS[@]}"; do
        if ! build_project "$project"; then
            failed=$((failed + 1))
        fi
        echo ""
    done
    echo "=========================================="
    echo "Build complete: $((${#ALL_PROJECTS[@]} - failed))/${#ALL_PROJECTS[@]} succeeded"
    if [ $failed -gt 0 ]; then
        echo -e "${RED}${failed} project(s) failed.${NC}"
        exit 1
    fi
    exit 0
fi

case "$1" in
    --list|-l)
        list_projects
        ;;
    --clean)
        clean_projects
        ;;
    --interactive)
        check_prerequisites
        echo "Entering Docker interactive shell..."
        echo "Inside the container, build with:"
        echo "  build drumlogue/<project>"
        echo "  # or: make -C drumlogue/<project>"
        echo ""
        # Interactive shell needs a real TTY.
        # On Windows Git Bash: winpty wraps docker to provide a proper TTY.
        _image=""
        if ! _image=$(detect_image); then
            echo -e "${RED}ERROR: Docker image not found.${NC}"
            echo "Build it first: cd logue-sdk && docker/build_image.sh"
            exit 1
        fi
        _platform_path=$(platform_path_for_docker "${SCRIPT_DIR}")
        if $IS_WINDOWS; then
            if command -v winpty &>/dev/null; then
                MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*" \
                winpty docker run --rm -it \
                    -v "${_platform_path}:/workspace" \
                    -h logue-sdk \
                    "${_image}" \
                    /app/interactive_entry
            else
                echo "Note: winpty not found. If the shell appears broken, install winpty"
                echo "or use Docker Desktop's terminal directly."
                MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*" \
                docker run --rm -it \
                    -v "${_platform_path}:/workspace" \
                    -h logue-sdk \
                    "${_image}" \
                    /app/interactive_entry
            fi
        else
            docker run --rm -it \
                -v "${_platform_path}:/workspace" \
                -h logue-sdk \
                "${_image}" \
                /app/interactive_entry
        fi
        ;;
    --collect)
        collect_units
        ;;
    -h|--help)
        usage
        ;;
    *)
        # Build specified project(s)
        check_prerequisites
        if $IS_WINDOWS; then echo "(Windows / Git Bash mode)"; fi
        failed=0
        for project in "$@"; do
            if [[ "$project" == -* ]]; then
                echo -e "${RED}Error: Unknown option or option in wrong position: $project${NC}" >&2
                usage
                exit 1
            fi
            if ! build_project "$project"; then
                failed=$((failed + 1))
            fi
            echo ""
        done
        if [ $failed -gt 0 ]; then
            echo -e "${RED}${failed} project(s) failed.${NC}"
            exit 1
        fi
        ;;
esac
