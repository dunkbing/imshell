#define STB_IMAGE_IMPLEMENTATION
// png, jpeg, gif, bmp and tga; the rest of stb_image's formats are left out
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_STDIO
#include "stb_image.h"

#include "imshell/image.hpp"

namespace imshell {
    ImTextureID LoadTexture(const uint8_t* data, const size_t size, const CreateTexture& create,
                            int* width, int* height) {
        int w = 0, h = 0, channels = 0;
        // a small file can expand to a huge bitmap: look at the header first
        constexpr long long kMaxPixels = 64LL * 1000 * 1000;
        constexpr int kMaxSide = 16384;
        if (!stbi_info_from_memory(data, static_cast<int>(size), &w, &h, &channels))
            return ImTextureID{};
        const long long rows = h < 0 ? -static_cast<long long>(h) : h; // a top-down bmp reports it negative
        if (w <= 0 || rows == 0 || w > kMaxSide || rows > kMaxSide || w * rows > kMaxPixels)
            return ImTextureID{};
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
} // namespace imshell
