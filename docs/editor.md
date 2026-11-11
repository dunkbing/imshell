# Editor

`imshell/editor.hpp` is a multi-line code editor, `imshell::TextEditor`: typing, selection, undo and redo, line numbers, find and replace, a read-only mode, and a completion popup. It is optional: it lives in its own target, `imshell::editor`, and nothing in imshell's core uses it, so an app that doesn't link it pays nothing. It draws with `Theme::Current()` and uses only ImGui's input, so it runs on every shell.

```cmake
target_link_libraries(myapp PRIVATE imshell::imshell imshell::editor)
```

```cpp
#include "imshell/editor.hpp"

imshell::TextEditor editor; // keep it between frames
editor.SetText("let x = 1\n");
editor.SetSubmitCallback([&] { run(editor.HasSelection() ? editor.GetSelectedText() : editor.GetText()); });

// each frame, inside a window; the size is a child window's
editor.Render("##code", ImVec2(0, 300));
```

A read-only code view with selection and copy is the same class:

```cpp
view.SetReadOnly(true);
view.SetText(code);
view.Render("##block", ImVec2(0, height), true);
```

`examples/gallery` has one with a small lexer and keyword completion.

## What the app fills in

The editor knows nothing about a language. What depends on one is a hook:

| Hook | Call | Notes |
|---|---|---|
| Highlighting | `SetHighlighter(fn)` | `fn(text, tokens, edit)` sets `tokens[i]` for byte `i`: `Token::Text`, `Keyword`, `String`, `Number`, `Comment`, `Function`, `Type` or `Operator`. It runs when the text changes, once, before the next draw. `edit` says what changed since the last call (`text[start, newEnd)` replaced `[start, oldEnd)`; the first call has the whole text as new). On entry the edited bytes are `Text` and the rest keep the previous call's kinds, moved with the edit, so an incremental parser (tree-sitter's `ts_tree_edit` takes these offsets) recolours only what changed; a lexer that redoes everything sets every byte. Without one the text is plain. |
| Completion | `SetCompletionProvider(fn)` | `fn(content, cursor, forced)` returns the items and the byte range the picked one replaces. Asked after a typed word character or `.`, and on Ctrl+Space or Ctrl+. (`forced`). An item has `text`, `insertText`, `detailText` (dimmed, on the right), and an `icon` (a letter or an icon font glyph) with a token colour. |
| Submit | `SetSubmitCallback(fn)` | Ctrl+Enter (Cmd on macOS) |
| Line comments | `SetLineComment("//")` | Ctrl+/ toggles the prefix on the selected lines |
| Indent words | `SetIndentWords({"begin", "then"})` | Enter after one of them indents the next line, as it does after an opening bracket |
| Runnable ranges | `SetRunnableRanges(ranges, onRun)` | `ranges(content)` returns byte ranges; each gets a play icon in the gutter, and a click calls `onRun(begin, end)` |
| Error marker | `SetErrorMarker(offset, message)` | a wavy underline under the token at a byte offset, the message on hover; `ClearErrorMarker()` |

The rest: `SetText` / `GetText`, `HasSelection`, `GetSelectedText`, `GetCursorIndex` (a byte offset), `SetPlaceholder`, `SetShowLineNumbers`, `SetTabSize`, `SetReadOnly`, `SetFocus`, `IsContentDirty` / `ClearContentDirty`.

Colours come from `Theme::Current()` each frame (`TextEditor::FromTheme(colors)` is that mapping). `SetPalette(palette)` replaces it, for a block on a different background for example; `TokenColor(token)` is the colour a token draws with.

## Behaviour worth knowing

- The editor is a child window, not an ImGui text field: it reads keys and characters itself while it has the focus, and stands back while an ImGui field (its own find bar included) is active.
- Brackets and quotes close themselves, typing the closing one steps over it, and Backspace between an empty pair removes both. Ctrl+Backspace (Opt on macOS) deletes a word, Ctrl+Shift+Backspace (Cmd) to the line start.
- Ctrl+F or Ctrl+H opens the find bar, starting from the selection; F3 and Shift+F3 step through the matches, Escape closes it.
- Undo keeps the last 100 steps as whole copies of the text. `SetText` clears them and the dirty flag.
- Offsets are bytes of UTF-8. Lines do not wrap; the text scrolls sideways. A tab is drawn `SetTabSize` spaces wide.
- The highlighter gets the whole text on every edit. For a large text and a slow parser, parse incrementally inside the hook (keep the last text and tree in its closure). There is no tree-sitter adapter in imshell yet.
- While it is focused and editable the editor is a text field to the platform, as ImGui's own fields are: it sets `PlatformImeData.WantVisible` and `WantTextInput` with the caret's position, so iOS and Android show the soft keyboard and underline a composition at the caret, Windows places the IME window there, and `io.WantTextInput` tells the host's global shortcuts that someone is typing. On macOS GLFW places the IME's candidate window by itself. Touch reaches it as the mouse input the shells' touch layer produces; the selection handles are for ImGui's own text fields.
- `tests/editor_test.cpp` drives an editor on a bare ImGui context with ImGui's input events: editing, selection, undo, a fake highlighter and a fake completion provider (`ctest`).
