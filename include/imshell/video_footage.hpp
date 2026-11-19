#pragma once

// footage: a video file as a layer. the picture at any time of the clip is a pure function of the time
// (a sequential render reads on, a seek restarts the decoder at that frame, and both give the same
// pixels), drawn through a texture imgui keeps up to date. the decoder is the `ffmpeg` binary, so a
// footage needs it on PATH (or `ffmpeg` set to its path); the clip's sound is added like any sound:
// an AudioClip whose path is the same file
#include "imgui.h"
#include "imshell/video.hpp"

#include <memory>
#include <string>

namespace imshell::video {
    enum class Fit {
        Stretch, // the whole picture over the box
        Contain, // all of it, centred, the box's spare space left alone
        Cover,   // fills the box, the overhang cropped
    };

    class Footage {
    public:
        // `fps` is the composition's rate: the clip is resampled to it. `maxWidth` > 0 decodes smaller than the file
        // (faster and lighter when it is drawn small). the object must outlive every render that draws it
        explicit Footage(std::string path, int fps, int maxWidth = 0, std::string ffmpeg = "ffmpeg");
        ~Footage();
        Footage(const Footage&) = delete;
        Footage& operator=(const Footage&) = delete;

        bool ok() const;
        int width() const;   // as decoded
        int height() const;
        double seconds() const; // length of the clip
        const std::string& error() const;

        // the picture `t` seconds into the clip (clamped: the first frame before 0, the last one held after the end)
        // as rgba, rows top to bottom; width() * height() * 4 bytes, valid until the next call. null when it is not ok()
        const unsigned char* pixels(double t);

        // draws the picture at `t` seconds into the clip into the box, on the draw list. the alpha of the style stack applies
        void Draw(ImDrawList* dl, double t, ImVec2 pos, ImVec2 size, Fit fit = Fit::Cover, ImU32 tint = IM_COL32_WHITE, float rounding = 0.f);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace imshell::video
