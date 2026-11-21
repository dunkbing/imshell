// the README's pictures: the components gallery drawn offscreen in a few themes, with no window.
//   imshell_screenshots render tokyo-night --images -o dir   a theme: the last png is the picture (auto-sizing cards need a few frames to settle, so one frame alone is not enough)
// the scene names are theme ids; see `imshell_screenshots help` for the rest
#define IMSHELL_GALLERY_NO_MAIN
#include "../gallery/main.cpp"

#include "imshell/video_cli.hpp"
#include "imshell/video_scenes.hpp"

int main(int argc, char** argv) {
    using namespace imshell::video;
    std::vector<Scene> scenes;
    for (const char* id : {"tokyo-night", "one-light", "catppuccin-mocha", "github-light", "gruvbox-dark", "nord"})
        scenes.push_back({id, 30, 0, [id](const Frame& f) {
            for (const auto& theme : imshell::Themes())
                if (std::string_view(theme.id) == id)
                    imshell::SetTheme(theme);
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(static_cast<float>(f.width), static_cast<float>(f.height)));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
            ImGui::Begin("gallery", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
            ImGui::PopStyleVar();
            gallery.shot = true;
            gallery.body();
            ImGui::End();
        }});
    Composition comp = MakeComposition(1000, 680, 30, scenes);
    comp.setup = [] { gallery.setup(); };
    return Cli(comp, argc, argv, "imshell_screenshots");
}
