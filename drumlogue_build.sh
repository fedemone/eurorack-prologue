#!/bin/bash
# Drumlogue Build Script for Windows WSL2
# Usage: ./drumlogue_build.sh [command] [project]
# Example: ./drumlogue_build.sh build drumlogue/dummy-synth

cd "$(dirname "$0")" || exit

COMMAND="${1:-build}"
PROJECT="${2:-drumlogue/dummy-synth}"

echo "================================"
echo "Drumlogue Build System"
echo "================================"
echo "Command: $COMMAND"
echo "Project: $PROJECT"
echo ""

# Navigate to logue-sdk/docker
cd logue-sdk/docker || exit 1

# Run the command
case "$COMMAND" in
    -l|list)
        ./run_cmd.sh build -l --drumlogue
        ;;
    -ba|build-all)
        ./run_cmd.sh build --drumlogue
        ;;
    -c|clean)
        ./run_cmd.sh build --clean "$PROJECT"
        ;;
    -i|interactive)
        ./run_interactive.sh
        ;;
    -bi|build-image)
        ./build_image.sh
        ;;
    *)
        ./run_cmd.sh build "$PROJECT"
        ;;
esac

