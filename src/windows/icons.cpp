// the system's icon font as glyphs of a merged font source on windows: Segoe Fluent Icons
// (windows 11), else Segoe MDL2 Assets (windows 10). an SF Symbol name maps to a codepoint of
// the font; a glyph is drawn by directwrite into a dib and kept as coverage only, so the text
// colour tints it
#include "../emoji.hpp"
#include "../icons.hpp"

#include "imgui_internal.h"

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>

#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>

namespace imshell::detail {
    namespace {
        // names are SF Symbols (what Icon() takes on every platform); codepoints are from
        // Microsoft's Segoe Fluent Icons list. a name with no entry has no icon here (the
        // caller's fallback shows)
        const std::unordered_map<std::string, unsigned>& names() {
            static const std::unordered_map<std::string, unsigned> map = {
                {"gearshape", 0xE713}, {"gear", 0xE713}, {"trash", 0xE74D}, {"doc.on.doc", 0xE8C8},
                {"pencil", 0xE70F}, {"plus", 0xE710}, {"minus", 0xE738}, {"xmark", 0xE711},
                {"checkmark", 0xE73E}, {"checkmark.circle", 0xE930}, {"magnifyingglass", 0xE721},
                {"folder", 0xE8B7}, {"folder.badge.plus", 0xE8F4}, {"doc", 0xE7C3}, {"doc.text", 0xE8A5},
                {"square.and.arrow.up", 0xE72D}, {"square.and.arrow.down", 0xE896},
                {"arrow.clockwise", 0xE72C}, {"arrow.triangle.2.circlepath", 0xE895},
                {"chevron.right", 0xE76C}, {"chevron.left", 0xE76B}, {"chevron.down", 0xE70D},
                {"chevron.up", 0xE70E}, {"star", 0xE734}, {"star.fill", 0xE735}, {"heart", 0xEB51},
                {"heart.fill", 0xEB52}, {"house", 0xE80F}, {"person", 0xE77B}, {"bell", 0xEA8F},
                {"lock", 0xE72E}, {"lock.open", 0xE785}, {"eye", 0xE890}, {"info.circle", 0xE946},
                {"exclamationmark.triangle", 0xE7BA}, {"play", 0xE768}, {"play.fill", 0xE768},
                {"pause", 0xE769}, {"pause.fill", 0xE769}, {"stop", 0xE71A}, {"stop.fill", 0xE71A},
                {"paperclip", 0xE723}, {"link", 0xE71B}, {"tag", 0xE8EC}, {"calendar", 0xE787},
                {"clock", 0xE917}, {"cart", 0xE719}, {"printer", 0xE749}, {"envelope", 0xE715},
                {"ellipsis", 0xE712}, {"line.3.horizontal", 0xE700}, {"arrow.left", 0xE72B},
                {"arrow.right", 0xE72A}, {"arrow.up", 0xE74A}, {"arrow.down", 0xE74B},
                {"camera", 0xE722}, {"globe", 0xE774}, {"wifi", 0xE701}, {"bookmark", 0xE8A4},
                {"pin", 0xE718}, {"flag", 0xE7C1}, {"phone", 0xE717}, {"mic", 0xE720},
                {"power", 0xE7E8}, {"scissors", 0xE8C6}, {"doc.on.clipboard", 0xE77F},
                {"arrow.uturn.backward", 0xE7A7}, {"arrow.uturn.forward", 0xE7A6},
                {"line.3.horizontal.decrease", 0xE71C}, {"icloud", 0xE753}, {"video", 0xE714},
                {"photo", 0xE91B}, {"questionmark.circle", 0xE897}, {"paperplane", 0xE724},
                {"speaker", 0xE767}, {"speaker.slash", 0xE74F}, {"message", 0xE8BD},
                {"chevron.left.forwardslash.chevron.right", 0xE943}, {"books.vertical", 0xE8F1},
            };
            return map;
        }

        IDWriteFontFace* iconFace(IDWriteFactory* write) {
            IDWriteFontCollection* fonts = nullptr;
            IDWriteFontFace* face = nullptr;
            if (SUCCEEDED(write->GetSystemFontCollection(&fonts))) {
                for (const wchar_t* name : {L"Segoe Fluent Icons", L"Segoe MDL2 Assets"}) {
                    UINT32 index = 0;
                    BOOL exists = FALSE;
                    IDWriteFontFamily* family = nullptr;
                    IDWriteFont* font = nullptr;
                    if (SUCCEEDED(fonts->FindFamilyName(name, &index, &exists)) && exists &&
                        SUCCEEDED(fonts->GetFontFamily(index, &family)) &&
                        SUCCEEDED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                               DWRITE_FONT_STYLE_NORMAL, &font)))
                        font->CreateFontFace(&face);
                    if (font)
                        font->Release();
                    if (family)
                        family->Release();
                    if (face)
                        break;
                }
                fonts->Release();
            }
            return face;
        }

        // one face for the lookups (the loader makes its own), kept for the process
        IDWriteFontFace* lookupFace() {
            static IDWriteFontFace* face = [] {
                IDWriteFactory* write = nullptr;
                if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&write))))
                    return static_cast<IDWriteFontFace*>(nullptr);
                return iconFace(write);
            }();
            return face;
        }

        // the glyph of a name in a face, or 0
        UINT16 glyphOf(IDWriteFontFace* face, const std::string& name) {
            const auto it = names().find(name);
            if (it == names().end() || !face)
                return 0;
            const UINT32 cp = it->second;
            UINT16 glyph = 0;
            face->GetGlyphIndices(&cp, 1, &glyph);
            return glyph;
        }

        struct Icons {
            IDWriteFactory* write = nullptr;
            IDWriteFontFace* face = nullptr;
            ID2D1Factory* d2d = nullptr;
            ID2D1DCRenderTarget* target = nullptr;
            ID2D1SolidColorBrush* brush = nullptr;
            DWRITE_FONT_METRICS metrics{};
        };

        template <class T> void release(T*& p) {
            if (p)
                p->Release();
            p = nullptr;
        }

        void destroy(Icons* i) {
            release(i->brush);
            release(i->target);
            release(i->d2d);
            release(i->face);
            release(i->write);
            delete i;
        }

        bool srcInit(ImFontAtlas*, ImFontConfig* src) {
            auto* i = new Icons;
            const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
            const bool ok =
                SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                              reinterpret_cast<IUnknown**>(&i->write))) &&
                (i->face = iconFace(i->write)) != nullptr &&
                SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &i->d2d)) &&
                SUCCEEDED(i->d2d->CreateDCRenderTarget(&props, &i->target)) &&
                SUCCEEDED(i->target->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &i->brush));
            if (!ok) {
                destroy(i);
                return false;
            }
            i->target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE); // cleartype has nothing to blend against
            i->face->GetMetrics(&i->metrics);
            src->FontLoaderData = i;
            return true;
        }

        void srcDestroy(ImFontAtlas*, ImFontConfig* src) {
            if (src->FontLoaderData)
                destroy(static_cast<Icons*>(src->FontLoaderData));
            src->FontLoaderData = nullptr;
        }

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
            auto* i = static_cast<Icons*>(src->FontLoaderData);
            const std::string* name = IconName(kIconBase + static_cast<unsigned>(glyphIndex) - 1);
            if (!name)
                return false;
            const UINT16 glyph = glyphOf(i->face, *name);
            if (!glyph)
                return false;
            const float density = src->RasterizerDensity * baked->RasterizerDensity;
            const float em = baked->Size * density;
            const float scale = em / i->metrics.designUnitsPerEm; // pixels per design unit
            DWRITE_GLYPH_METRICS gm{};
            i->face->GetDesignGlyphMetrics(&glyph, 1, &gm, FALSE);
            const float advance = gm.advanceWidth * scale / density;
            if (outAdvance) {
                *outAdvance = advance;
                return true;
            }
            out->AdvanceX = advance;
            // the ink box (y up from the baseline), a pixel wider for antialiasing
            const float x0 = gm.leftSideBearing * scale;
            const float x1 = (static_cast<INT32>(gm.advanceWidth) - gm.rightSideBearing) * scale;
            const float y1 = (gm.verticalOriginY - gm.topSideBearing) * scale;
            const float y0 = (gm.verticalOriginY - static_cast<INT32>(gm.advanceHeight) + gm.bottomSideBearing) * scale;
            const int left = static_cast<int>(std::floor(x0)) - 1;
            const int bottom = static_cast<int>(std::floor(y0)) - 1;
            const int w = static_cast<int>(std::ceil(x1)) + 1 - left;
            const int h = static_cast<int>(std::ceil(y1)) + 1 - bottom;
            if (x1 <= x0 || y1 <= y0 || w <= 0 || h <= 0)
                return true;

            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof info.bmiHeader;
            info.bmiHeader.biWidth = w;
            info.bmiHeader.biHeight = -h; // top down
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr;
            HDC dc = CreateCompatibleDC(nullptr);
            HBITMAP dib = dc ? CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
            bool drawn = false;
            if (dib) {
                HGDIOBJ old = SelectObject(dc, dib);
                std::memset(bits, 0, static_cast<size_t>(w) * h * 4);
                const RECT rect{0, 0, w, h};
                if (SUCCEEDED(i->target->BindDC(dc, &rect))) {
                    i->target->BeginDraw();
                    i->target->Clear(D2D1::ColorF(0, 0, 0, 0));
                    DWRITE_GLYPH_RUN run{};
                    run.fontFace = i->face;
                    run.fontEmSize = em;
                    run.glyphCount = 1;
                    run.glyphIndices = &glyph;
                    // the pen: `left` pixels right of the picture's left edge, the baseline `bottom + h` rows down from its top
                    i->target->DrawGlyphRun(D2D1::Point2F(static_cast<float>(-left), static_cast<float>(bottom + h)), &run, i->brush);
                    drawn = SUCCEEDED(i->target->EndDraw());
                }
                GdiFlush();
                SelectObject(dc, old);
            }
            bool ok = true;
            if (drawn) {
                atlas->Builder->TempBuffer.resize(w * h * 4);
                unsigned char* pixels = reinterpret_cast<unsigned char*>(atlas->Builder->TempBuffer.Data);
                std::memcpy(pixels, bits, static_cast<size_t>(w) * h * 4);
                // coverage only: white, tinted by the text; the brush was black, so the alpha is the coverage
                for (int p = 0; p < w * h; ++p)
                    pixels[p * 4] = pixels[p * 4 + 1] = pixels[p * 4 + 2] = 255;
                ok = PutEmojiGlyph(atlas, src, baked, out, pixels, w, h, left, bottom, density, false);
            }
            if (dib)
                DeleteObject(dib);
            if (dc)
                DeleteDC(dc);
            return ok;
        }
    } // namespace

    bool IconAvailable(const char* name) {
        return glyphOf(lookupFace(), name) != 0;
    }

    const ImFontLoader* IconLoader() {
        if (!lookupFace())
            return nullptr; // neither icon font is installed (a server edition)
        static ImFontLoader loader = [] {
            ImFontLoader l;
            l.Name = "Segoe Fluent Icons";
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
