// the system's colour emoji (Noto Color Emoji on linux and android) as glyphs of a merged font
// source, through freetype. the font holds pictures at one large size, which imgui's freetype
// loader cannot use (it asks for the exact size): the picture is loaded at its own size and
// scaled down here. IMSHELL_EMOJI_FONT names another file
#include "emoji.hpp"

#include "imgui_internal.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace imshell::detail {
    namespace {
        constexpr int kTone = 0x7FFFFF; // no font has this many glyphs

        FT_Library library() {
            static FT_Library lib = [] {
                FT_Library l = nullptr;
                return FT_Init_FreeType(&l) == 0 ? l : nullptr;
            }();
            return lib;
        }

        std::string fontPath() {
            if (const char* path = std::getenv("IMSHELL_EMOJI_FONT"))
                return path;
            static const char* candidates[] = {
#if defined(__ANDROID__)
                "/system/fonts/NotoColorEmoji.ttf",
                "/system/fonts/NotoColorEmojiLegacy.ttf",
#else
                "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf",     // debian, ubuntu
                "/usr/share/fonts/google-noto-emoji/NotoColorEmoji.ttf", // fedora
                "/usr/share/fonts/noto/NotoColorEmoji.ttf",              // arch
                "/usr/share/fonts/noto-emoji/NotoColorEmoji.ttf",
                "/usr/share/fonts/TTF/NotoColorEmoji.ttf",
#endif
            };
            std::error_code ec;
            for (const char* path : candidates)
                if (std::filesystem::exists(path, ec))
                    return path;
            return {};
        }

        bool srcInit(ImFontAtlas*, ImFontConfig* src) {
            const std::string path = fontPath();
            FT_Face face = nullptr;
            if (path.empty() || !library() || FT_New_Face(library(), path.c_str(), 0, &face) != 0)
                return false;
            // the largest picture size: drawn down to every size asked for
            if (FT_HAS_FIXED_SIZES(face) && face->num_fixed_sizes > 0) {
                int best = 0;
                for (int i = 1; i < face->num_fixed_sizes; ++i)
                    if (face->available_sizes[i].height > face->available_sizes[best].height)
                        best = i;
                FT_Select_Size(face, best);
            } else {
                FT_Set_Pixel_Sizes(face, 0, 128);
            }
            src->FontLoaderData = face;
            return true;
        }

        void srcDestroy(ImFontAtlas*, ImFontConfig* src) {
            if (src->FontLoaderData)
                FT_Done_Face(static_cast<FT_Face>(src->FontLoaderData));
            src->FontLoaderData = nullptr;
        }

        int glyphIndex(ImFontAtlas*, ImFontConfig* src, ImWchar codepoint) {
            const FT_UInt glyph = FT_Get_Char_Index(static_cast<FT_Face>(src->FontLoaderData), static_cast<FT_ULong>(codepoint));
            // tones claimed with an index of their own (above any real one): drawn with no width
            return glyph != 0 && IsSkinTone(static_cast<unsigned>(codepoint)) ? kTone : static_cast<int>(glyph);
        }

        bool containsGlyph(ImFontAtlas*, ImFontConfig*, int glyph) {
            return glyph != 0;
        }

        bool bakedInit(ImFontAtlas*, ImFontConfig*, ImFontBaked*, void*) {
            return true;
        }

        void bakedDestroy(ImFontAtlas*, ImFontConfig*, ImFontBaked*, void*) {}

        bool loadGlyph(ImFontAtlas* atlas, ImFontConfig* src, ImFontBaked* baked, void*, int glyphIndex,
                       ImFontGlyph* out, float* outAdvance) {
            if (glyphIndex == kTone) {
                if (outAdvance)
                    *outAdvance = 0;
                else
                    out->AdvanceX = 0;
                return true;
            }
            FT_Face face = static_cast<FT_Face>(src->FontLoaderData);
            if (FT_Load_Glyph(face, static_cast<FT_UInt>(glyphIndex), FT_LOAD_COLOR) != 0)
                return false;
            const float density = src->RasterizerDensity * baked->RasterizerDensity;
            // a picture as tall as the system's emoji on apple (17 px at 16): noto's are wider than
            // an em, so they are sized by their height, not the font's em
            const float pictureHeight = FT_HAS_FIXED_SIZES(face) && face->num_fixed_sizes > 0
                                            ? static_cast<float>(face->size->metrics.height) / 64.0f
                                            : static_cast<float>(face->size->metrics.y_ppem);
            const float scale = baked->Size * density * 1.0625f / pictureHeight;
            const float advance = static_cast<float>(face->glyph->advance.x) / 64.0f * scale / density;
            if (outAdvance) {
                *outAdvance = advance;
                return true;
            }
            out->AdvanceX = advance;
            if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0)
                return false;
            const FT_Bitmap& bm = face->glyph->bitmap;
            if (bm.width == 0 || bm.rows == 0)
                return true; // nothing to draw
            const int sw = static_cast<int>(bm.width), sh = static_cast<int>(bm.rows);
            const int left = static_cast<int>(std::floor(face->glyph->bitmap_left * scale));
            const int top = static_cast<int>(std::ceil(face->glyph->bitmap_top * scale));
            const int w = std::max(1, static_cast<int>(std::lround(sw * scale)));
            const int h = std::max(1, static_cast<int>(std::lround(sh * scale)));

            // area average of the source pixels under each target pixel, in premultiplied rgba
            atlas->Builder->TempBuffer.resize(w * h * 4);
            unsigned char* pixels = reinterpret_cast<unsigned char*>(atlas->Builder->TempBuffer.Data);
            const bool bgra = bm.pixel_mode == FT_PIXEL_MODE_BGRA;
            for (int y = 0; y < h; ++y) {
                const int y0 = y * sh / h, y1 = std::max(y0 + 1, (y + 1) * sh / h);
                for (int x = 0; x < w; ++x) {
                    const int x0 = x * sw / w, x1 = std::max(x0 + 1, (x + 1) * sw / w);
                    unsigned sum[4] = {0, 0, 0, 0}, n = 0;
                    for (int sy = y0; sy < y1; ++sy) {
                        const unsigned char* row = bm.buffer + sy * bm.pitch;
                        for (int sx = x0; sx < x1; ++sx, ++n) {
                            if (bgra) { // premultiplied already
                                const unsigned char* p = row + sx * 4;
                                sum[0] += p[2], sum[1] += p[1], sum[2] += p[0], sum[3] += p[3];
                            } else { // a plain outline glyph: white, as coverage
                                const unsigned a = row[sx];
                                sum[0] += a, sum[1] += a, sum[2] += a, sum[3] += a;
                            }
                        }
                    }
                    unsigned char* d = pixels + (y * w + x) * 4;
                    for (int c = 0; c < 4; ++c)
                        d[c] = static_cast<unsigned char>(sum[c] / n);
                }
            }
            return PutEmojiGlyph(atlas, src, baked, out, pixels, w, h, left, top - h, density);
        }
    } // namespace

    const ImFontLoader* EmojiLoader() {
        static ImFontLoader loader = [] {
            ImFontLoader l;
            l.Name = "Noto Color Emoji";
            l.FontSrcInit = srcInit;
            l.FontSrcDestroy = srcDestroy;
            l.FontSrcGetGlyphIndexFromCodepoint = glyphIndex;
            l.FontSrcContainsGlyph = containsGlyph;
            l.FontBakedInit = bakedInit;
            l.FontBakedDestroy = bakedDestroy;
            l.FontBakedLoadGlyph = loadGlyph;
            return l;
        }();
        // no emoji font on this machine: nothing to merge
        return fontPath().empty() ? nullptr : &loader;
    }
} // namespace imshell::detail
