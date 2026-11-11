// what every shell shares: imgui.ini and Run()
#include "imshell/app.hpp"
#include "imshell/image.hpp"
#include "activity.hpp"

#ifdef __ANDROID__
#include "imshell/android.hpp"
#endif
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

namespace imshell {
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
