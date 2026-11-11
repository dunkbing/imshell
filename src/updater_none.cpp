// IMSHELL_UPDATER off, or ios (the app store updates)
#include "imshell/updater.hpp"

namespace imshell::updater {
    void Start(const Config&) {}
    void Check() {}
    bool Available() {
        return false;
    }
    std::string LatestVersion() {
        return {};
    }
    void Stop() {}
} // namespace imshell::updater
