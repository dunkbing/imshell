#pragma once

#include "imgui.h"
#include <cstddef>
#include <span>
#include <string>

// one ui font from several files: a text font, icon fonts and other scripts merged into it,
// then the system's chinese/japanese/korean font so user text in those scripts shows
//
//   mydear::AddFont({{text->data, text->size},
//                    {icons->data, icons->size, kIconRanges, true}});
//
// call from Delegate::setup() (the first font added is imgui's default). files embedded with
// mydear_embed_fonts work as they are: data is not copied, so it must outlive the atlas
namespace mydear {
    struct FontFile {
        const void* data = nullptr;
        size_t size = 0;
        const ImWchar* ranges = nullptr; // null = imgui's default (latin)
        bool icons = false;              // no oversampling, snapped to pixels
    };

    // the first file is the base, the rest merge into it; null if the base fails to load.
    // systemCjk merges SystemCjkFontPath() last (mapped, not read: those files are 20+ mb)
    ImFont* AddFont(std::span<const FontFile> files, float size = 16.0f, bool systemCjk = true);
    inline ImFont* AddFont(std::initializer_list<FontFile> files, float size = 16.0f,
                           bool systemCjk = true) {
        return AddFont(std::span(files.begin(), files.size()), size, systemCjk);
    }

    // an installed font with chinese, japanese and korean glyphs, or empty
    std::string SystemCjkFontPath();
} // namespace mydear
