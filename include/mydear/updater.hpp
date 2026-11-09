// auto-update (opt in with -DMYDEAR_UPDATER=ON; a no-op otherwise and on ios). the os install
// format picks the mechanism: sparkle on macos, winsparkle on windows, an appimage swap on linux.
// see docs/auto-update.md
#pragma once

#include <string>

namespace mydear::updater {
    struct Config {
        std::string appName;     // windows: winsparkle's dialogs
        std::string version;     // this build, dotted numbers ("1.4.2")
        std::string appcastUrl;  // windows: the sparkle appcast (macos reads SUFeedURL from Info.plist)
        std::string publicKey;   // windows: the appcast's ed25519 key (macos: SUPublicEDKey)
        std::string manifestUrl; // linux: json manifest, see the docs for its shape
        std::string assetKey;    // linux: the entry under "downloads"; empty = appimage-<arch>
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
} // namespace mydear::updater
