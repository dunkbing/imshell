#pragma once

#include "imgui.h"
#include <cstddef>
#include <cstdint>
#include <functional>

namespace imshell {
    // the backend's upload: RGBA pixels in, a texture out (ImTextureID{} on failure)
    using CreateTexture = std::function<ImTextureID(const uint8_t* rgba, int width, int height)>;

    // decode a PNG, JPEG, GIF (first frame), BMP or TGA held in memory (an embedded image; at most
    // 16384 a side and 64 million pixels, else ImTextureID{}) and upload it
    ImTextureID LoadTexture(const uint8_t* data, size_t size, const CreateTexture& create,
                            int* width = nullptr, int* height = nullptr);
    // the same, uploaded by the app that Run() started (a delegate has no App to ask);
    // ImTextureID{} when there is none (before Run, after it, on ios)
    ImTextureID LoadTexture(const uint8_t* data, size_t size, int* width = nullptr,
                            int* height = nullptr);
} // namespace imshell
