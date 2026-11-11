#!/usr/bin/env bash
# measures the examples on this mac and the web gallery as served, for the site's performance
# section; writes src/data/benchmarks.json. needs an unlocked screen: mydear draws nothing in a
# window that cannot have focus.
# usage: site/bench.sh <release build dir with mydear_hello and mydear_gallery> [site url]
set -euo pipefail

BUILD=$(cd "${1:?usage: bench.sh <release build dir> [site url]}" && pwd)
URL=${2:-https://mydear-d0o.pages.dev}
SITE=$(cd "$(dirname "$0")" && pwd)
OUT="$SITE/src/data/benchmarks.json"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

[ "$(uname)" = Darwin ] || { echo "bench.sh: macOS only" >&2; exit 1; }
now() { perl -MTime::HiRes=time -e 'printf "%.6f", time'; }
mb() { awk -v b="$1" 'BEGIN { printf "%.1f", b / 1048576 }'; }

# size: the stripped executable (dear imgui, mydear and glfw linked in statically)
size() {
    strip -x -o "$TMP/$1" "$BUILD/$1"
    mb "$(stat -f %z "$TMP/$1")"
}

# launch to first frame: hello quits right after drawing it; the median of 11 runs
firstFrame() {
    local runs=() t0 t1 out
    for _ in $(seq 11); do
        t0=$(now)
        out=$(MYDEAR_HELLO_QUIT=0 "$BUILD/mydear_hello" 2>/dev/null)
        t1=$(now)
        case "$out" in *"hello: 0 frames"* | "") echo "bench.sh: no frame drawn (screen locked?)" >&2; exit 1 ;; esac
        runs+=("$(awk -v a="$t0" -v b="$t1" 'BEGIN { printf "%.0f", (b - a) * 1000 }')")
    done
    printf '%s\n' "${runs[@]}" | sort -n | sed -n 6p
}

# at rest: the physical footprint 3 s after launch, then the cpu used over the next 10 s (the
# gallery never idles: its skeleton and progress bar redraw every frame)
cputime() { ps -o time= -p "$1" | awk -F'[:.]' '{ printf "%.2f", $1 * 60 + $2 + $3 / 100 }'; }
rest() {
    "$BUILD/$1" >/dev/null 2>&1 &
    local pid=$! fp before after
    sleep 3
    fp=$(footprint "$pid" | sed -nE 's/.*Footprint: ([0-9.]+) (KB|MB|GB).*/\1 \2/p' | head -1 |
        awk '{ print ($2 == "KB" ? $1 / 1024 : $2 == "GB" ? $1 * 1024 : $1) }')
    before=$(cputime "$pid")
    sleep 10
    after=$(cputime "$pid")
    kill "$pid"
    wait "$pid" 2>/dev/null || true
    awk -v f="$fp" -v a="$before" -v b="$after" 'BEGIN { printf "%.1f %.1f", f, (b - a) / 10 * 100 }'
}

# the web gallery as a browser downloads it (brotli)
transfer() {
    curl -sf -H 'Accept-Encoding: br' -o /dev/null -w '%{size_download}' "$URL/gallery/$1"
}

helloSize=$(size mydear_hello)
gallerySize=$(size mydear_gallery)
frameMs=$(firstFrame)
read -r helloMem helloCpu <<<"$(rest mydear_hello)"
read -r galleryMem galleryCpu <<<"$(rest mydear_gallery)"
wasm=$(transfer mydear_gallery.wasm)
js=$(transfer mydear_gallery.js)

mkdir -p "$(dirname "$OUT")"
cat >"$OUT" <<EOF
{
  "date": "$(date +%F)",
  "commit": "$(git -C "$SITE" rev-parse --short HEAD)",
  "machine": "$(sysctl -n machdep.cpu.brand_string), $(($(sysctl -n hw.memsize) / 1073741824)) GB",
  "os": "macOS $(sw_vers -productVersion)",
  "hello": { "sizeMB": $helloSize, "firstFrameMs": $frameMs, "memoryMB": $helloMem, "idleCpu": $helloCpu },
  "gallery": { "sizeMB": $gallerySize, "memoryMB": $galleryMem, "animatingCpu": $galleryCpu },
  "web": { "wasmKB": $((wasm / 1024)), "jsKB": $((js / 1024)) }
}
EOF
cat "$OUT"
