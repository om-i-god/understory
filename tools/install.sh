#!/bin/sh
# Build the plugin and install it for the current user. Restart OBS afterwards.
set -e
cd "$(dirname "$0")/.."
[ -d deps/obs-studio ] || sh tools/fetch-deps.sh
cmake -S . -B build-obs -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build-obs -j
DEST="$HOME/Library/Application Support/obs-studio/plugins"
mkdir -p "$DEST"
rm -rf "$DEST/understory.plugin"
cp -R build-obs/understory.plugin "$DEST/"
echo "installed -> $DEST/understory.plugin  (restart OBS, then + > Understory (living nature))"
