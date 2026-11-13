#pragma once

#include "imshell/editor.hpp"
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

// optional markdown (target imshell::markdown) through imgui_rich_md, which the host provides:
// headings, inline styles, lists, links, tables, and text that can be selected and copied. code
// blocks are drawn by imshell::TextEditor, read-only, with a copy button. colours follow
// CurrentTheme()
namespace imshell::Markdown {
    // a font file in memory; it must outlive the renderer (an embedded file does)
    struct Font {
        const void* data = nullptr;
        size_t size = 0;
    };

    // a button over the top right of a code block, left of copy
    struct CodeAction {
        std::string icon; // the button's label: an icon font glyph or a word
        std::string tooltip;
        std::function<bool(const std::string& language)> applies; // empty: every block
        std::function<void(const std::string& code, const std::string& language)> run;
    };

    struct Options {
        // the text's faces. regular unset: imgui_rich_md's own font; another unset: regular
        Font regular, bold, italic, boldItalic;
        Font code; // inline code. unset: imgui_rich_md's monospace
        // a newline in the source is a line break, as in chat messages
        bool hardBreaks = true;
        // the highlighter for a fence's language name ("cpp", "" without one); empty: plain
        std::function<TextEditor::Highlighter(const std::string& language)> highlighter;
        std::vector<CodeAction> codeActions;
        // the copy button's labels; code blocks draw with the caller's font, icons included
        std::string copyIcon = "Copy";
        std::string copiedIcon = "Copied";
    };
    // before the first Render. the fonts and hardBreaks are read when the renderer starts (the
    // first Render, or the first after Shutdown); the rest applies from the next Render
    void Configure(Options options);

    // call inside a window. idPrefix keeps ImGui ids unique per document on screen
    void Render(const std::string& markdown, const std::string& idPrefix);
    // frees the renderer's fonts and textures; call while the graphics backend is alive.
    // Render works again afterwards
    void Shutdown();
} // namespace imshell::Markdown
