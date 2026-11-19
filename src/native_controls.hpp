#pragma once

#include <span>

// what a platform does for imshell/native_controls.hpp; src/native_controls.cpp falls back to
// imshell's own widgets where nativeControlsAvailable() is false
namespace imshell::detail {
    bool nativeControlsAvailable();
    bool nativeSwitch(const char* id, bool* value);
    bool nativeSegmented(const char* id, std::span<const char* const> labels, int* current);
    bool nativePopUp(const char* id, std::span<const char* const> items, int* current);
} // namespace imshell::detail
