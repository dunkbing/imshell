// panels of a resizable group on a bare imgui context, driven by a scripted mouse: the
// layout, a drag, limits, the window shrinking, a hidden panel, a nested group. the test
// engine's item hook catches duplicate ids. built with -DIMSHELL_BUILD_TESTS=ON
#include "imshell/resizable.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <cmath>
#include <cstdio>
#include <set>

namespace {
    std::set<ImGuiID> seen;
    int duplicates = 0;
    int failed = 0;
} // namespace

void ImGuiTestEngineHook_ItemAdd(ImGuiContext*, ImGuiID id, const ImRect&, const ImGuiLastItemData*) {
    if (id && !seen.insert(id).second) {
        std::fprintf(stderr, "duplicate id 0x%08x\n", id);
        ++duplicates;
    }
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID, const char*, ImGuiItemStatusFlags) {}
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) {
    return nullptr;
}

namespace {
    // the app's state: a fixed sidebar, a flexible middle, a sized right panel
    float sidebar = 200, right = 300;
    bool rightShown = true;
    float width = 1000;
    float seenWidth[3] = {}; // each panel's width as drawn; 0 when hidden
    float topHeight = 0;
    int releases = 0;

    void frame() {
        ImGuiIO& io = ImGui::GetIO();
        io.DeltaTime = 1.0f / 60.0f;
        io.DisplaySize = ImVec2(width, 600);
        seen.clear();
        ImGui::NewFrame();
        GImGui->TestEngineHookItems = true;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(width, 600));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("w", nullptr, ImGuiWindowFlags_NoDecoration);
        ImGui::PopStyleVar();
        seenWidth[0] = seenWidth[1] = seenWidth[2] = 0;
        if (imshell::BeginResizable("main")) {
            if (imshell::ResizablePanel({.size = &sidebar, .min = 100, .fixed = true})) {
                seenWidth[0] = ImGui::GetWindowWidth();
                ImGui::Button("a");
            }
            if (imshell::ResizablePanel({.min = 200})) {
                seenWidth[1] = ImGui::GetWindowWidth();
                // a nested group: two stacked panels
                if (imshell::BeginResizable("inner", {.axis = imshell::Axis::Vertical})) {
                    if (imshell::ResizablePanel({.min = 50})) {
                        topHeight = ImGui::GetWindowHeight();
                        ImGui::Button("a"); // same label as in another panel: a different id
                    }
                    if (imshell::ResizablePanel({.min = 50})) {
                        ImGui::Button("b");
                    }
                    imshell::EndResizable();
                }
            }
            if (imshell::ResizablePanel({.size = &right, .min = 150, .visible = rightShown})) {
                seenWidth[2] = ImGui::GetWindowWidth();
            }
            releases += imshell::EndResizable();
        }
        ImGui::End();
        ImGui::Render();
    }

    void frames(int n) {
        for (int i = 0; i < n; ++i) {
            frame();
        }
    }

    // press at x, move to x + by over a few frames, let go
    void drag(float x, float by) {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(x, 300);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        for (int step = 1; step <= 5; ++step) {
            io.AddMousePosEvent(x + by * static_cast<float>(step) / 5, 300);
            frame();
        }
        io.AddMouseButtonEvent(0, false);
        frame();
        io.AddMousePosEvent(-100, -100);
        frames(2);
    }

    void expect(bool ok, const char* what) {
        if (!ok) {
            std::fprintf(stderr, "FAIL: %s (widths %.0f %.0f %.0f, sidebar %.0f, right %.0f)\n", what, seenWidth[0],
                         seenWidth[1], seenWidth[2], sidebar, right);
            ++failed;
        }
    }
} // namespace

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Build();
    frames(3);
    // 1000 wide, two 1px handles: the middle takes what the others leave
    expect(seenWidth[0] == 200 && seenWidth[2] == 300 && seenWidth[1] == 498, "first layout");
    expect(std::abs(topHeight - 299) <= 1, "nested group shares its height");

    // the handle after the sidebar is at x 200: dragged 50 to the right
    drag(200.5f, 50);
    expect(seenWidth[0] == 250 && seenWidth[1] == 448 && seenWidth[2] == 300, "drag moves only the pair");
    expect(sidebar == 250, "the sidebar's size is written back");
    expect(releases == 1, "one release per drag");

    // the handle before the right panel (x 250+1+448 = 699): dragged far right, the right
    // panel stops at its minimum, and the handle stays with it
    drag(699.5f, 400);
    expect(seenWidth[2] == 150 && seenWidth[1] == 598, "a minimum stops the drag");
    expect(releases == 2, "second release");

    // the window shrinks: the fixed sidebar keeps its size, the others keep their minimums
    width = 760;
    frames(3);
    expect(seenWidth[0] == 250, "fixed panel keeps its size");
    expect(seenWidth[1] >= 200 && seenWidth[2] >= 150, "minimums kept on shrink");
    expect(seenWidth[0] + seenWidth[1] + seenWidth[2] + 2 == 760, "panels fill the group");

    // narrower than the minimums: the fixed panel gives way first, then all shrink to fit
    width = 400;
    frames(3);
    expect(seenWidth[0] + seenWidth[1] + seenWidth[2] + 2 == 400 && seenWidth[0] == 100, "too narrow: fits the group");
    expect(sidebar == 250, "squeezing does not change the size asked for");
    width = 760;
    frames(3);
    expect(seenWidth[0] == 250, "the fixed panel's size comes back");

    // hidden: no room taken, no handle; shown again at about its size
    const float before = seenWidth[2];
    rightShown = false;
    frames(3);
    expect(seenWidth[2] == 0 && seenWidth[0] + seenWidth[1] + 1 == 760, "hidden panel takes no room");
    rightShown = true;
    frames(3);
    expect(std::abs(seenWidth[2] - before) <= 2, "shown again at its size");

    // the app sets a size: taken
    sidebar = 180;
    frames(3);
    expect(seenWidth[0] == 180, "a size set by the app is used");

    ImGui::DestroyContext();
    if (duplicates) {
        std::fprintf(stderr, "FAIL: %d duplicate id(s)\n", duplicates);
        ++failed;
    }
    if (failed) {
        return 1;
    }
    std::printf("resizable: ok\n");
    return 0;
}
