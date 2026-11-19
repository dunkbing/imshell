#pragma once

// what a graphics backend gives the portable video code (generic.cpp): imgui drawn into an
// offscreen target and read back as rgba. each backend file (egl.cpp, later d3d11.cpp) implements
// MakeRasterizer; the Apple build has its own zero copy path (metal.mm) and does not use this
#include "imgui.h"
#include "imshell/animation.hpp"
#include "imshell/video.hpp"

#include <memory>
#include <string>
#include <vector>

namespace imshell::video {
    class Rasterizer {
    public:
        virtual ~Rasterizer() = default;
        // frame `index` at `offset` frames (motion blur) as width * height * 4 rgba bytes, top row
        // first, over opaque black
        virtual bool render(int index, bool sequential, float offset, std::vector<unsigned char>& rgba) = 0;
    };

    // null and `error` when the platform has no usable graphics (no driver, no display, no gpu)
    std::unique_ptr<Rasterizer> MakeRasterizer(const Composition& comp, std::string* error);

    // the imgui part every backend shares, on the current context: two passes (windows size themselves
    // on their second frame, so the first is thrown away), the second left rendered in
    // ImGui::GetDrawData(). `newFrame` is the renderer backend's NewFrame
    inline void DrawComposition(const Composition& comp, int index, bool sequential, float offset, void (*newFrame)()) {
        const Frame frame{.index = index, .frames = comp.frames, .fps = comp.fps,
                          .width = comp.width, .height = comp.height, .sequential = sequential, .offset = offset};
        ImGui::GetIO().DeltaTime = 1.f / static_cast<float>(comp.fps);
        for (int i = 0; i < 2; ++i) {
            newFrame();
            ImGui::NewFrame();
            if (comp.draw) {
                comp.draw(frame);
            }
            if (i == 0) {
                ImGui::EndFrame();
            } else {
                ImGui::Render();
            }
        }
    }

    // the per-context setup: no ini, the composition's size, easing off (Animate would depend on the frames before)
    inline void PrepareContext(const Composition& comp) {
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.DisplaySize = ImVec2(static_cast<float>(comp.width), static_cast<float>(comp.height));
        io.DisplayFramebufferScale = ImVec2(1.f, 1.f);
        imshell::SetReduceMotion(true);
    }
} // namespace imshell::video
