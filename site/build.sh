#!/bin/sh
# builds the site into site/dist: the astro pages plus the components gallery compiled to
# webassembly (into public/gallery). needs emscripten (emcmake), bun and IMSHELL_IMGUI_DIR
# usage: IMSHELL_IMGUI_DIR=/path/to/imgui site/build.sh [wasm-build-dir]
set -eu
cd "$(dirname "$0")/.."
: "${IMSHELL_IMGUI_DIR:?set IMSHELL_IMGUI_DIR to a dear imgui checkout}"
BUILD=${1:-build-web}
emcmake cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DIMSHELL_IMGUI_DIR="$IMSHELL_IMGUI_DIR" -DIMSHELL_BUILD_EXAMPLES=ON >/dev/null
cmake --build "$BUILD" --target imshell_gallery
mkdir -p site/public/gallery
cp "$BUILD"/imshell_gallery.html "$BUILD"/imshell_gallery.js "$BUILD"/imshell_gallery.wasm site/public/gallery/
cd site
bun install --frozen-lockfile
bun run build
echo "site/dist: serve it with any static file server (bun run preview)"
