// auto-update (opt in with -DIMSHELL_UPDATER=ON; a no-op otherwise and on ios). the os install
// format picks the mechanism: sparkle on macos, winsparkle on windows, an appimage swap on linux.
// see docs/auto-update.md
#pragma once

#include <string>

namespace imshell::updater {
    struct Config {
        std::string appName;     // windows: winsparkle's dialogs
        std::string version;     // this build, dotted numbers ("1.4.2")
        std::string appcastUrl;  // windows: the sparkle appcast (macos reads SUFeedURL from Info.plist)
        std::string publicKey;   // windows: the appcast's ed25519 key (macos: SUPublicEDKey)
        std::string manifestUrl; // linux: json manifest, see the docs for its shape
        std::string assetKey;    // linux: the entry under "downloads"; empty = appimage-<arch>
        // windows: stage updates instead of running the installer over the app. the
        // downloaded installer runs with these arguments, e.g. "/verysilent /update=true" for an
        // inno setup installer that then installs into <app dir>\install\; the host ships
        // tools\imshell-update.exe, which swaps it in once the app exits. empty = winsparkle
        // runs the installer itself (an msi's wizard). see docs/auto-update.md
        std::string stageArguments;
    };

    // starts a silent background check; call once after the window exists
    void Start(const Config& config);
    // a check the user asked for: says what it found, offers the update, restarts after it
    void Check();
    // linux only: a newer version was found (macos and windows prompt by themselves)
    bool Available();
    std::string LatestVersion();
    // before exit: cancels a running check or download
    void Stop();
} // namespace imshell::updater
