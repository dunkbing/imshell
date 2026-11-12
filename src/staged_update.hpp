// a staged windows update: the installer, run in update mode, put the new
// version in <app dir>/install/ while the old one ran; after the app exits, the helper swaps it
// in. portable and pure std::filesystem, so the plan is tested everywhere; the helper adds the
// win32 parts (waiting for the app, retrying a locked file, the relaunch)
#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace imshell::detail {
    // written into install/ last, once the installer exited cleanly: only a complete stage is applied
    inline constexpr const char* kStagedMarker = ".imshell-staged";

    // for messages: path::string() throws on windows when a name is not in the ansi code page
    // (a user name in %LOCALAPPDATA%), utf-8 never does
    inline std::string Utf8(const std::filesystem::path& path) {
        const std::u8string s = path.u8string();
        return {s.begin(), s.end()};
    }

    using RenameFn = std::function<bool(const std::filesystem::path& from, const std::filesystem::path& to)>;

    // true when dir/install/ holds a complete stage
    bool HasStagedUpdate(const std::filesystem::path& dir);

    // each entry of dir/install/ replaces the one in dir: the old moves to dir/old/ first. a failed
    // step undoes the earlier ones in reverse, so the app is either all old or all new. old/ and
    // install/ are removed after a success (errors there are ignored). returns the error, empty
    // when applied or when there was nothing complete to apply. a stage that failed
    // kMaxStagedAttempts times is deleted (the next update downloads again), so a broken one
    // does not retry at every exit forever
    inline constexpr int kMaxStagedAttempts = 3;
    std::string ApplyStagedUpdate(const std::filesystem::path& dir, const RenameFn& rename = {});
} // namespace imshell::detail
