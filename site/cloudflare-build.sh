#!/bin/bash
# cloudflare's build (workers builds, root site/): fetches emscripten, cmake, ninja and dear
# imgui into .tools/ (git-ignored), then build.sh compiles the gallery to wasm and builds the
# site. versions are pinned here; keep IMGUI_COMMIT with dearsql's external/imgui
set -euo pipefail
cd "$(dirname "$0")"
EMSCRIPTEN_VERSION=7.0.0
IMGUI_COMMIT=b916186e66aa3c02b72c6ca17455516512ed8049
TOOLS="$PWD/.tools"
mkdir -p "$TOOLS"

if ! command -v cmake >/dev/null || ! command -v ninja >/dev/null; then
    python3 -m pip install --quiet --user cmake ninja
    export PATH="$HOME/.local/bin:$PATH"
fi

[ -d "$TOOLS/emsdk" ] || git clone --quiet --depth 1 https://github.com/emscripten-core/emsdk.git "$TOOLS/emsdk"
"$TOOLS/emsdk/emsdk" install "$EMSCRIPTEN_VERSION"
"$TOOLS/emsdk/emsdk" activate "$EMSCRIPTEN_VERSION" >/dev/null
source "$TOOLS/emsdk/emsdk_env.sh" >/dev/null 2>&1

if [ ! -f "$TOOLS/imgui/imgui.h" ]; then
    rm -rf "$TOOLS/imgui"
    git init --quiet "$TOOLS/imgui"
    git -C "$TOOLS/imgui" fetch --quiet --depth 1 https://github.com/ocornut/imgui.git "$IMGUI_COMMIT"
    git -C "$TOOLS/imgui" checkout --quiet FETCH_HEAD
fi

MYDEAR_IMGUI_DIR="$TOOLS/imgui" sh ./build.sh "$TOOLS/build-web"
