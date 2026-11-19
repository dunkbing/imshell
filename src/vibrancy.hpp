#pragma once

// what a shell does for imshell/vibrancy.hpp
namespace imshell::detail {
    // whether the last frame asked for a region: the shell clears transparent while it does
    bool vibrancyActive();
    // after the frame: place, show or hide the os's view for the region the frame asked for
    void applyVibrancy();
} // namespace imshell::detail
