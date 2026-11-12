#include "staged_update.hpp"

#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace imshell::detail {
    bool HasStagedUpdate(const fs::path& dir) {
        std::error_code ec;
        return fs::exists(dir / "install" / kStagedMarker, ec);
    }

    std::string ApplyStagedUpdate(const fs::path& dir, const RenameFn& renameFn) {
        if (!HasStagedUpdate(dir))
            return {};
        const RenameFn rename = renameFn ? renameFn : [](const fs::path& from, const fs::path& to) {
            std::error_code ec;
            fs::rename(from, to, ec);
            return !ec;
        };
        const fs::path install = dir / "install", old = dir / "old";
        std::error_code ec;
        fs::remove_all(old, ec); // a previous run's leftovers
        fs::create_directories(old, ec);
        if (ec)
            return "cannot create " + Utf8(old) + ": " + ec.message();

        // listed first: moving entries out while iterating the folder is unspecified
        std::vector<fs::path> names;
        for (const fs::directory_entry& entry : fs::directory_iterator(install, ec))
            if (entry.path().filename() != kStagedMarker)
                names.push_back(entry.path().filename());
        if (ec)
            return "cannot list " + Utf8(install) + ": " + ec.message();

        std::vector<std::pair<fs::path, fs::path>> done; // each move made, to undo
        auto undo = [&] {
            for (auto it = done.rbegin(); it != done.rend(); ++it)
                rename(it->second, it->first);
        };
        auto move = [&](const fs::path& from, const fs::path& to) {
            if (!rename(from, to))
                return false;
            done.emplace_back(from, to);
            return true;
        };
        for (const fs::path& name : names) {
            const fs::path from = install / name, target = dir / name;
            std::error_code exists;
            if (fs::exists(fs::symlink_status(target, exists)) && !move(target, old / name)) {
                undo();
                return "cannot move " + Utf8(target) + " aside";
            }
            if (!move(from, target)) {
                undo();
                return "cannot move " + Utf8(from) + " into place";
            }
        }
        fs::remove_all(install, ec);
        fs::remove_all(old, ec);
        return {};
    }
} // namespace imshell::detail
