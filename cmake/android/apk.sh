#!/bin/sh
# packs a native library into a signed apk, or a play store app bundle, without gradle:
# javac, d8, aapt2, zipalign, apksigner (apk) / bundletool, jarsigner (aab)
# usage: apk.sh <sdk> <work dir> <manifest> <lib.so> <out.apk|out.aab> <assets dir or -> <java files...>
# env: IMSHELL_APK_DEBUG=1 debuggable; IMSHELL_APK_ICON launcher png; IMSHELL_APK_FORMAT=aab for a bundle
#      (bundletool on PATH, or BUNDLETOOL); IMSHELL_ANDROID_KEYSTORE, _KEY_ALIAS, _KEYSTORE_PASS, _KEY_PASS
#      sign with that key (passwords are read from the environment, never from the command line)
set -eu
sdk=$1 work=$2 manifest=$3 lib=$4 out=$5 assets=$6
shift 6
format=${IMSHELL_APK_FORMAT:-apk}

tools=$(ls -d "$sdk"/build-tools/*/ | sort -V | tail -1)
platform=$(ls -d "$sdk"/platforms/android-*/ | sort -V | tail -1)
jar="$platform/android.jar"

rm -rf "$work/classes" "$work/stage" "$work/res" "$work/module"
mkdir -p "$work/classes" "$work/stage/lib/arm64-v8a"
javac -nowarn --release 11 -classpath "$jar" -d "$work/classes" "$@"
(cd "$work/classes" && jar cf ../classes.jar .)
"$tools/d8" --release --min-api 28 --lib "$jar" --output "$work/stage" "$work/classes.jar"
cp "$lib" "$work/stage/lib/arm64-v8a/"

set -- link --manifest "$manifest" -I "$jar"
[ "$assets" != "-" ] && set -- "$@" -A "$assets"
# debug builds: run-as and a debugger can reach the app
[ "${IMSHELL_APK_DEBUG:-0}" = 1 ] && set -- "$@" --debug-mode
if [ -n "${IMSHELL_APK_ICON:-}" ]; then
    mkdir -p "$work/res/mipmap-xxxhdpi"
    cp "$IMSHELL_APK_ICON" "$work/res/mipmap-xxxhdpi/ic_launcher.png"
    "$tools/aapt2" compile --dir "$work/res" -o "$work/res.zip"
    set -- "$@" "$work/res.zip"
fi

if [ -n "${IMSHELL_ANDROID_KEYSTORE:-}" ]; then
    ks=$IMSHELL_ANDROID_KEYSTORE alias=${IMSHELL_ANDROID_KEY_ALIAS:?set IMSHELL_ANDROID_KEY_ALIAS}
    : "${IMSHELL_ANDROID_KEYSTORE_PASS:?set IMSHELL_ANDROID_KEYSTORE_PASS}"
    IMSHELL_ANDROID_KEY_PASS=${IMSHELL_ANDROID_KEY_PASS:-$IMSHELL_ANDROID_KEYSTORE_PASS}
    export IMSHELL_ANDROID_KEYSTORE_PASS IMSHELL_ANDROID_KEY_PASS
elif [ "$format" = aab ]; then
    echo "apk.sh: an app bundle is signed with your upload key: set IMSHELL_ANDROID_KEYSTORE" >&2
    exit 1
fi

if [ "$format" = aab ]; then
    # a bundle module: proto resources, then dex/, lib/, assets/ and manifest/ in fixed places
    "$tools/aapt2" "$@" --proto-format -o "$work/proto.apk"
    mkdir -p "$work/module/manifest" "$work/module/dex"
    (cd "$work/module" && unzip -q "$work/proto.apk")
    mv "$work/module/AndroidManifest.xml" "$work/module/manifest/"
    cp "$work/stage/classes.dex" "$work/module/dex/"
    cp -R "$work/stage/lib" "$work/module/"
    rm -f "$work/base.zip"
    (cd "$work/module" && zip -qr "$work/base.zip" .)
    rm -f "$out"
    ${BUNDLETOOL:-bundletool} build-bundle --modules="$work/base.zip" --output="$out"
    jarsigner -keystore "$ks" -storepass:env IMSHELL_ANDROID_KEYSTORE_PASS -keypass:env IMSHELL_ANDROID_KEY_PASS \
        -sigalg SHA256withRSA -digestalg SHA-256 "$out" "$alias" >/dev/null
    echo "aab: $out"
    exit 0
fi

"$tools/aapt2" "$@" -o "$work/base.apk"
(cd "$work/stage" && zip -qr "$work/base.apk" classes.dex lib)
"$tools/zipalign" -f -p 4 "$work/base.apk" "$work/aligned.apk"

if [ -n "${IMSHELL_ANDROID_KEYSTORE:-}" ]; then
    "$tools/apksigner" sign --ks "$ks" --ks-key-alias "$alias" --ks-pass env:IMSHELL_ANDROID_KEYSTORE_PASS \
        --key-pass env:IMSHELL_ANDROID_KEY_PASS --out "$out" "$work/aligned.apk"
else
    # the android sdk's debug key, or one of our own next to the build
    ks="$HOME/.android/debug.keystore"
    if [ ! -f "$ks" ]; then
        ks="$work/debug.keystore"
        [ -f "$ks" ] || keytool -genkeypair -keystore "$ks" -storepass android -keypass android \
            -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 \
            -dname "CN=Android Debug,O=Android,C=US" >/dev/null 2>&1
    fi
    "$tools/apksigner" sign --ks "$ks" --ks-pass pass:android --out "$out" "$work/aligned.apk"
fi
echo "apk: $out"
