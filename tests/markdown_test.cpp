// imshell::Markdown on a bare imgui context, no window: a document is drawn for a few frames,
// the code block reaches the highlighter hook, and Render works again after Shutdown. the
// renderer loads its fonts on demand, so the context says its backend takes textures and the
// frame loop below plays that backend. built with -DIMSHELL_BUILD_TESTS=ON and imgui_rich_md
#define STB_IMAGE_IMPLEMENTATION // imgui_rich_md's, which src/image.cpp has in an app
#include "stb_image.h"

#include "imshell/markdown.hpp"

#include "imgui.h"

#include <cstdio>
#include <string>

namespace {
    int failures = 0;
}

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond);                           \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

namespace {
    const std::string document = "# Heading\n\n"
                                 "Some **bold**, *italic* and `inline code`.\n\n"
                                 "- one\n- two\n\n"
                                 "| a | b |\n|---|---|\n| 1 | 2 |\n\n"
                                 "```cpp\nint x = 1;\n```\n";

    // what a renderer backend does with the atlas textures
    void textures(bool destroy) {
        for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
            if (destroy || tex->Status == ImTextureStatus_WantDestroy) {
                tex->SetTexID(ImTextureID_Invalid);
                tex->SetStatus(ImTextureStatus_Destroyed);
            } else if (tex->Status != ImTextureStatus_OK) {
                tex->SetTexID(1);
                tex->SetStatus(ImTextureStatus_OK);
            }
        }
    }

    void frames(int n = 3) {
        for (int i = 0; i < n; ++i) {
            ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(800, 600));
            ImGui::Begin("w", nullptr, ImGuiWindowFlags_NoDecoration);
            imshell::Markdown::Render(document, "doc");
            ImGui::End();
            ImGui::Render();
            CHECK(ImGui::GetDrawData()->TotalVtxCount > 0);
            textures(false);
        }
    }
} // namespace

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(800, 600);
    io.IniFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    std::string language = "?", highlighted, asked = "?";
    int made = 0;
    imshell::Markdown::Options options;
    options.highlighter = [&](const std::string& lang) {
        ++made;
        language = lang;
        return [&](std::string_view text, std::span<imshell::TextEditor::Token>,
                   const imshell::TextEditor::Edit&) { highlighted = text; };
    };
    options.codeActions.push_back({.icon = "Run",
                                   .tooltip = "Run it",
                                   .applies =
                                       [&](const std::string& lang) {
                                           asked = lang;
                                           return true;
                                       },
                                   .run = [](const std::string&, const std::string&) {}});
    imshell::Markdown::Configure(options);

    frames();
    CHECK(made == 1); // one block, asked once
    CHECK(language == "cpp");
    CHECK(highlighted == "int x = 1;");
    CHECK(asked == "cpp"); // the action's predicate

    imshell::Markdown::Shutdown();
    frames();
    CHECK(made == 2); // a new editor after Shutdown
    CHECK(highlighted == "int x = 1;");

    imshell::Markdown::Shutdown();
    textures(true);
    ImGui::DestroyContext();
    if (failures) {
        std::fprintf(stderr, "FAIL: %d check(s)\n", failures);
        return 1;
    }
    std::printf("markdown: ok\n");
    return 0;
}
