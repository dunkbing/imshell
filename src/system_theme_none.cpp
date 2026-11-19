// no system palette to read here: the first preset stands in
#include "imshell/themes.hpp"

#include <string>

namespace imshell {
    bool SystemThemeAvailable() {
        return false;
    }

    std::string SystemThemeKey() {
        return {};
    }

    Theme SystemTheme() {
        return Themes()[0];
    }
} // namespace imshell
