// MYDEAR_UPDATER off, or ios (the app store updates)
#include "mydear/updater.hpp"

namespace mydear::updater {
    void Start(const Config&) {}
    void Check() {}
    bool Available() {
        return false;
    }
    std::string LatestVersion() {
        return {};
    }
    void Stop() {}
} // namespace mydear::updater
