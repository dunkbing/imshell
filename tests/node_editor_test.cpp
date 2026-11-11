// imshell::NodeEditor on a bare imgui context: a graph draws, and the style and colours follow
// Theme::Current(). built with -DIMSHELL_BUILD_TESTS=ON
#include "imshell/node_editor.hpp"
#include "imshell/themes.hpp"

#include "imgui.h"

#include <cstdio>

namespace ed = ax::NodeEditor;

namespace {
    int failures = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);                       \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

    bool same(const ImVec4& a, const ImVec4& b) {
        return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
    }

    void frame(ed::EditorContext* editor) {
        ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(800, 600));
        ImGui::Begin("w", nullptr, ImGuiWindowFlags_NoDecoration);
        ed::SetCurrentEditor(editor);
        imshell::NodeEditor::ApplyColors();
        ed::Begin("graph");
        for (int id : {1, 2}) {
            ed::BeginNode(id);
            ImGui::Text("node %d", id);
            ed::BeginPin(id * 10, ed::PinKind::Output);
            ImGui::Text("->");
            ed::EndPin();
            ed::EndNode();
        }
        ed::End();
        ed::SetCurrentEditor(nullptr);
        ImGui::End();
        ImGui::Render();
    }
} // namespace

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(800, 600);
    io.IniFilename = nullptr;
    io.Fonts->Build();

    ed::Config config;
    config.SettingsFile = nullptr; // no NodeEditor.json beside the test
    ed::EditorContext* editor = ed::CreateEditor(&config);
    ed::SetCurrentEditor(editor);
    imshell::NodeEditor::ApplyStyle();
    CHECK(ed::GetStyle().NodeRounding == imshell::Theme::CornerRadius::MEDIUM);
    CHECK(ed::GetStyle().NodeBorderWidth == 1.0f);
    ed::SetCurrentEditor(nullptr);

    for (int i = 0; i < 3; ++i)
        frame(editor);
    ed::SetCurrentEditor(editor);
    CHECK(same(ed::GetStyle().Colors[ed::StyleColor_Bg], imshell::Theme::Current()->colors.mantle));
    ed::SetCurrentEditor(nullptr);

    // another theme: the next frame takes its colours
    imshell::Theme::Current() = &imshell::Theme::THEMES[1];
    frame(editor);
    ed::SetCurrentEditor(editor);
    CHECK(same(ed::GetStyle().Colors[ed::StyleColor_NodeBg], imshell::Theme::THEMES[1].colors.surface0));
    ed::SetCurrentEditor(nullptr);

    ed::DestroyEditor(editor);
    ImGui::DestroyContext();
    if (failures) {
        std::fprintf(stderr, "FAIL: %d check(s)\n", failures);
        return 1;
    }
    std::printf("node editor: ok\n");
    return 0;
}
