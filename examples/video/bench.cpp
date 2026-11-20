// the benchmark compositions: 1080p30, 10 s, drawn with ordinary imgui widgets.
//   imshell_video_bench ui bench          24 windows with text and bars, springing in
//   imshell_video_bench table bench       one window, a 12 x 40 table of live numbers and a plot
//   imshell_video_bench effects bench     the ui scene with glow, grade, vignette and grain on every frame
// the first argument picks the scene, the rest is the video command line (bench, render, frame, ...)
#include "imshell/video_cli.hpp"
#include "imshell/video_effects.hpp"

#include "imgui.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace imshell::video;

namespace {
    constexpr ImGuiWindowFlags kBare = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar;

    void uiScene(const Frame& f) {
        constexpr int cols = 6, rows = 4;
        const float cw = 300.f, ch = 220.f, gap = 18.f;
        const float x0 = (static_cast<float>(f.width) - cols * cw - (cols - 1) * gap) * 0.5f;
        const float y0 = (static_cast<float>(f.height) - rows * ch - (rows - 1) * gap) * 0.5f;
        for (int i = 0; i < cols * rows; ++i) {
            const int start = i * 4;
            const float rise = SpringIn(f.position(), f.fps, 60.f, 0.f, start, 40);
            const float fill = Interpolate(f.position(), {static_cast<float>(start + 30), static_cast<float>(start + 200)}, {0.f, 1.f}, Ease::InOutCubic);
            ImGui::SetNextWindowPos(ImVec2(x0 + (i % cols) * (cw + gap), y0 + (i / cols) * (ch + gap) + rise));
            ImGui::SetNextWindowSize(ImVec2(cw, ch));
            char id[24];
            std::snprintf(id, sizeof id, "card##%d", i);
            ImGui::Begin(id, nullptr, kBare);
            ImGui::Text("Card %d", i + 1);
            ImGui::Separator();
            ImGui::Text("frame %d, %.2f s", f.index, f.time());
            ImGui::TextWrapped("a handful of widgets, laid out by imgui every frame");
            ImGui::ProgressBar(fill, ImVec2(-1.f, 0.f));
            ImGui::Button("Button");
            ImGui::SameLine();
            static bool checks[cols * rows] = {}; // never clicked: a frame is still a function of its number
            ImGui::Checkbox("check", &checks[i]);
            ImGui::End();
        }
    }

    void tableScene(const Frame& f) {
        ImGui::SetNextWindowPos(ImVec2(60.f, 40.f));
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(f.width) - 120.f, static_cast<float>(f.height) - 80.f));
        ImGui::Begin("table", nullptr, kBare);
        if (ImGui::BeginTable("t", 12, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            for (int c = 0; c < 12; ++c) {
                char h[16];
                std::snprintf(h, sizeof h, "col %d", c);
                ImGui::TableSetupColumn(h);
            }
            ImGui::TableHeadersRow();
            for (int r = 0; r < 34; ++r) {
                ImGui::TableNextRow();
                for (int c = 0; c < 12; ++c) {
                    ImGui::TableSetColumnIndex(c);
                    ImGui::Text("%.2f", std::sin((f.index + r * 7 + c * 13) * 0.05) * 100.0);
                }
            }
            ImGui::EndTable();
        }
        std::vector<float> values(256);
        for (size_t i = 0; i < values.size(); ++i) {
            values[i] = std::sin((static_cast<float>(i) + static_cast<float>(f.index)) * 0.1f);
        }
        ImGui::PlotLines("##plot", values.data(), static_cast<int>(values.size()), 0, nullptr, -1.f, 1.f, ImVec2(-1.f, 80.f));
        ImGui::End();
    }
} // namespace

int main(int argc, char** argv) {
    const char* scene = argc > 1 ? argv[1] : "ui";
    if (argc < 3 || (std::strcmp(scene, "ui") != 0 && std::strcmp(scene, "table") != 0 && std::strcmp(scene, "effects") != 0)) {
        std::fprintf(stderr, "usage: imshell_video_bench ui|table|effects <video command, e.g. bench>\n");
        return 1;
    }
    Composition comp;
    comp.width = 1920;
    comp.height = 1080;
    comp.fps = 30;
    comp.frames = 300;
    comp.draw = std::strcmp(scene, "table") == 0 ? tableScene : uiScene;
    if (std::strcmp(scene, "effects") == 0) {
        comp.post = [](const Frame& f, Image& image) {
            Glow(image, 0.6f, 0.02f, 0.7f);
            Grade(image, 0.f, 1.1f, 1.15f);
            Vignette(image, 0.6f, 0.5f);
            Grain(image, 0.03f, static_cast<uint32_t>(f.index));
        };
    }
    return Cli(comp, argc - 1, argv + 1, "imshell_video_bench");
}
