// winsparkle: the same appcast and ed25519 key as sparkle, its own dialogs
#include "mydear/updater.hpp"

#include <string>
#include <windows.h>
#include <winsparkle.h>

namespace mydear::updater {
    namespace {
        bool gStarted = false;

        std::wstring wide(const std::string& s) {
            const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
            std::wstring out(n > 0 ? n - 1 : 0, L'\0');
            if (n > 1)
                MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
            return out;
        }
    } // namespace

    void Start(const Config& config) {
        if (gStarted || config.appcastUrl.empty())
            return;
        win_sparkle_set_app_details(wide(config.appName).c_str(), wide(config.appName).c_str(),
                                    wide(config.version).c_str());
        win_sparkle_set_appcast_url(config.appcastUrl.c_str());
        if (!config.publicKey.empty())
            win_sparkle_set_eddsa_public_key(config.publicKey.c_str());
        win_sparkle_set_automatic_check_for_updates(1);
        win_sparkle_init();
        gStarted = true;
    }

    void Check() {
        if (gStarted)
            win_sparkle_check_update_with_ui();
    }

    bool Available() {
        return false;
    }

    std::string LatestVersion() {
        return {};
    }

    void Stop() {
        if (gStarted)
            win_sparkle_cleanup();
        gStarted = false;
    }
} // namespace mydear::updater
