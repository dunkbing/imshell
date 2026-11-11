// what every shell shares: the data folder, imgui.ini and Run()
#include "imshell/app.hpp"
#include "imshell/image.hpp"
#include "activity.hpp"

#include <cstdlib>
#include <filesystem>
#include <system_error>
#ifdef __ANDROID__
#include "imshell/android.hpp"
#endif
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

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

    void detail::useIniFile(const Config& config) {
        static std::string path; // imgui keeps the pointer
        const std::string dir = config.appName.empty() ? std::string() : DataDir(config.appName);
        path = dir.empty() ? std::string() : dir + "/imgui.ini";
        ImGui::GetIO().IniFilename = path.empty() ? nullptr : path.c_str();
    }

    static App* gRunning = nullptr; // Run's app, for LoadTexture

    ImTextureID LoadTexture(const uint8_t* data, size_t size, int* width, int* height) {
        if (!gRunning)
            return ImTextureID{};
        return LoadTexture(
            data, size,
            [](const uint8_t* rgba, int w, int h) { return gRunning->createTexture(rgba, w, h); },
            width, height);
    }

#if !(defined(__APPLE__) && TARGET_OS_IOS)
    int Run(const Config& config, Delegate& delegate) {
        static App app; // android: outlives a recreated activity
        static bool started = false;
        gRunning = &app; // before create: setup() may load textures
        if (!started && !(started = app.create(config, delegate))) {
            gRunning = nullptr;
            return 1;
        }
        app.run();
#ifdef __ANDROID__
        if (AndroidActivityRecreating())
            return 0; // the next android_main carries on
#endif
        gRunning = nullptr;
        app.destroy();
        started = false;
        return 0;
    }
#endif
} // namespace imshell
