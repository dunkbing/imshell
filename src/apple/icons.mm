// the system's SF Symbols as glyphs of a merged font source (macos and ios). each symbol is
// drawn at the font's size by the platform and kept as coverage only, so the text color tints it
#include "../emoji.hpp"
#include "../icons.hpp"

#include "imgui_internal.h"

#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>

namespace imshell::detail {
    namespace {
        // a symbol drawn for a font size: its size in points, and how far its bottom edge lies
        // below the text baseline (symbols carry that so they sit on the baseline like text)
        struct Symbol {
#if TARGET_OS_IPHONE
            UIImage* image = nil;
#else
            NSImage* image = nil;
#endif
            CGSize size = CGSizeZero;
            CGFloat belowBaseline = 0;
        };

        Symbol makeSymbol(const std::string& name, CGFloat points) {
            Symbol s;
            NSString* ns = [NSString stringWithUTF8String:name.c_str()];
#if TARGET_OS_IPHONE
            UIImageSymbolConfiguration* config = [UIImageSymbolConfiguration configurationWithPointSize:points
                                                                                                weight:UIImageSymbolWeightRegular
                                                                                                 scale:UIImageSymbolScaleMedium];
            UIImage* image = [UIImage systemImageNamed:ns withConfiguration:config];
            s.image = [image imageWithTintColor:UIColor.whiteColor renderingMode:UIImageRenderingModeAlwaysOriginal];
            s.size = image.size;
            s.belowBaseline = image.baselineOffsetFromBottom;
#else
            NSImage* image = [NSImage imageWithSystemSymbolName:ns accessibilityDescription:nil];
            NSImageSymbolConfiguration* config = [NSImageSymbolConfiguration configurationWithPointSize:points
                                                                                                 weight:NSFontWeightRegular
                                                                                                  scale:NSImageSymbolScaleMedium];
            s.image = [image imageWithSymbolConfiguration:config];
            s.size = s.image.size;
            s.belowBaseline = s.image.alignmentRect.origin.y;
#endif
            return s;
        }

        bool srcInit(ImFontAtlas*, ImFontConfig*) {
            return true;
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
            if (!name)
                return false;
            // pixels per point is 1: the symbol is made as large as the font is in pixels
            const float density = src->RasterizerDensity * baked->RasterizerDensity;
            const Symbol symbol = makeSymbol(*name, baked->Size * density);
            if (!symbol.image)
                return false;
            const int w = static_cast<int>(std::ceil(symbol.size.width));
            const int h = static_cast<int>(std::ceil(symbol.size.height));
            if (outAdvance) {
                *outAdvance = static_cast<float>(w) / density;
                return true;
            }
            out->AdvanceX = static_cast<float>(w) / density;
            if (w <= 0 || h <= 0)
                return true;

            atlas->Builder->TempBuffer.resize(w * h * 4);
            unsigned char* pixels = reinterpret_cast<unsigned char*>(atlas->Builder->TempBuffer.Data);
            std::memset(pixels, 0, static_cast<size_t>(w) * h * 4);
            CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
            CGContextRef context = CGBitmapContextCreate(pixels, w, h, 8, w * 4, space,
                                                         static_cast<uint32_t>(kCGImageAlphaPremultipliedLast) | static_cast<uint32_t>(kCGBitmapByteOrder32Big));
            CGContextSetInterpolationQuality(context, kCGInterpolationHigh);
            const CGRect rect = CGRectMake(0, 0, symbol.size.width, symbol.size.height);
#if TARGET_OS_IPHONE
            UIGraphicsPushContext(context);
            CGContextTranslateCTM(context, 0, h); // uikit draws from the top left
            CGContextScaleCTM(context, 1, -1);
            [symbol.image drawInRect:rect];
            UIGraphicsPopContext();
#else
            NSGraphicsContext* graphics = [NSGraphicsContext graphicsContextWithCGContext:context flipped:NO];
            [NSGraphicsContext saveGraphicsState];
            NSGraphicsContext.currentContext = graphics;
            [symbol.image drawInRect:NSRectFromCGRect(rect)];
            [NSGraphicsContext restoreGraphicsState];
#endif
            CGContextRelease(context);
            CGColorSpaceRelease(space);
            // coverage only: white, tinted by the text
            for (int i = 0; i < w * h; ++i)
                pixels[i * 4] = pixels[i * 4 + 1] = pixels[i * 4 + 2] = 255;
            return PutEmojiGlyph(atlas, src, baked, out, pixels, w, h, 0, static_cast<int>(std::lround(-symbol.belowBaseline)), density, false);
        }
    } // namespace

    bool IconAvailable(const char* name) {
        NSString* ns = [NSString stringWithUTF8String:name];
#if TARGET_OS_IPHONE
        return ns && [UIImage systemImageNamed:ns] != nil;
#else
        return ns && [NSImage imageWithSystemSymbolName:ns accessibilityDescription:nil] != nil;
#endif
    }

    const ImFontLoader* IconLoader() {
        static ImFontLoader loader = [] {
            ImFontLoader l;
            l.Name = "SF Symbols";
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
