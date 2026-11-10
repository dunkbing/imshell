#pragma once

#include <string>

namespace mydear {
    struct Config;
}

namespace mydear::detail {
    // input or a widget in use this frame: the loop stays at full speed
    bool isImGuiUserActive();
    // after ImGui::CreateContext: imgui.ini in DataDir(appName), or none
    void useIniFile(const Config& config);
#ifdef __ANDROID__
    std::string androidFilesDir(); // the activity's internal storage
#endif
} // namespace mydear::detail
