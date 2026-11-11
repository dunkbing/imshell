// the data folder, on its own so a host can use it without linking a shell
#include "imshell/app.hpp"
#include "activity.hpp"

#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace imshell {
    std::string DataDir(const std::string& appName) {
        namespace fs = std::filesystem;
        const auto env = [](const char* name) {
            const char* v = std::getenv(name);
            return v && *v ? fs::path(v) : fs::path();
        };
#if defined(__ANDROID__)
        fs::path root = detail::androidFilesDir();
#elif defined(_WIN32)
        const wchar_t* appData = _wgetenv(L"APPDATA"); // wide: a non-ascii user name
        fs::path root = appData ? fs::path(appData) : fs::path();
#elif defined(__APPLE__)
        fs::path root = env("HOME").empty() ? fs::path() : env("HOME") / "Library" / "Application Support";
#else
        fs::path root = env("XDG_DATA_HOME");
        if (root.empty() && !env("HOME").empty())
            root = env("HOME") / ".local" / "share";
#endif
        if (root.empty() || appName.empty())
            return {};
        const fs::path dir = root / fs::path(reinterpret_cast<const char8_t*>(appName.c_str()));
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (!fs::is_directory(dir, ec))
            return {};
        const std::u8string u8 = dir.u8string(); // utf-8, as imgui wants file names
        return std::string(u8.begin(), u8.end());
    }
} // namespace imshell
