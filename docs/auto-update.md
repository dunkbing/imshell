# Auto-update

`mydear/updater.hpp` keeps an installed app up to date. How an app updates depends on how it was installed, so mydear uses the mechanism that owns each format: Sparkle for a macOS `.app`, WinSparkle for a Windows install, and an in-place swap for a Linux AppImage. A package from a package manager (DEB, RPM, Flatpak, the App Store) is updated by its manager, never by the app.

```cpp
#include "mydear/updater.hpp"

// once, after App::create
mydear::updater::Start({
    .appName = "My App",
    .version = "1.4.2",
    .appcastUrl = "https://example.com/appcast.xml",     // windows
    .publicKey = "<base64 ed25519 public key>",          // windows
    .manifestUrl = "https://example.com/version.json",   // linux
});

// a "Check for Updates..." menu item
mydear::updater::Check();

// before exit
mydear::updater::Stop();
```

`Start()` checks quietly in the background. `Check()` is the check a user asked for: it says what it found ("up to date", or the new version with its notes), offers to download it, and offers to restart afterwards. On Linux `Available()` and `LatestVersion()` report what the quiet check found, so the app can show an update button of its own.

## Turning it on

The updater is off by default, so mydear needs nothing extra. Configure with `-DMYDEAR_UPDATER=ON`; the host provides the platform's updater, because it also has to ship it:

- **macOS:** set `MYDEAR_SPARKLE_FRAMEWORK` to a `Sparkle.framework` (Sparkle 2) and embed the framework in the app bundle (`Contents/Frameworks`). Sparkle reads the feed and the key from `Info.plist`: `SUFeedURL` and `SUPublicEDKey`; without the key it doesn't start. `appcastUrl` and `publicKey` are not used.
- **Windows:** define an imported `WinSparkle` target (`winsparkle.h`, `WinSparkle.lib`) and ship `WinSparkle.dll` next to the exe.
- **Linux:** mydear finds `httplib`, `nlohmann_json` and `OpenSSL` with `find_package` (vcpkg has all three).
- **iOS:** always a no-op; the App Store updates the app.

## What each platform does

| | Feed | Verified by | Installed by | Prompts |
|---|---|---|---|---|
| macOS | Sparkle appcast (`SUFeedURL`) | Sparkle: ed25519 signature, code signature | Sparkle, replacing the whole `.app` | Sparkle's own windows, on its schedule |
| Windows | Sparkle appcast (`appcastUrl`) | WinSparkle: ed25519 signature (`publicKey`) | the downloaded installer (an MSI, for instance) | WinSparkle's own windows |
| Linux | JSON manifest (`manifestUrl`) | SHA-256 from the manifest, before anything is replaced | mydear: rename over the AppImage, then exec it | GTK dialogs, only after `Check()` |

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

mydear picks the entry for the CPU it was built for (`appimage-x86_64` or `appimage-aarch64`; `Config::assetKey` overrides it), with no fallback, since another architecture's binary won't run. Versions compare as dotted numbers; a part that isn't a number counts as 0, so `1.4.3-beta` is `1.4.3`.

An update goes like this:

1. Download next to the AppImage (`<file>.update`), so the final rename stays on one filesystem.
2. Check its SHA-256 against the manifest. A mismatch, a failed download or a cancel deletes the file; the installed AppImage is never touched.
3. Mark it executable and rename it over the running AppImage. The running process keeps its old file open, so nothing breaks until the restart.
4. Ask to restart; **Restart Now** execs the new AppImage in place of the running process.

Only an AppImage (`$APPIMAGE` set) updates itself. Run any other way, `Check()` explains that the package manager or a fresh download updates it, and `Available()` still reports a newer version.

The SHA-256 catches a broken or swapped download from the manifest's host, but it doesn't prove who published the release: anyone who can change the manifest can change the hash. Serve the manifest over HTTPS from a host you control.

## Restart and unsaved work

macOS and Windows: Sparkle and WinSparkle ask the app to quit before installing. On Linux the restart happens only when the user clicks **Restart Now**. `execl` replaces the process without running destructors, so an app with unsaved state should save it when the window loses focus or on a timer, not only at exit.
