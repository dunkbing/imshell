#!/bin/sh
# builds the site into site/dist: the astro pages plus the components gallery compiled to
# webassembly (into public/gallery). needs emscripten (emcmake), bun and MYDEAR_IMGUI_DIR
# usage: MYDEAR_IMGUI_DIR=/path/to/imgui site/build.sh [wasm-build-dir]
set -eu
cd "$(dirname "$0")/.."
: "${MYDEAR_IMGUI_DIR:?set MYDEAR_IMGUI_DIR to a dear imgui checkout}"
BUILD=${1:-build-web}
emcmake cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DMYDEAR_IMGUI_DIR="$MYDEAR_IMGUI_DIR" -DMYDEAR_BUILD_EXAMPLES=ON >/dev/null
cmake --build "$BUILD" --target mydear_gallery
mkdir -p site/public/gallery
cp "$BUILD"/mydear_gallery.html "$BUILD"/mydear_gallery.js "$BUILD"/mydear_gallery.wasm site/public/gallery/
cd site
bun install --frozen-lockfile
bun run build
echo "site/dist: serve it with any static file server (bun run preview)"
