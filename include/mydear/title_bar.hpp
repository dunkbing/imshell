#pragma once

#include "imgui.h"

// a title bar drawn with imgui: the app puts any widgets in it, mydear does the platform
// part. empty space drags the window, a double-click zooms (or what the os setting says),
// and the os's own buttons keep their place (macos traffic lights). needs Config::titleBar
//
//   auto bar = mydear::BeginTitleBar();
//   if (mydear::IconButton(...)) ...
//   mydear::EndTitleBar();
//
// lay the rest of the ui out below mydear::TitleBarHeight()
namespace mydear {
    struct TitleBar {
        ImVec2 pos, size;     // the strip, screen coordinates
        float leftInset = 0;  // taken by the os buttons at the start: start widgets after it
        float rightInset = 0; // the same at the end
        bool fullscreen = false;
    };

    float TitleBarHeight();
    // a window over the strip, the cursor at leftInset, vertically centred for a frame-height row
    TitleBar BeginTitleBar();
    void EndTitleBar();
} // namespace mydear
