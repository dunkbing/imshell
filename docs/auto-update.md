# Auto-update

`imshell/updater.hpp` keeps an installed app up to date. How an app updates depends on how it was installed, so imshell uses the mechanism that owns each format: Sparkle for a macOS `.app`, WinSparkle for a Windows install, and an in-place swap for a Linux AppImage. A package from a package manager (DEB, RPM, Flatpak, the App Store) is updated by its manager, never by the app.

```cpp
#include "imshell/updater.hpp"

// once, after App::create
imshell::updater::Start({
    .appName = "My App",
    .version = "1.4.2",
    .appcastUrl = "https://example.com/appcast.xml",     // windows
    .publicKey = "<base64 ed25519 public key>",          // windows
    .manifestUrl = "https://example.com/version.json",   // linux
});

// a "Check for Updates..." menu item
imshell::updater::Check();

// before exit
imshell::updater::Stop();
```

`Start()` checks quietly in the background. `Check()` is the check a user asked for: it says what it found ("up to date", or the new version with its notes), offers to download it, and offers to restart afterwards. On Linux `Available()` and `LatestVersion()` report what the quiet check found, so the app can show an update button of its own.

## Turning it on

The updater is off by default, so imshell needs nothing extra. Configure with `-DIMSHELL_UPDATER=ON` and hand the app's target to `imshell_app`, which ships the updater with it:

```cmake
set(IMSHELL_UPDATER ON CACHE BOOL "" FORCE) # before add_subdirectory(imshell)
add_subdirectory(external/imshell)
add_executable(app ...)
imshell_app(app NAME App ID com.example.app VERSION 1.2.0
            APPCAST https://example.com/appcast.xml PUBLIC_KEY <ed25519 public key>)
```

- **macOS:** imshell downloads Sparkle 2.9.1 at configure time (hash-checked); set `IMSHELL_SPARKLE_FRAMEWORK` to use your own `Sparkle.framework` instead. `cmake --build <dir> --target app-app` makes `App.app` and `app-macos-<arch>.zip` in the build folder: the exe with its build-tree rpaths replaced by the bundle's `Frameworks`, an `Info.plist` (with `SUFeedURL` and `SUPublicEDKey` from `APPCAST` and `PUBLIC_KEY`; Sparkle doesn't start without the key), Sparkle embedded and signed inside out, then the app. It signs ad hoc unless `IMSHELL_SIGN_IDENTITY` names a Developer ID identity (hardened runtime, timestamped), and notarizes and staples when the environment has `IMSHELL_NOTARY_PROFILE` (a `notarytool` keychain profile) or `IMSHELL_NOTARY_KEY` (an App Store Connect `.p8`) with `IMSHELL_NOTARY_KEY_ID` and `IMSHELL_NOTARY_ISSUER`. Credentials are read when it runs, never stored in the cache. The dev exe in the build folder keeps running unbundled.
- **Windows:** imshell downloads WinSparkle 0.9.2 (hash-checked) unless the host already defines a `WinSparkle` target; `imshell_app` copies `WinSparkle.dll` next to the exe after each build. Ship it with the app. The feed and key go to `Start` (`appcastUrl`, `publicKey`).
- **Linux:** imshell finds `httplib`, `nlohmann_json` and `OpenSSL` with `find_package` (vcpkg has all three); the feed goes to `Start` (`manifestUrl`).
- **iOS:** always a no-op; the App Store updates the app.

## What each platform does

| | Feed | Verified by | Installed by | Prompts |
|---|---|---|---|---|
| macOS | Sparkle appcast (`SUFeedURL`) | Sparkle: ed25519 signature, code signature | Sparkle, replacing the whole `.app` | Sparkle's own windows, on its schedule |
| Windows | Sparkle appcast (`appcastUrl`) | WinSparkle: ed25519 signature (`publicKey`) | the downloaded installer (an MSI, for instance) | WinSparkle's own windows |
| Linux | JSON manifest (`manifestUrl`) | SHA-256 from the manifest, before anything is replaced | imshell: rename over the AppImage, then exec it | GTK dialogs, only after `Check()` |

macOS and Windows share one appcast: publish each release once, signed with one ed25519 key (Sparkle's `generate_keys` and `sign_update`).

## The Linux manifest

```json
{
  "version": "1.4.3",
  "release_notes": "Faster startup.",
  "downloads": {
    "appimage-x86_64": { "url": "https://example.com/MyApp-1.4.3-x86_64.AppImage", "sha256": "<64 hex digits>" },
    "appimage-aarch64": { "url": "https://example.com/MyApp-1.4.3-aarch64.AppImage", "sha256": "<64 hex digits>" }
  }
}
```

imshell picks the entry for the CPU it was built for (`appimage-x86_64` or `appimage-aarch64`; `Config::assetKey` overrides it), with no fallback, since another architecture's binary won't run. Versions compare as dotted numbers; a part that isn't a number counts as 0, so `1.4.3-beta` is `1.4.3`.

An update goes like this:

1. Download next to the AppImage (`<file>.update`), so the final rename stays on one filesystem.
2. Check its SHA-256 against the manifest. A mismatch, a failed download or a cancel deletes the file; the installed AppImage is never touched.
3. Mark it executable and rename it over the running AppImage. The running process keeps its old file open, so nothing breaks until the restart.
4. Ask to restart; **Restart Now** execs the new AppImage in place of the running process.

Only an AppImage (`$APPIMAGE` set) updates itself. Run any other way, `Check()` explains that the package manager or a fresh download updates it, and `Available()` still reports a newer version.

The SHA-256 catches a broken or swapped download from the manifest's host, but it doesn't prove who published the release: anyone who can change the manifest can change the hash. Serve the manifest over HTTPS from a host you control.

## Restart and unsaved work

macOS and Windows: Sparkle and WinSparkle ask the app to quit before installing (on Windows by closing the main window, so `Delegate::closeRequested()` can still ask about unsaved work). An MSI runs without its wizard when the appcast's enclosure has `sparkle:installerArguments="/passive /norestart"`; `/norestart` matters, since a locked file otherwise lets `/passive` restart the machine. Add it only for a release whose previous version already closes itself, or the old app still holds the exe while the installer runs. WinSparkle starts the installer before it asks the app to quit, so a host whose `closeRequested()` can return false (to ask about unsaved work) keeps the wizard: a silent MSI would run while the app waits on the user. On Linux the restart happens only when the user clicks **Restart Now**. `execl` replaces the process without running destructors, so an app with unsaved state should save it when the window loses focus or on a timer, not only at exit.
