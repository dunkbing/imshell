#pragma once

#include "imgui.h"
#include <initializer_list>

// the platform's own controls inside the imgui layout, drawn and handled by the os (an NSSwitch,
// an NSSegmentedControl; UISwitch and UISegmentedControl on ios), through NativeView. where a
// platform has none they are imshell's own ToggleButton and SegmentedTabs, so a caller writes one
// line and needs no #ifdef:
//
//   if (imshell::NativeSwitch("##notify", &notify)) save();
//   imshell::NativeSegmented("##view", {"List", "Grid"}, &view);
//
// what the os draws is its own: the system accent and the window's light or dark, not the
// theme's colours, so they look right with SystemTheme() and in step with the rest of the os.
// and, as with any native view, they hide while an imgui popup covers them and imgui's keyboard
// navigation does not reach them (the os's does: tab, space, and VoiceOver). use the imshell
// widgets where either matters
namespace imshell {
    // true where NativeSwitch and NativeSegmented are the os's controls
    bool NativeControlsAvailable();

    // a switch; true in the frame it changed
    bool NativeSwitch(const char* id, bool* value);

    // segments (one selected); true in the frame the selection changed. the labels are read once
    // per id, so changing them later needs another id
    bool NativeSegmented(const char* id, std::initializer_list<const char*> labels, int* current);
} // namespace imshell
