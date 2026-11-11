#pragma once

#include "imgui.h"

struct ImFontAtlas;
struct ImFontBaked;
struct ImFontConfig;
struct ImFontGlyph;

namespace imshell::detail {
    // a font loader for one merged source: the system's colour emoji, drawn by the platform's
    // own text engine (coretext) or by freetype from the system's colour font. null where
    // there is none
    const ImFontLoader* EmojiLoader();

    // shared by the loaders: an rgba picture with premultiplied alpha, w x h pixels, its left
    // edge `left` and its bottom `bottom` pixels from the pen on the baseline (y up), into
    // the atlas as the glyph `out`. pixels is changed (alpha is divided out)
    bool PutEmojiGlyph(ImFontAtlas* atlas, ImFontConfig* src, ImFontBaked* baked, ImFontGlyph* out,
                       unsigned char* pixels, int w, int h, int left, int bottom, float density);

    // skin tones (U+1F3FB to U+1F3FF) colour the emoji before them; imgui does not join the
    // two, so the tone's square is not drawn
    inline bool IsSkinTone(unsigned codepoint) {
        return codepoint >= 0x1F3FB && codepoint <= 0x1F3FF;
    }
} // namespace imshell::detail
