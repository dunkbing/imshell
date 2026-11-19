#include "icons.hpp"

namespace imshell::detail {
    // shortcut: no native icon loader here yet (windows: segoe fluent icons, linux: the gtk symbolic theme), Icon() gives the host's fallback; build the loader for the platform
    const ImFontLoader* IconLoader() {
        return nullptr;
    }

    bool IconAvailable(const char*) {
        return false;
    }
} // namespace imshell::detail
