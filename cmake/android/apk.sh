#!/bin/sh
# packs a native library into a signed apk without gradle: javac, d8, aapt2, zipalign, apksigner
# usage: apk.sh <sdk> <work dir> <manifest> <lib.so> <out.apk> <assets dir or -> <java files...>
set -eu
sdk=$1 work=$2 manifest=$3 lib=$4 out=$5 assets=$6
shift 6

tools=$(ls -d "$sdk"/build-tools/*/ | sort -V | tail -1)
platform=$(ls -d "$sdk"/platforms/android-*/ | sort -V | tail -1)
jar="$platform/android.jar"

rm -rf "$work/classes" "$work/stage"
mkdir -p "$work/classes" "$work/stage/lib/arm64-v8a"
javac -nowarn --release 11 -classpath "$jar" -d "$work/classes" "$@"
(cd "$work/classes" && jar cf ../classes.jar .)
"$tools/d8" --release --min-api 28 --lib "$jar" --output "$work/stage" "$work/classes.jar"
cp "$lib" "$work/stage/lib/arm64-v8a/"

set -- link -o "$work/base.apk" --manifest "$manifest" -I "$jar"
[ "$assets" != "-" ] && set -- "$@" -A "$assets"
# debug builds: run-as and a debugger can reach the app
[ "${IMSHELL_APK_DEBUG:-0}" = 1 ] && set -- "$@" --debug-mode
"$tools/aapt2" "$@"
(cd "$work/stage" && zip -qr "$work/base.apk" classes.dex lib)
"$tools/zipalign" -f -p 4 "$work/base.apk" "$work/aligned.apk"

# the android sdk's debug key, or one of our own next to the build
ks="$HOME/.android/debug.keystore"
if [ ! -f "$ks" ]; then
    ks="$work/debug.keystore"
    [ -f "$ks" ] || keytool -genkeypair -keystore "$ks" -storepass android -keypass android \
        -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 \
        -dname "CN=Android Debug,O=Android,C=US" >/dev/null 2>&1
fi
"$tools/apksigner" sign --ks "$ks" --ks-pass pass:android --out "$out" "$work/aligned.apk"
echo "apk: $out"
