#!/bin/bash
#
# build_drumlogue.sh
#
# Convenience wrapper for building drumlogue oscillator units using
# the official logue-sdk Docker build system.
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
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_DIR="${SCRIPT_DIR}/logue-sdk"
SDK_DRUMLOGUE="${SDK_DIR}/platform/drumlogue"
DOCKER_DIR="${SDK_DIR}/docker"
OUTPUT_DIR="${SCRIPT_DIR}/build/drumlogue"

# All available oscillator projects
ALL_PROJECTS=(
    mo2_va mo2_wsh mo2_fm mo2_grn mo2_add mo2_string
    mo2_wta mo2_wtb mo2_wtc mo2_wtd mo2_wte mo2_wtf
    modal_strike modal_strike_16_nolimit modal_strike_24_nolimit
)

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

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
}

check_prerequisites() {
    if [ ! -d "$SDK_DIR" ]; then
        echo -e "${RED}ERROR: logue-sdk directory not found.${NC}"
        echo "Run: git submodule update --init"
        exit 1
    fi

    if [ ! -d "${SDK_DRUMLOGUE}/common" ]; then
        echo -e "${RED}ERROR: logue-sdk missing drumlogue platform.${NC}"
        echo "Make sure the logue-sdk submodule is on the main branch."
        exit 1
    fi

    if [ ! -f "${DOCKER_DIR}/run_cmd.sh" ]; then
        echo -e "${RED}ERROR: Docker scripts not found in logue-sdk/docker/.${NC}"
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

    # Use the SDK Docker build command
    cd "$SDK_DIR"
    docker/run_cmd.sh build "drumlogue/${project}"
    local rc=$?

    if [ $rc -eq 0 ]; then
        echo -e "${GREEN}Successfully built ${project}${NC}"
        # Check for output file
        if [ -f "${project_dir}/build/${project}.drmlgunit" ]; then
            echo "  Output: ${project_dir}/build/${project}.drmlgunit"
        fi
    else
        echo -e "${RED}FAILED to build ${project} (exit code: ${rc})${NC}"
    fi

    cd "$SCRIPT_DIR"
    return $rc
}

clean_projects() {
    echo "Cleaning build artifacts..."
    for project in "${ALL_PROJECTS[@]}"; do
        local project_dir="${SDK_DRUMLOGUE}/${project}"
        if [ -d "${project_dir}/build" ]; then
            echo "  Cleaning ${project}"
            rm -rf "${project_dir}/build"
            rm -rf "${project_dir}/.dep"
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
        local unit="${SDK_DRUMLOGUE}/${project}/build/${project}.drmlgunit"
        if [ -f "$unit" ]; then
            cp "$unit" "${OUTPUT_DIR}/"
            echo "  ${project}.drmlgunit"
            count=$((count + 1))
        fi
    done
    echo -e "${GREEN}Collected ${count} unit files.${NC}"
}

# Parse arguments
if [ $# -eq 0 ]; then
    # Build all
    check_prerequisites
    echo "Building all ${#ALL_PROJECTS[@]} oscillator projects..."
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
        echo "Inside the container, build with: make -C platform/drumlogue/<project>"
        cd "$SDK_DIR"
        docker/run_interactive.sh
        ;;
    --collect)
        collect_units
        ;;
    -h|--help)
        usage
        ;;
    *)
        # Build specified projects
        check_prerequisites
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
