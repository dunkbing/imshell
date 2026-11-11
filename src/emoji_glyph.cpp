#include "emoji.hpp"

#include "imgui_internal.h"

#include <algorithm>

namespace imshell::detail {
    bool PutEmojiGlyph(ImFontAtlas* atlas, ImFontConfig* src, ImFontBaked* baked, ImFontGlyph* out,
                       unsigned char* pixels, int w, int h, int left, int bottom, float density) {
        (void)src;
        // imgui blends straight alpha
        for (int i = 0; i < w * h; ++i) {
            unsigned char* p = pixels + i * 4;
            if (p[3] != 0 && p[3] != 255)
                for (int c = 0; c < 3; ++c)
                    p[c] = static_cast<unsigned char>(std::min(255, p[c] * 255 / p[3]));
        }
        const ImFontAtlasRectId id = ImFontAtlasPackAddRect(atlas, w, h);
        if (id == ImFontAtlasRectId_Invalid)
            return false;
        ImTextureRect* rect = ImFontAtlasPackGetRect(atlas, id);
        // on the text's baseline: its bottom row `bottom` pixels from it
        out->X0 = static_cast<float>(left) / density;
        out->X1 = static_cast<float>(left + w) / density;
        out->Y0 = static_cast<float>(-(bottom + h)) / density + baked->Ascent;
        out->Y1 = static_cast<float>(-bottom) / density + baked->Ascent;
        out->Visible = true;
        out->Colored = true;
        out->PackId = id;
        ImFontAtlasBakedSetFontGlyphBitmap(atlas, baked, src, out, rect, pixels, ImTextureFormat_RGBA32, w * 4);
        return true;
    }
} // namespace imshell::detail
