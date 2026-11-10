#!/bin/sh
# builds the site into site/dist: the static pages plus the components gallery compiled to
# webassembly. needs emscripten (emcmake) and MYDEAR_IMGUI_DIR (a dear imgui checkout)
# usage: MYDEAR_IMGUI_DIR=/path/to/imgui site/build.sh [build-dir]
set -eu
cd "$(dirname "$0")/.."
: "${MYDEAR_IMGUI_DIR:?set MYDEAR_IMGUI_DIR to a dear imgui checkout}"
BUILD=${1:-build-web}
emcmake cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DMYDEAR_IMGUI_DIR="$MYDEAR_IMGUI_DIR" -DMYDEAR_BUILD_EXAMPLES=ON >/dev/null
cmake --build "$BUILD" --target mydear_gallery
rm -rf site/dist && mkdir -p site/dist/gallery
cp site/index.html site/components.html site/style.css site/dist/
cp "$BUILD"/mydear_gallery.html "$BUILD"/mydear_gallery.js "$BUILD"/mydear_gallery.wasm site/dist/gallery/
echo "site/dist: serve it with any static file server"
