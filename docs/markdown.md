# Markdown

`imshell/markdown.hpp` draws markdown: headings, bold and italic, lists, links, tables, inline code, and text that can be selected and copied. The parsing and layout are [imgui_rich_md](https://github.com/pthom/imgui_rich_md)'s; imshell adds the theme's colours, the app's fonts, and code blocks drawn by a read-only [`TextEditor`](editor.md) with a copy button. It is optional: it lives in its own target, `imshell::markdown`, which exists only when the host provides imgui_rich_md.

## Adding it

imshell neither vendors nor downloads imgui_rich_md. As with Dear ImGui, the host has a checkout (a submodule, for example) and passes its folder before adding imshell:

```cmake
set(IMSHELL_RICH_MD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui_rich_md)
add_subdirectory(external/imshell)
target_link_libraries(myapp PRIVATE imshell::imshell imshell::markdown)
```

imshell then adds that folder itself. imgui_rich_md links a target named `imgui`; when the host has none, imshell makes a header-only one from `IMSHELL_IMGUI_DIR`, since the host compiles Dear ImGui. It also turns `IMGUI_RICHMD_STB_IMAGE_IMPLEMENTATION` off, because imshell already has stb_image.

A host that already adds imgui_rich_md can instead do so before `add_subdirectory(external/imshell)`: imshell uses an existing `imgui_rich_md` target and leaves `IMSHELL_RICH_MD_DIR` alone. That host turns `IMGUI_RICHMD_STB_IMAGE_IMPLEMENTATION` off itself.

`imshell::markdown` links `imshell::editor`. Without the folder or the target there is no `imshell::markdown`, and nothing else changes.

A plain checkout of imgui_rich_md has its md4c submodule empty unless cloned with `--recursive`; it then fetches md4c while configuring.

## Drawing

```cpp
#include "imshell/markdown.hpp"

// each frame, inside a window; the second argument keeps ImGui ids apart per document
imshell::Markdown::Render(message.text, "msg" + std::to_string(message.id));

// before the graphics backend goes away
imshell::Markdown::Shutdown();
```

`Shutdown()` frees the renderer's fonts and textures. `Render` after it starts the renderer again.

## Options

`Configure(options)` before the first `Render`; every field has a default.

| Field | Default | What it is |
|---|---|---|
| `regular`, `bold`, `italic`, `boldItalic` | imgui_rich_md's own font (Roboto) | The text's faces as font files in memory (`{data, size}`), which must stay alive: files embedded with `imshell_embed_fonts` do. With `regular` set, a face left unset uses `regular`. |
| `code` | imgui_rich_md's monospace | The font of inline code. |
| `hardBreaks` | `true` | A newline in the source is a line break, as in chat messages. `false` is CommonMark's rule. |
| `highlighter` | none: plain text | `fn(language)` returns the `TextEditor::Highlighter` for a fence's language name (`"cpp"`, or `""` for a fence without one). Asked once per code block, and again when its language changes. |
| `codeActions` | none | Buttons over a code block, left of copy. Each has an `icon` (the label), a `tooltip`, `applies(language)` (empty: every block) and `run(code, language)`. |
| `copyIcon`, `copiedIcon` | `"Copy"`, `"Copied"` | The copy button's labels. |

The fonts and `hardBreaks` are read when the renderer starts: the first `Render`, or the first after `Shutdown()`. The rest applies from the next `Render`.

```cpp
imshell::Markdown::Options options;
const EmbeddedFont* font = findEmbeddedFont("MyFont-Regular");
options.regular = options.code = {font->data, font->size};
options.highlighter = [](const std::string& language) { return highlighterFor(language); };
options.copyIcon = ICON_COPY; // a glyph of the app's icon font
options.codeActions.push_back({.icon = ICON_PLAY,
                               .tooltip = "Run",
                               .applies = [](const std::string& language) { return language == "sh"; },
                               .run = [](const std::string& code, const std::string&) { run(code); }});
imshell::Markdown::Configure(options);
```

## Behaviour worth knowing

- Colours come from `Theme::Current()` each frame: inline code is the theme's `peach`, a code block sits on `crust` with a `surface1` border, and its text uses the editor's palette.
- A code block is a read-only `TextEditor` without line numbers: its text can be selected and copied, and a long line scrolls sideways. It draws with the font that was current when `Render` was called, not the markdown font, so labels can be glyphs of the app's icon font.
- Code blocks are remembered by `idPrefix` and their position in the document, so a block that grows while a reply streams keeps its window and scroll. One not drawn for 600 frames is dropped.
- There is one renderer for the whole app. Its fonts load on demand into ImGui's atlas, which needs a renderer backend with `ImGuiBackendFlags_RendererHasTextures` (Dear ImGui 1.92 or later); imshell's shells have it.
- imgui_rich_md's other features (mermaid diagrams, images) work as it builds them; imshell sets none of its build options except the stb_image one.
- `tests/markdown_test.cpp` draws a document on a bare ImGui context, with a few lines standing in for the renderer backend's texture handling, and checks the highlighter and action hooks and a `Render` after `Shutdown()` (`ctest`, when imgui_rich_md is given).
