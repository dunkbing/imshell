// the system's colour emoji (Segoe UI Emoji) as glyphs of a merged font source. its colour is
// in layers (COLR), which imgui's rasterizers cannot read: directwrite splits each emoji into
// its coloured layers, direct2d draws them into a dib, and the colour pixels go into the atlas
#include "../emoji.hpp"

#include "imgui_internal.h"

#include <windows.h>
#include <d2d1.h>
#include <dwrite_2.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace imshell::detail {
    namespace {
        struct Emoji {
            IDWriteFactory2* write = nullptr;
            IDWriteFontFace* face = nullptr;
            ID2D1Factory* d2d = nullptr;
            ID2D1DCRenderTarget* target = nullptr;
            ID2D1SolidColorBrush* brush = nullptr;
            DWRITE_FONT_METRICS metrics{};
            float pictureHeight = 0; // design units: how tall an emoji's ink is (see loadGlyph)
        };

        template <class T> void release(T*& p) {
            if (p)
                p->Release();
            p = nullptr;
        }

        void destroy(Emoji* e) {
            release(e->brush);
            release(e->target);
            release(e->d2d);
            release(e->face);
            release(e->write);
            delete e;
        }

        IDWriteFontFace* emojiFace(IDWriteFactory2* write) {
            IDWriteFontCollection* fonts = nullptr;
            IDWriteFontFamily* family = nullptr;
            IDWriteFont* font = nullptr;
            IDWriteFontFace* face = nullptr;
            UINT32 index = 0;
            BOOL exists = FALSE;
            if (SUCCEEDED(write->GetSystemFontCollection(&fonts)) &&
                SUCCEEDED(fonts->FindFamilyName(L"Segoe UI Emoji", &index, &exists)) && exists &&
                SUCCEEDED(fonts->GetFontFamily(index, &family)) &&
                SUCCEEDED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                       DWRITE_FONT_STYLE_NORMAL, &font)))
                font->CreateFontFace(&face);
            release(font);
            release(family);
            release(fonts);
            return face;
        }

        bool srcInit(ImFontAtlas*, ImFontConfig* src) {
            auto* e = new Emoji;
            const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
            const bool ok =
                SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2),
                                              reinterpret_cast<IUnknown**>(&e->write))) &&
                (e->face = emojiFace(e->write)) != nullptr &&
                SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &e->d2d)) &&
                SUCCEEDED(e->d2d->CreateDCRenderTarget(&props, &e->target)) &&
                SUCCEEDED(e->target->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &e->brush));
            if (!ok) {
                destroy(e);
                return false;
            }
            // grayscale: cleartype has nothing to blend against on a transparent picture
            e->target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
            e->face->GetMetrics(&e->metrics);
            // a face's ink, as the picture height every emoji is sized by
            const UINT32 face = 0x1F600;
            UINT16 glyph = 0;
            DWRITE_GLYPH_METRICS gm{};
            if (SUCCEEDED(e->face->GetGlyphIndices(&face, 1, &glyph)) && glyph &&
                SUCCEEDED(e->face->GetDesignGlyphMetrics(&glyph, 1, &gm, FALSE)))
                e->pictureHeight = static_cast<float>(static_cast<INT32>(gm.advanceHeight) - gm.topSideBearing -
                                                      gm.bottomSideBearing);
            if (e->pictureHeight <= 0)
                e->pictureHeight = e->metrics.designUnitsPerEm;
            src->FontLoaderData = e;
            return true;
        }

        void srcDestroy(ImFontAtlas*, ImFontConfig* src) {
            if (src->FontLoaderData)
                destroy(static_cast<Emoji*>(src->FontLoaderData));
            src->FontLoaderData = nullptr;
        }

        UINT16 glyphOf(const Emoji* e, unsigned codepoint) {
            const UINT32 cp = codepoint;
            UINT16 glyph = 0;
            e->face->GetGlyphIndices(&cp, 1, &glyph);
            return glyph;
        }

        int glyphIndex(ImFontAtlas*, ImFontConfig* src, ImWchar codepoint) {
            return glyphOf(static_cast<Emoji*>(src->FontLoaderData), static_cast<unsigned>(codepoint));
        }

        bool containsGlyph(ImFontAtlas*, ImFontConfig*, int glyph) {
            return glyph > 0;
        }

        bool bakedInit(ImFontAtlas*, ImFontConfig*, ImFontBaked*, void*) {
            return true;
        }

        void bakedDestroy(ImFontAtlas*, ImFontConfig*, ImFontBaked*, void*) {}

        // drawn with no width: the skin tones (see IsSkinTone), and the variation selectors
        // (U+FE0E text, U+FE0F emoji style) after a symbol, which segoe gives a full advance
        bool noWidth(const Emoji* e, UINT16 glyph) {
            for (unsigned cp = 0x1F3FB; cp <= 0x1F3FF; ++cp)
                if (glyphOf(e, cp) == glyph)
                    return true;
            return glyph == glyphOf(e, 0xFE0E) || glyph == glyphOf(e, 0xFE0F);
        }

        // the glyph's layers drawn at the pen (originX, originY: the baseline, y down) into the
        // target's dc; a glyph with no colour layers is drawn in one colour
        void drawLayers(Emoji* e, const DWRITE_GLYPH_RUN& run, float originX, float originY) {
            IDWriteColorGlyphRunEnumerator* layers = nullptr;
            if (FAILED(e->write->TranslateColorGlyphRun(originX, originY, &run, nullptr,
                                                        DWRITE_MEASURING_MODE_NATURAL, nullptr, 0, &layers))) {
                e->brush->SetColor(D2D1::ColorF(0, 0, 0, 1));
                e->target->DrawGlyphRun(D2D1::Point2F(originX, originY), &run, e->brush);
                return;
            }
            BOOL more = FALSE;
            while (SUCCEEDED(layers->MoveNext(&more)) && more) {
                const DWRITE_COLOR_GLYPH_RUN* layer = nullptr;
                if (FAILED(layers->GetCurrentRun(&layer)))
                    break;
                // 0xFFFF: the text colour; black, as the other platforms draw it
                const D2D1_COLOR_F colour = layer->paletteIndex == 0xFFFF
                                                ? D2D1::ColorF(0, 0, 0, 1)
                                                : D2D1::ColorF(layer->runColor.r, layer->runColor.g,
                                                               layer->runColor.b, layer->runColor.a);
                e->brush->SetColor(colour);
                e->target->DrawGlyphRun(D2D1::Point2F(layer->baselineOriginX, layer->baselineOriginY),
                                        &layer->glyphRun, e->brush);
            }
            release(layers);
        }

        bool loadGlyph(ImFontAtlas* atlas, ImFontConfig* src, ImFontBaked* baked, void*, int glyphIndex,
                       ImFontGlyph* out, float* outAdvance) {
            auto* e = static_cast<Emoji*>(src->FontLoaderData);
            const float density = src->RasterizerDensity * baked->RasterizerDensity;
            const UINT16 glyph = static_cast<UINT16>(glyphIndex);
            if (noWidth(e, glyph)) {
                if (outAdvance)
                    *outAdvance = 0;
                else
                    out->AdvanceX = 0;
                return true;
            }
            // a picture as tall as the system's emoji on apple (17 px at 16), as on linux: segoe's
            // are taller than an em, so they are sized by their height, not the font's em
            const float scale = baked->Size * density * 1.0625f / e->pictureHeight; // per design unit
            const float em = scale * e->metrics.designUnitsPerEm;
            DWRITE_GLYPH_METRICS gm{};
            e->face->GetDesignGlyphMetrics(&glyph, 1, &gm, FALSE);
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
                return true; // nothing to draw (a space)

            // a top-down 32-bit dib for direct2d to draw into
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof info.bmiHeader;
            info.bmiHeader.biWidth = w;
            info.bmiHeader.biHeight = -h;
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
                if (SUCCEEDED(e->target->BindDC(dc, &rect))) {
                    e->target->BeginDraw();
                    e->target->Clear(D2D1::ColorF(0, 0, 0, 0));
                    DWRITE_GLYPH_RUN run{};
                    run.fontFace = e->face;
                    run.fontEmSize = em;
                    run.glyphCount = 1;
                    run.glyphIndices = &glyph;
                    // the pen: `left` pixels right of the picture's left edge, the baseline
                    // `bottom + h` rows down from its top
                    drawLayers(e, run, static_cast<float>(-left), static_cast<float>(bottom + h));
                    drawn = SUCCEEDED(e->target->EndDraw());
                }
                GdiFlush();
                SelectObject(dc, old);
            }
            bool ok = true;
            if (drawn) {
                atlas->Builder->TempBuffer.resize(w * h * 4);
                unsigned char* pixels = reinterpret_cast<unsigned char*>(atlas->Builder->TempBuffer.Data);
                std::memcpy(pixels, bits, static_cast<size_t>(w) * h * 4);
                for (int i = 0; i < w * h; ++i) // bgra to rgba, still premultiplied
                    std::swap(pixels[i * 4], pixels[i * 4 + 2]);
                ok = PutEmojiGlyph(atlas, src, baked, out, pixels, w, h, left, bottom, density);
            }
            if (dib)
                DeleteObject(dib);
            if (dc)
                DeleteDC(dc);
            return ok;
        }
    } // namespace

    const ImFontLoader* EmojiLoader() {
        static ImFontLoader loader = [] {
            ImFontLoader l;
            l.Name = "Segoe UI Emoji";
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
