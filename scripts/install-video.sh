#!/bin/sh
# installs imshell-video (render a scene.js to mp4, no compiler needed):
#   curl -fsSL https://raw.githubusercontent.com/dunkbing/imshell/main/scripts/install-video.sh | sh
# IMSHELL_VIDEO_VERSION=video-vX.Y picks a release (default: the latest), IMSHELL_BIN the folder (default ~/.local/bin)
set -eu
repo=dunkbing/imshell
bin=${IMSHELL_BIN:-$HOME/.local/bin}

case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=macos-arm64 ;;
    Linux-x86_64) target=linux-x64 ;;
    *) echo "no prebuilt imshell-video for $(uname -s) $(uname -m) (macOS arm64, Linux x64 and Windows x64 are built); see docs/video.md to build one" >&2; exit 1 ;;
esac
file=imshell-video-$target.tar.gz
if [ -n "${IMSHELL_VIDEO_BASE_URL:-}" ]; then
    base=$IMSHELL_VIDEO_BASE_URL # a mirror, or a local folder (file://) for testing
elif [ -n "${IMSHELL_VIDEO_VERSION:-}" ]; then
    base=https://github.com/$repo/releases/download/$IMSHELL_VIDEO_VERSION
else
    base=https://github.com/$repo/releases/latest/download
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
echo "downloading $file"
curl -fsSL "$base/$file" -o "$tmp/$file"
curl -fsSL "$base/SHA256SUMS" -o "$tmp/SHA256SUMS"
expected=$(grep " $file\$" "$tmp/SHA256SUMS" | cut -d' ' -f1)
[ -n "$expected" ] || { echo "no checksum for $file in SHA256SUMS" >&2; exit 1; }
if command -v sha256sum >/dev/null 2>&1; then actual=$(sha256sum "$tmp/$file" | cut -d' ' -f1); else actual=$(shasum -a 256 "$tmp/$file" | cut -d' ' -f1); fi
[ "$actual" = "$expected" ] || { echo "checksum mismatch for $file" >&2; exit 1; }

tar xzf "$tmp/$file" -C "$tmp"
mkdir -p "$bin"
cp "$tmp/imshell-video/imshell-video" "$bin/imshell-video"
chmod +x "$bin/imshell-video"
echo "installed $bin/imshell-video"
case ":$PATH:" in *":$bin:"*) ;; *) echo "add $bin to your PATH" ;; esac
command -v ffmpeg >/dev/null 2>&1 || {
    [ "$target" = linux-x64 ] && echo "note: Linux renders through ffmpeg; install it (apt install ffmpeg)" || echo "note: ffmpeg is only needed to read video footage or write webm and prores"
}
echo "try: imshell-video api"
