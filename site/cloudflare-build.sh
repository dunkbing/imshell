#!/bin/bash
# cloudflare's build (workers builds, root site/): fetches emscripten, cmake and ninja into
# .tools/ (git-ignored) and the imgui submodule (pinned by the repo), then build.sh compiles the
# gallery to wasm and builds the site
set -euo pipefail
cd "$(dirname "$0")"
EMSCRIPTEN_VERSION=7.0.0
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

git -C .. submodule update --init --depth 1 external/imgui
sh ./build.sh "$TOOLS/build-web"
