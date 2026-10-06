#!/usr/bin/env bash
# Builds the desktop and web versions in release mode and gathers them into dist/
# Needs emsdk set up in ./emsdk (see emsdk's README) and a native C compiler for the desktop build
set -euo pipefail
cd "$(dirname "$0")"

generator=()
if command -v ninja >/dev/null; then
    generator=(-G Ninja)
fi

echo "== Desktop build"
cmake -S . -B build/desktop "${generator[@]}" -DCMAKE_BUILD_TYPE=Release
cmake --build build/desktop --config Release --parallel

echo "== Web build"
(
    # Kept in a subshell so emsdk's environment doesn't leak into anything else
    source ./emsdk/emsdk_env.sh >/dev/null 2>&1
    emcmake cmake -S . -B build/web "${generator[@]}" -DCMAKE_BUILD_TYPE=Release
    cmake --build build/web --parallel
)

echo "== Copying to dist/"
# Emptied rather than deleted, since Windows won't remove a folder a program (like a local server) has open
mkdir -p dist
rm -rf dist/*
cp build/web/index.html build/web/index.js build/web/index.wasm dist/
# Roboto's license has to travel with the font, which is embedded in both builds
cp OFL.txt dist/
# MSVC puts the .exe in a Release/ subfolder; other generators don't
cp "$(find build/desktop -maxdepth 2 -name 'fill-a-canvas.exe' | head -n 1)" dist/

ls -l dist
