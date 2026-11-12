#!/bin/sh
# embed-sparkle.sh <App.app> <Sparkle.framework> [identity]: copies Sparkle into the bundle's
# Frameworks and signs its parts inside out, as Sparkle's docs ask. identity: a "Developer ID
# Application: ..." keychain identity (hardened runtime, timestamped, for notarizing); none or
# "-": ad hoc. sign the app itself after this. the exe finds it through the rpath that
# imshell_updater_runtime() adds
set -e
app=$1 framework=$2 identity=${3:--}
[ -d "$app" ] && [ -d "$framework" ] || { echo "usage: $0 <App.app> <Sparkle.framework> [identity]" >&2; exit 1; }
fw="$app/Contents/Frameworks/Sparkle.framework"
mkdir -p "$app/Contents/Frameworks"
rm -rf "$fw"
ditto "$framework" "$fw"
if [ "$identity" = "-" ]; then
    sign() { codesign --force -s - "$@"; }
else
    sign() { codesign --force --timestamp --options runtime -s "$identity" "$@"; }
fi
sign "$fw/Versions/B/XPCServices/Installer.xpc"
sign --preserve-metadata=entitlements "$fw/Versions/B/XPCServices/Downloader.xpc"
sign "$fw/Versions/B/Autoupdate"
sign "$fw/Versions/B/Updater.app"
sign "$fw"
