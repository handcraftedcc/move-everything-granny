#!/bin/bash
# Install Granny Grain module to Move
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

cd "$REPO_ROOT"

if [ ! -d "dist/granny" ]; then
    echo "Error: dist/granny not found. Run ./scripts/build.sh first."
    exit 1
fi

echo "=== Installing Granny Grain Module ==="

echo "Copying module to Move..."
ssh ableton@move.local "mkdir -p /data/UserData/move-anything/modules/sound_generators/granny"
scp -r dist/granny/* ableton@move.local:/data/UserData/move-anything/modules/sound_generators/granny/

echo "Setting permissions..."
ssh ableton@move.local "chmod -R a+rw /data/UserData/move-anything/modules/sound_generators/granny"

echo ""
echo "=== Install Complete ==="
echo "Module installed to: /data/UserData/move-anything/modules/sound_generators/granny/"
echo ""
echo "Restart Move Anything to load the new module."
