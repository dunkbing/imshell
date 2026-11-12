// text input on the web: a hidden textarea under the caret takes the typing whenever an imgui
// text field is active. the browser's own machinery runs on it (input methods and their
// candidate window at the caret, dead keys, the soft keyboard on phones, its suggestions) and
// whatever it changes in the textarea is replayed into the field as backspaces and characters.
// keys that move or submit (arrows, enter, tab, shortcuts) still go to glfw and imgui
#include "../ime.hpp"
#include "../loop_pass.hpp"
#include "imgui_internal.h"

#include <emscripten.h>

#include <string>
#include <string_view>

// clang-format off
EM_JS(void, imshell_text_install, (), {
    const t = document.createElement("textarea");
    for (const [k, v] of [["autocomplete", "off"], ["autocorrect", "off"], ["autocapitalize", "off"],
                          ["spellcheck", "false"], ["aria-hidden", "true"], ["tabindex", "-1"]])
        t.setAttribute(k, v);
    // 16px: ios zooms into a focused field with smaller text
    t.style.cssText = "position:fixed;left:0;top:0;width:1px;height:16px;font-size:16px;padding:0;border:0;" +
                      "margin:0;opacity:0;resize:none;overflow:hidden;pointer-events:none;caret-color:transparent;" +
                      "white-space:pre;z-index:-1";
    document.body.appendChild(t);
    const q = Module.imshellText = {t, prev: "", composing: "", ops: [], focused: false, stale: true};
    const hasText = () => t.selectionStart > 0;
    t.addEventListener("keydown", e => {
        // the input method's keys, dead keys and typed characters edit the textarea; backspace
        // too while it has something to delete. glfw must not see them as well
        const text = !e.ctrlKey && !e.metaKey && ([...e.key].length === 1 || e.key === "Dead");
        if (e.isComposing || e.keyCode === 229 || text || (e.key === "Backspace" && hasText() && !e.ctrlKey && !e.metaKey && !e.altKey)) {
            e.stopPropagation();
            return;
        }
        // the rest moves, submits or is a shortcut: imgui's, through glfw. the textarea stays
        // put, and its text is no longer what is before the caret
        if (!["c", "x", "v"].includes(e.key.toLowerCase()) || !(e.ctrlKey || e.metaKey))
            e.preventDefault();
        q.stale = true;
    });
    // paste, drop and the textarea's own undo are imgui's (glfw passes the clipboard on)
    t.addEventListener("beforeinput", e => {
        if (/^(insertFromPaste|insertFromDrop|deleteByCut|historyUndo|historyRedo)$/.test(e.inputType))
            e.preventDefault();
    });
    t.addEventListener("compositionupdate", e => { q.composing = e.data || ""; });
    t.addEventListener("compositionend", () => { q.composing = ""; });
    // what changed since last time, as backspaces then text
    t.addEventListener("input", () => {
        const now = t.value;
        let p = 0;
        while (p < q.prev.length && p < now.length && q.prev[p] === now[p])
            ++p;
        if (p > 0 && p < q.prev.length && q.prev.charCodeAt(p - 1) >= 0xD800 && q.prev.charCodeAt(p - 1) < 0xDC00)
            --p; // never split a surrogate pair
        const removed = [...q.prev.slice(p)].length;
        const added = now.slice(p);
        if (removed || added)
            q.ops.push([removed, added]);
        q.prev = now;
    });
});

// the textarea follows the field: focus, the caret's place, the text before the caret
EM_JS(void, imshell_text_follow, (int want, float x, float y, float h, const char* context, int reset), {
    const q = Module.imshellText;
    const t = q.t;
    if (!want) {
        if (q.focused) {
            q.focused = false;
            t.blur();
        }
        return;
    }
    const r = Module.canvas.getBoundingClientRect();
    t.style.left = (r.left + x) + "px";
    t.style.top = (r.top + y) + "px";
    t.style.height = Math.max(h, 16) + "px";
    if (!q.focused) {
        q.focused = true;
        q.stale = true;
        t.focus({preventScroll: true});
    }
    // the text before the caret, so backspace and the browser's suggestions have something to
    // work on; never under an open composition
    if ((q.stale || reset) && !q.composing) {
        const c = UTF8ToString(context);
        if (t.value !== c)
            t.value = c;
        t.setSelectionRange(c.length, c.length);
        q.prev = c;
        q.stale = false;
    }
});

// the next edit: removed codepoints into *removed, the text into out (size bytes); 0 when none
EM_JS(int, imshell_text_next, (int* removed, char* out, int size), {
    const q = Module.imshellText;
    if (!q || !q.ops.length)
        return 0;
    const [n, text] = q.ops.shift();
    HEAP32[removed >> 2] = n;
    stringToUTF8(text, out, size);
    return 1;
});

EM_JS(int, imshell_text_composing, (char* out, int size), {
    const q = Module.imshellText;
    stringToUTF8(q ? q.composing : "", out, size);
    return q && q.composing ? 1 : 0;
});

EM_JS(int, imshell_text_focused, (), {
    return Module.imshellText && Module.imshellText.focused ? 1 : 0;
});

// phones open their keyboard only for a focus inside the tap's own event: on a click, run the
// frames that let imgui see the tap, and focus the textarea then if a field took it
EM_JS(void, imshell_text_listen_taps, (), {
    Module.canvas.addEventListener("click", () => {
        if (Module._imshell_text_tap())
            Module.imshellText.t.focus({preventScroll: true});
    });
});
// clang-format on

namespace {
    void (*gRunFrame)() = nullptr;
    std::string gComposing; // the browser's open composition, for the underline

    // the textarea's edits, in order, ahead of the frame that shows them
    void pass() {
        int removed = 0;
        static char text[4096];
        while (imshell_text_next(&removed, text, sizeof text)) {
            imshell::detail::ime::backspace(removed);
            for (const char* c = text; *c; ++c)
                if (*c == '\n') { // a phone's return key: enter, which also submits
                    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
                    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
                } else {
                    const char* end = c;
                    while (end[1] && end[1] != '\n')
                        ++end;
                    ImGui::GetIO().AddInputCharactersUTF8(std::string(c, end + 1).c_str());
                    c = end;
                }
        }
        char composing[512];
        imshell_text_composing(composing, sizeof composing);
        gComposing = composing;
    }
} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE int imshell_text_tap() {
    // the click's mouse events are queued: a few frames let the field take them
    for (int i = 0; i < 3 && gRunFrame; ++i)
        gRunFrame();
    return GImGui->PlatformImeData.WantTextInput && !imshell_text_focused();
}

namespace imshell::detail {
    void installTextInput(void (*runFrame)()) {
        gRunFrame = runFrame;
        imshell_text_install();
        imshell_text_listen_taps();
        addLoopPass(pass);
    }

    // after the app's frame: the textarea follows the active field
    void textInputFrame() {
        const ImGuiPlatformImeData& data = GImGui->PlatformImeData;
        static ImGuiID field = 0;
        const bool reset = data.WantTextInput && GImGui->ActiveId != field;
        field = data.WantTextInput ? GImGui->ActiveId : 0;
        std::string_view text;
        int caret = 0, start = 0, end = 0;
        std::string before;
        if (data.WantTextInput && ime::field(text, caret, start, end)) {
            const size_t from = caret > 200 ? ime::utf16ToByte(text, ime::utf16Length(text.substr(0, caret)) - 100) : 0;
            before = text.substr(from, caret - from);
        }
        imshell_text_follow(data.WantTextInput, data.InputPos.x, data.InputPos.y, data.InputLineHeight,
                            before.c_str(), reset);
        drawComposingUnderline(gComposing.c_str());
    }
} // namespace imshell::detail
