#include "imshell/vibrancy.hpp"

#include "vibrancy.hpp"

namespace imshell {
    // shortcut: no window material off macos yet (ios: UIVisualEffectView, windows: Mica or acrylic), so hosts keep their opaque sidebar
    bool VibrancyAvailable() {
        return false;
    }

    void Vibrancy(ImVec2, ImVec2, VibrancyMaterial) {}

    namespace detail {
        bool vibrancyActive() {
            return false;
        }

        void applyVibrancy() {}
    } // namespace detail
} // namespace imshell
