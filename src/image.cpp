#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

#include "mydear/image.hpp"

namespace mydear {
    ImTextureID LoadTexture(const uint8_t* data, const size_t size, const CreateTexture& create,
                            int* width, int* height) {
        int w = 0, h = 0, channels = 0;
        unsigned char* pixels =
            stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &channels, 4);
        if (!pixels)
            return ImTextureID{};
        const ImTextureID tex = create(pixels, w, h);
        stbi_image_free(pixels);
        if (width)
            *width = w;
        if (height)
            *height = h;
        return tex;
    }
} // namespace mydear
