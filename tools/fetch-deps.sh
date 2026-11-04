#!/bin/sh
# Fetch build-time headers for the OBS plugin into deps/ (not committed).
# OBS_VERSION should match the OBS you run (OBS.app > About).
set -e
cd "$(dirname "$0")/.."
OBS_VERSION="${OBS_VERSION:-32.2.2}"
mkdir -p deps
if [ ! -d deps/obs-studio ]; then
  git clone -q --depth 1 --branch "$OBS_VERSION" --filter=blob:none --sparse https://github.com/obsproject/obs-studio.git deps/obs-studio
  git -C deps/obs-studio sparse-checkout set libobs cmake
fi
[ -d deps/simde ] || git clone -q --depth 1 --branch v0.8.2 https://github.com/simd-everywhere/simde.git deps/simde
echo "deps ready (obs-studio $OBS_VERSION headers + simde)"
