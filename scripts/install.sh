#!/bin/bash
# Install Granny Grain module to Move
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

cd "$REPO_ROOT"

if [ ! -d "dist/granny-grain" ]; then
    echo "Error: dist/granny-grain not found. Run ./scripts/build.sh first."
    exit 1
fi

echo "=== Installing Granny Grain Module ==="

echo "Copying module to Move..."
ssh ableton@move.local "mkdir -p /data/UserData/schwung/modules/sound_generators/granny-grain"
scp -r dist/granny-grain/* ableton@move.local:/data/UserData/schwung/modules/sound_generators/granny-grain/

echo "Setting permissions..."
ssh ableton@move.local "chmod -R a+rw /data/UserData/schwung/modules/sound_generators/granny-grain"

echo ""
echo "=== Install Complete ==="
echo "Module installed to: /data/UserData/schwung/modules/sound_generators/granny-grain/"
echo ""
echo "Restart Schwung to load the new module."
