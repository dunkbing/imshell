// every mydear::ui component on a bare imgui context for a few frames: imgui's asserts catch a
// broken push/pop, and the test engine's item hook (compiled into this test's own imgui) sees
// every item id, so a duplicate fails. built with -DMYDEAR_BUILD_TESTS=ON
#include "mydear/components.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <cstdio>
#include <set>
#include <string>

namespace {
    std::set<ImGuiID> seen; // this frame's item ids
    int duplicates = 0;
} // namespace

void ImGuiTestEngineHook_ItemAdd(ImGuiContext*, ImGuiID id, const ImRect&,
                                 const ImGuiLastItemData*) {
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

namespace ui = mydear::ui;

namespace {
    std::string field = "x", empty;
    bool chips[3] = {true, false, false};

    void frame(int n) {
        ImGuiIO& io = ImGui::GetIO();
        io.DeltaTime = 1.0f / 60.0f;
        seen.clear();
        ImGui::NewFrame();
        GImGui->TestEngineHookItems = true;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(500, 900));
        ImGui::Begin("w", nullptr, ImGuiWindowFlags_NoDecoration);
        ui::TextField("Email", field, {.placeholder = "you@example.com", .error = "bad"});
        ui::TextField("Password", empty, {.hint = "8+", .password = true});
        ui::BeginCard("card", "Title");
        ui::Badge("one");
        ImGui::SameLine();
        ui::Badge("two", ui::Tone::Danger);
        ui::Callout("Title", "Body text that wraps over more than one line in a narrow window.");
        ui::Callout(nullptr, "No title", ui::Tone::Success);
        ui::Progress(0.5f, "Half");
        ui::Progress(-1);
        ui::Skeleton(ImVec2(0, 12));
        ui::EndCard();
        ui::Avatar("Ada Lovelace");
        ImGui::SameLine();
        ui::Avatar("x", 20);
        ui::Metric("m1", "Revenue", "$1", "+1%");
        ui::Metric("m2", "Churn", "2%", "-1%");
        ui::EmptyState("Nothing", "Body", "Act");
        static const char* const labels[] = {"a", "b", "c"};
        ui::Chips("chips", labels, chips);
        ImGui::End();
        if (n == 1)
            ui::Toast("hello", ui::Tone::Info, 0.05f);
        ui::RenderToasts();
        ImGui::Render();
    }
} // namespace

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(500, 900);
    io.Fonts->Build();
    for (int i = 0; i < 10; ++i)
        frame(i);
    ImGui::DestroyContext();
    if (duplicates) {
        std::fprintf(stderr, "FAIL: %d duplicate id(s)\n", duplicates);
        return 1;
    }
    std::printf("components: ok\n");
    return 0;
}
