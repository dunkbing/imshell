// the staged update's swap (src/staged_update.cpp), on a temp folder: applied, ignored without
// the marker, undone when a step fails (the stage kept for the next try), old leftovers cleared
#include "staged_update.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using imshell::detail::ApplyStagedUpdate;
using imshell::detail::kStagedMarker;

namespace {
    int failures = 0;

    void check(bool ok, const char* what) {
        if (!ok) {
            std::fprintf(stderr, "FAIL: %s\n", what);
            ++failures;
        }
    }

    void write(const fs::path& path, const std::string& text) {
        fs::create_directories(path.parent_path());
        std::ofstream(path) << text;
    }

    std::string read(const fs::path& path) {
        std::ifstream in(path);
        std::stringstream s;
        s << in.rdbuf();
        return s.str();
    }

    // the installed app, and a complete (or incomplete) stage of the next version
    fs::path setup(const char* name, bool marker = true) {
        const fs::path dir = fs::temp_directory_path() / "imshell_staged_update_test" / name;
        fs::remove_all(dir);
        write(dir / "app.exe", "old");
        write(dir / "lib.dll", "old");
        write(dir / "data" / "x", "old");
        write(dir / "keep.txt", "user"); // not in the new version: left alone
        write(dir / "install" / "app.exe", "new");
        write(dir / "install" / "lib.dll", "new");
        write(dir / "install" / "data" / "x", "new");
        write(dir / "install" / "added", "new");
        if (marker)
            write(dir / "install" / kStagedMarker, "2.0.0");
        return dir;
    }

    bool allOld(const fs::path& dir) {
        return read(dir / "app.exe") == "old" && read(dir / "lib.dll") == "old" &&
               read(dir / "data" / "x") == "old" && !fs::exists(dir / "added");
    }
} // namespace

int main() {
    {
        const fs::path dir = setup("applied");
        write(dir / "old" / "stale", "from a crashed run");
        check(ApplyStagedUpdate(dir).empty(), "applies");
        check(read(dir / "app.exe") == "new" && read(dir / "lib.dll") == "new", "files replaced");
        check(read(dir / "data" / "x") == "new", "folders replaced");
        check(read(dir / "added") == "new", "new files added");
        check(read(dir / "keep.txt") == "user", "other files kept");
        check(!fs::exists(dir / "install") && !fs::exists(dir / "old"), "install and old removed");
        check(!fs::exists(dir / kStagedMarker), "the marker is not copied");
    }
    {
        const fs::path dir = setup("incomplete", false);
        check(ApplyStagedUpdate(dir).empty(), "nothing to do without the marker");
        check(allOld(dir) && fs::exists(dir / "install"), "an incomplete stage is left alone");
    }
    {
        const fs::path dir = setup("rolled-back");
        int calls = 0;
        const std::string error = ApplyStagedUpdate(dir, [&](const fs::path& from, const fs::path& to) {
            if (++calls == 4) // a locked file, after at least one entry went through
                return false;
            std::error_code ec;
            fs::rename(from, to, ec);
            return !ec;
        });
        check(!error.empty(), "a failed step is reported");
        check(allOld(dir), "a failed step leaves the old version");
        check(read(dir / "install" / "app.exe") == "new" && fs::exists(dir / "install" / kStagedMarker),
              "the stage is kept for the next try");
        check(ApplyStagedUpdate(dir).empty() && read(dir / "app.exe") == "new", "the next try applies");
    }
    fs::remove_all(fs::temp_directory_path() / "imshell_staged_update_test");
    if (failures) {
        std::fprintf(stderr, "FAIL: %d check(s)\n", failures);
        return 1;
    }
    std::printf("staged update: ok\n");
    return 0;
}
