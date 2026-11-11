// the system's colour emoji (Apple Color Emoji) as glyphs of a merged font source. its glyphs
// are pictures at fixed sizes, which imgui's rasterizers cannot read: coretext draws each one,
// scaled down from the 64 px picture, and the colour pixels go into the atlas
#include "../emoji.hpp"

#include "imgui_internal.h"

#import <CoreText/CoreText.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace imshell::detail {
    namespace {
        bool srcInit(ImFontAtlas*, ImFontConfig* src) {
            src->FontLoaderData = (void*)CTFontCreateWithName(CFSTR("AppleColorEmoji"), 64, nullptr);
            return src->FontLoaderData != nullptr;
        }

        void srcDestroy(ImFontAtlas*, ImFontConfig* src) {
            if (src->FontLoaderData)
                CFRelease((CTFontRef)src->FontLoaderData);
            src->FontLoaderData = nullptr;
        }

        int glyphIndex(ImFontAtlas*, ImFontConfig* src, ImWchar codepoint) {
            UniChar text[2];
            CGGlyph glyphs[2] = {0, 0};
            int count = 1;
            const unsigned cp = static_cast<unsigned>(codepoint);
            if (cp >= 0x10000) { // a surrogate pair; needs IMGUI_USE_WCHAR32 to get here at all
                text[0] = static_cast<UniChar>(0xD800 + ((cp - 0x10000) >> 10));
                text[1] = static_cast<UniChar>(0xDC00 + ((cp - 0x10000) & 0x3FF));
                count = 2;
            } else {
                text[0] = static_cast<UniChar>(cp);
            }
            return CTFontGetGlyphsForCharacters((CTFontRef)src->FontLoaderData, text, glyphs, count) ? glyphs[0] : 0;
        }

        bool containsGlyph(ImFontAtlas*, ImFontConfig*, int glyph) {
            return glyph > 0;
        }

        bool bakedInit(ImFontAtlas*, ImFontConfig*, ImFontBaked*, void*) {
            return true;
        }

        void bakedDestroy(ImFontAtlas*, ImFontConfig*, ImFontBaked*, void*) {}

        // skin tones (U+1F3FB to U+1F3FF) colour the emoji before them; imgui does not join the
        // two, so the tone's own square is left out rather than drawn beside it
        bool isSkinTone(CTFontRef font, CGGlyph glyph) {
            static const std::vector<CGGlyph> tones = [font] {
                std::vector<CGGlyph> out;
                for (unsigned cp = 0x1F3FB; cp <= 0x1F3FF; ++cp) {
                    const UniChar text[2] = {static_cast<UniChar>(0xD800 + ((cp - 0x10000) >> 10)),
                                             static_cast<UniChar>(0xDC00 + ((cp - 0x10000) & 0x3FF))};
                    CGGlyph g[2] = {0, 0};
                    if (CTFontGetGlyphsForCharacters(font, text, g, 2))
                        out.push_back(g[0]);
                }
                return out;
            }();
            return std::find(tones.begin(), tones.end(), glyph) != tones.end();
        }

        bool loadGlyph(ImFontAtlas* atlas, ImFontConfig* src, ImFontBaked* baked, void*, int glyphIndex,
                       ImFontGlyph* out, float* outAdvance) {
            const float density = src->RasterizerDensity * baked->RasterizerDensity;
            CTFontRef font = (CTFontRef)src->FontLoaderData;
            if (isSkinTone(font, static_cast<CGGlyph>(glyphIndex))) {
                if (outAdvance)
                    *outAdvance = 0;
                else
                    out->AdvanceX = 0;
                return true;
            }
            // the pictures come at fixed sizes; at another size coretext would draw the nearest
            // one as it is, so it is drawn from the large one and scaled
            const CGFloat scale = baked->Size * density / CTFontGetSize(font);
            CGGlyph glyph = static_cast<CGGlyph>(glyphIndex);
            CGSize advance;
            CTFontGetAdvancesForGlyphs(font, kCTFontOrientationHorizontal, &glyph, &advance, 1);
            if (outAdvance) {
                *outAdvance = static_cast<float>(advance.width * scale) / density;
                return true;
            }
            out->AdvanceX = static_cast<float>(advance.width * scale) / density;
            CGRect box = CTFontGetBoundingRectsForGlyphs(font, kCTFontOrientationHorizontal, &glyph, nullptr, 1);
            box = CGRectMake(box.origin.x * scale, box.origin.y * scale, box.size.width * scale, box.size.height * scale);
            const int left = static_cast<int>(std::floor(box.origin.x));
            const int bottom = static_cast<int>(std::floor(box.origin.y));
            const int w = static_cast<int>(std::ceil(box.origin.x + box.size.width)) - left;
            const int h = static_cast<int>(std::ceil(box.origin.y + box.size.height)) - bottom;
            if (w <= 0 || h <= 0)
                return true; // nothing to draw (a space)

            atlas->Builder->TempBuffer.resize(w * h * 4);
            unsigned char* pixels = reinterpret_cast<unsigned char*>(atlas->Builder->TempBuffer.Data);
            std::memset(pixels, 0, static_cast<size_t>(w) * h * 4);
            CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
            CGContextRef context = CGBitmapContextCreate(pixels, w, h, 8, w * 4, space,
                                                         kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
            CGContextSetInterpolationQuality(context, kCGInterpolationHigh);
            CGContextTranslateCTM(context, -left, -bottom);
            CGContextScaleCTM(context, scale, scale);
            const CGPoint origin = CGPointZero;
            CTFontDrawGlyphs(font, &glyph, &origin, 1, context);
            CGContextRelease(context);
            CGColorSpaceRelease(space);
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
            // on the text's baseline: its bottom row at `bottom` pixels below it
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
    } // namespace

    const ImFontLoader* EmojiLoader() {
        static ImFontLoader loader = [] {
            ImFontLoader l;
            l.Name = "Apple Color Emoji";
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
