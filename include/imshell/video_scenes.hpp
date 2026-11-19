#pragma once

// scenes: a composition made of ideas that follow each other, each with its own frame counter and
// an optional cross-fade into the one before. SceneLayers is pure (which scenes are on screen at a
// frame, and how much); DrawScenes draws them with ImGui's alpha; MakeComposition builds a
// Composition whose marks name the scenes for the command line
#include "imgui.h"
#include "imshell/video.hpp"

#include <algorithm>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace imshell::video {
    struct Scene {
        std::string name;
        int frames = 60; // its share of the timeline; the overlap is not added to the total
        // frames over which it fades in while the previous scene fades out (a dissolve; content that has
        // no opaque background of its own does not pop). the scene starts drawing at the first of
        // these frames: its entrance animation begins with the fade. 0 = a cut; the first scene has none
        int overlap = 0;
        std::function<void(const Frame&)> draw; // Frame::index counts from the scene's own first frame
    };

    // one scene on screen at a frame
    struct Layer {
        int scene = 0;
        int local = 0;      // the frame within the scene, from 0
        int length = 0;     // the scene's frames, with its overlap
        float alpha = 1.f;  // below 1 while it fades in
        double offset = 0.0; // blur: the fraction of a frame between `local` and the frame being drawn
    };

    // the frames the scenes take together
    inline int SceneFrames(std::span<const Scene> scenes) {
        int total = 0;
        for (const Scene& s : scenes) {
            total += s.frames;
        }
        return total;
    }

    // the scenes visible at `position` (a frame, fractional for a blur sample), bottom first. past
    // the end the last scene stays, on its last frame
    inline std::vector<Layer> SceneLayers(double position, std::span<const Scene> scenes) {
        std::vector<Layer> layers;
        const int total = SceneFrames(scenes);
        if (scenes.empty()) {
            return layers;
        }
        const double at = std::min(position, static_cast<double>(std::max(total - 1, 0)));
        int start = 0;
        for (size_t i = 0; i < scenes.size(); ++i) {
            const int overlap = i == 0 ? 0 : std::clamp(scenes[i].overlap, 0, std::min(scenes[i - 1].frames, scenes[i].frames));
            const int first = start - overlap;
            const int end = start + scenes[i].frames;
            if (at >= first && at < end) {
                Layer layer;
                layer.scene = static_cast<int>(i);
                layer.length = scenes[i].frames + overlap;
                const double local = at - first;
                layer.local = std::clamp(static_cast<int>(std::floor(local)), 0, layer.length - 1);
                layer.offset = local - layer.local;
                layer.alpha = overlap > 0 ? static_cast<float>(std::clamp(local / overlap, 0.0, 1.0)) : 1.f;
                if (layer.alpha < 1.f && !layers.empty() && layers.back().scene == layer.scene - 1) {
                    layers.back().alpha = 1.f - layer.alpha; // a dissolve: the one leaving fades as this one comes
                }
                layers.push_back(layer);
            }
            start = end;
        }
        return layers;
    }

    // draws the layers of frame `f`: each scene gets its own Frame, with ImGui's alpha pushed while it fades
    inline void DrawScenes(const Frame& f, std::span<const Scene> scenes) {
        for (const Layer& layer : SceneLayers(f.position(), scenes)) {
            const Scene& scene = scenes[static_cast<size_t>(layer.scene)];
            if (!scene.draw) {
                continue;
            }
            Frame sub = f;
            sub.index = layer.local;
            sub.frames = layer.length;
            sub.scene = layer.scene;
            sub.offset = static_cast<float>(layer.offset);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, layer.alpha);
            scene.draw(sub);
            ImGui::PopStyleVar();
        }
    }

    // a composition of the scenes: its length is theirs, its marks name them
    inline Composition MakeComposition(int width, int height, int fps, std::vector<Scene> scenes) {
        Composition comp;
        comp.width = width;
        comp.height = height;
        comp.fps = fps;
        comp.frames = std::max(1, SceneFrames(scenes));
        int start = 0;
        for (size_t i = 0; i < scenes.size(); ++i) {
            const int overlap = i == 0 ? 0 : std::clamp(scenes[i].overlap, 0, std::min(scenes[i - 1].frames, scenes[i].frames));
            comp.marks.push_back({scenes[i].name.empty() ? "scene" + std::to_string(i) : scenes[i].name, start - overlap, scenes[i].frames + overlap});
            start += scenes[i].frames;
        }
        comp.draw = [scenes = std::move(scenes)](const Frame& f) { DrawScenes(f, scenes); };
        return comp;
    }
} // namespace imshell::video
