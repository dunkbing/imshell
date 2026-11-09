#pragma once

#include "imgui.h"
#include <cstddef>
#include <cstdint>
#include <functional>

namespace mydear {
    // the backend's upload: RGBA pixels in, a texture out (ImTextureID{} on failure)
    using CreateTexture = std::function<ImTextureID(const uint8_t* rgba, int width, int height)>;

    // decode a PNG held in memory (an embedded image) and upload it
    ImTextureID LoadTexture(const uint8_t* data, size_t size, const CreateTexture& create,
                            int* width = nullptr, int* height = nullptr);
} // namespace mydear
