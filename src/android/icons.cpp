// icons on android: the system has no icon font, so a cut-down Material Symbols Outlined is
// compiled in (material_symbols.inc, a few KB) and drawn through freetype as glyphs of a merged
// font source. an SF Symbol name maps to a codepoint of that font; a glyph is kept as coverage
// only, so the text colour tints it
#include "../emoji.hpp"
#include "../icons.hpp"

#include "imgui_internal.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>

#include "material_symbols.inc"

namespace imshell::detail {
    namespace {
        // names are SF Symbols (what Icon() takes on every platform); a name with no entry has
        // no icon here (the caller's fallback shows)
        const std::unordered_map<std::string, FT_ULong>& names() {
            static const std::unordered_map<std::string, FT_ULong> map = {
                {"gearshape", 0xe8b8},
                {"gear", 0xe8b8},
                {"trash", 0xe92e},
                {"doc.on.doc", 0xe14d},
                {"pencil", 0xf097},
                {"plus", 0xe145},
                {"minus", 0xe15b},
                {"xmark", 0xe5cd},
                {"checkmark", 0xe668},
                {"magnifyingglass", 0xef7a},
                {"folder", 0xe2c7},
                {"folder.badge.plus", 0xe2cc},
                {"doc", 0xe873},
                {"doc.text", 0xe873},
                {"square.and.arrow.up", 0xe6b8},
                {"square.and.arrow.down", 0xf090},
                {"arrow.clockwise", 0xe5d5},
                {"chevron.right", 0xe5cc},
                {"chevron.left", 0xe5cb},
                {"chevron.down", 0xe5cf},
                {"chevron.up", 0xe5ce},
                {"star", 0xf09a},
                {"heart", 0xe87e},
                {"house", 0xe9b2},
                {"person", 0xf0d3},
                {"bell", 0xe7f5},
                {"lock", 0xe899},
                {"lock.open", 0xe898},
                {"eye", 0xe8f4},
                {"info.circle", 0xe88e},
                {"exclamationmark.triangle", 0xf083},
                {"play", 0xe037},
                {"pause", 0xe034},
                {"stop", 0xe047},
                {"paperclip", 0xe226},
                {"link", 0xe250},
                {"tag", 0xf05b},
                {"calendar", 0xebcc},
                {"clock", 0xefd6},
                {"printer", 0xe8ad},
                {"envelope", 0xe159},
                {"paperplane", 0xe163},
                {"ellipsis", 0xe5d3},
                {"line.3.horizontal", 0xe5d2},
                {"arrow.left", 0xe5c4},
                {"arrow.right", 0xe5c8},
                {"arrow.up", 0xe5d8},
                {"arrow.down", 0xe5db},
                {"camera", 0xe412},
                {"globe", 0xea07},
                {"wifi", 0xe63e},
                {"bookmark", 0xe8e7},
                {"mic", 0xe31d},
                {"power", 0xf8c7},
                {"scissors", 0xe14e},
                {"doc.on.clipboard", 0xe14f},
                {"arrow.uturn.backward", 0xe166},
                {"arrow.uturn.forward", 0xe15a},
                {"video", 0xe04b},
                {"photo", 0xe3f4},
                {"questionmark.circle", 0xe8fd},
                {"speaker", 0xe050},
                {"speaker.slash", 0xe04f},
                {"message", 0xe0c9},
                {"tray", 0xe156},
            };
            return map;
        }

        struct Library {
            FT_Library library = nullptr;
            FT_Face face = nullptr;
        };

        // one face for the process; a face is not thread safe, and glyphs load on the font thread
        Library& lib() {
            static Library l = [] {
                Library r;
                if (FT_Init_FreeType(&r.library) == 0 &&
                    FT_New_Memory_Face(r.library, kMaterialSymbols, sizeof kMaterialSymbols, 0, &r.face) != 0)
                    r.face = nullptr;
                return r;
            }();
            return l;
        }

        FT_UInt glyphOf(const std::string& name) {
            const auto it = names().find(name);
            Library& l = lib();
            return it == names().end() || !l.face ? 0 : FT_Get_Char_Index(l.face, it->second);
        }

        bool srcInit(ImFontAtlas*, ImFontConfig*) {
            return lib().face != nullptr;
        }

        void srcDestroy(ImFontAtlas*, ImFontConfig*) {}

        // the glyph index is the codepoint's place in the block, plus one (0 is "none")
        int glyphIndex(ImFontAtlas*, ImFontConfig*, ImWchar codepoint) {
            const unsigned cp = static_cast<unsigned>(codepoint);
            return IconName(cp) ? static_cast<int>(cp - kIconBase) + 1 : 0;
        }

        bool containsGlyph(ImFontAtlas*, ImFontConfig*, int glyph) {
            return glyph > 0;
        }

        bool bakedInit(ImFontAtlas*, ImFontConfig*, ImFontBaked*, void*) {
            return true;
        }

        void bakedDestroy(ImFontAtlas*, ImFontConfig*, ImFontBaked*, void*) {}

        bool loadGlyph(ImFontAtlas* atlas, ImFontConfig* src, ImFontBaked* baked, void*, int glyphIndex,
                       ImFontGlyph* out, float* outAdvance) {
            const std::string* name = IconName(kIconBase + static_cast<unsigned>(glyphIndex) - 1);
            const FT_UInt glyph = name ? glyphOf(*name) : 0;
            if (!glyph)
                return false;
            const float density = src->RasterizerDensity * baked->RasterizerDensity;
            const float em = baked->Size * density;
            const int size = std::max(1, static_cast<int>(std::lround(em)));
            if (outAdvance) {
                *outAdvance = static_cast<float>(size) / density;
                return true;
            }
            out->AdvanceX = static_cast<float>(size) / density;
            FT_Face face = lib().face;
            if (FT_Set_Pixel_Sizes(face, 0, size) != 0 || FT_Load_Glyph(face, glyph, FT_LOAD_RENDER) != 0)
                return true; // advance only
            const FT_Bitmap& bitmap = face->glyph->bitmap;
            const int w = static_cast<int>(bitmap.width), h = static_cast<int>(bitmap.rows);
            if (w <= 0 || h <= 0 || bitmap.pixel_mode != FT_PIXEL_MODE_GRAY)
                return true;
            atlas->Builder->TempBuffer.resize(w * h * 4);
            unsigned char* pixels = reinterpret_cast<unsigned char*>(atlas->Builder->TempBuffer.Data);
            // coverage only: white, tinted by the text; the bitmap is the alpha
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    unsigned char* p = pixels + (y * w + x) * 4;
                    p[0] = p[1] = p[2] = 255;
                    p[3] = bitmap.buffer[y * bitmap.pitch + x];
                }
            // centred on the text: the middle of a capital sits about 0.35 em above the baseline
            return PutEmojiGlyph(atlas, src, baked, out, pixels, w, h, face->glyph->bitmap_left,
                                 static_cast<int>(std::lround(0.35f * em - h / 2.0f)), density, false);
        }
    } // namespace

    bool IconAvailable(const char* name) {
        return glyphOf(name) != 0;
    }

    const ImFontLoader* IconLoader() {
        if (!lib().face)
            return nullptr;
        static ImFontLoader loader = [] {
            ImFontLoader l;
            l.Name = "Material Symbols";
            l.FontSrcInit = srcInit;
            l.FontSrcDestroy = srcDestroy;
            l.FontSrcGetGlyphIndexFromCodepoint = glyphIndex;
            l.FontSrcContainsGlyph = containsGlyph;
            l.FontBakedInit = bakedInit;
            l.FontBakedDestroy = bakedDestroy;
            l.FontBakedLoadGlyph = loadGlyph;
            return l;
        }();
        return &loader;
    }
} // namespace imshell::detail
