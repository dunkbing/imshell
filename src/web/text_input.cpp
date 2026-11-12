// text input on the web: a hidden textarea under the caret takes the typing whenever an imgui
// text field is active. the browser's own machinery runs on it (input methods and their
// candidate window at the caret, dead keys, the soft keyboard on phones, its suggestions) and
// whatever it changes in the textarea is replayed into the field as backspaces and characters.
// keys that move or submit (arrows, enter, tab, shortcuts) still go to glfw and imgui
#include "../ime.hpp"
#include "../loop_pass.hpp"
#include "imshell/text_input.hpp"
#include "imgui_internal.h"

#include <emscripten.h>
#include <GLFW/emscripten_glfw3.h>
#include <GLFW/glfw3.h>

#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

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
        // the rest moves, submits or is a shortcut: imgui's. glfw ignores keys while another
        // element has the focus, so they go to imgui from here. the textarea stays put (copy,
        // cut and paste still fire their events, which glfw reads the clipboard from), and its
        // text is no longer what is before the caret
        e.stopPropagation();
        if (!["c", "x", "v"].includes(e.key.toLowerCase()) || !(e.ctrlKey || e.metaKey))
            e.preventDefault();
        q.key(e, 1);
        q.stale = true;
    });
    t.addEventListener("keyup", e => {
        e.stopPropagation();
        q.key(e, 0);
    });
    q.key = (e, down) => {
        stringToUTF8(e.code || "", q.code, 32);
        Module._imshell_text_key(down, e.ctrlKey, e.shiftKey, e.altKey, e.metaKey);
    };
    q.code = Module._imshell_text_code();
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
EM_JS(void, imshell_text_follow, (int want, float x, float y, float h, const char* context, int known, int reset, int kind), {
    const q = Module.imshellText;
    const t = q.t;
    // the field's kind (imshell::TextKind): what the phone's keyboard offers and corrects
    // an open keyboard changes kind on the tap that moved the field (a refocus outside a tap
    // would close it on phones)
    if (want && q.kind !== kind) {
        q.kind = kind;
        q.kindChanged = true;
        const prose = kind === 1, code = kind === 2;
        t.setAttribute("spellcheck", prose ? "true" : "false");
        t.setAttribute("autocorrect", prose ? "on" : "off");
        t.setAttribute("autocapitalize", prose ? "sentences" : "off");
        t.setAttribute("inputmode", code ? "numeric" : "text");
        t.setAttribute("autocomplete", code ? "one-time-code" : "off");
    }
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
        q.inGesture = false; // a focus from a frame, not a tap: phones show no keyboard for it
    }
    // the text before the caret, so backspace and the browser's suggestions have something to
    // work on; never under an open composition. a field imshell cannot read (an editor) gets
    // spaces instead, refilled as they run out: deleting one is a backspace in the field
    const low = known ? t.value.length > 400 : t.value.length < 8 || t.value.length > 400;
    if ((q.stale || reset || low) && !q.composing) {
        const c = known ? UTF8ToString(context) : " ".repeat(16);
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

// phones open their keyboard only for a focus inside the tap's own event: on a click, run the
// frames that let imgui see the tap, and focus the textarea then if a field took it
EM_JS(void, imshell_text_listen_taps, (), {
    // a press can move the caret: an open composition ends where it is, and the textarea
    // reloads what is before the caret
    Module.canvas.addEventListener("pointerdown", () => {
        const q = Module.imshellText;
        if (!q.focused)
            return;
        if (q.composing) {
            q.composing = "";
            q.t.blur();
            q.t.focus({preventScroll: true});
        }
        q.stale = true;
    });
    // touchend for phones (glfw takes touches over, so no click follows them), click for mice
    // the field may have taken the focus already, between touch down and up: outside the
    // gesture, so without a keyboard. focusing again inside it is what opens one
    const tap = () => {
        const t = Module.imshellText.t;
        if (!Module._imshell_text_tap())
            return;
        const q = Module.imshellText;
        const changed = q.kindChanged;
        q.kindChanged = false;
        if (document.activeElement === t && q.inGesture && !changed)
            return; // the keyboard is up already: a blur would make it bounce
        if (document.activeElement === t)
            t.blur();
        t.focus({preventScroll: true});
        q.inGesture = true;
    };
    Module.canvas.addEventListener("touchend", tap);
    Module.canvas.addEventListener("click", tap);
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

namespace {
    char gCode[32]; // the key's KeyboardEvent.code, written by the textarea's listener

    ImGuiKey keyFromCode(std::string_view code) {
        if (code.size() == 4 && code.starts_with("Key") && code[3] >= 'A' && code[3] <= 'Z')
            return static_cast<ImGuiKey>(ImGuiKey_A + (code[3] - 'A'));
        if (code.size() == 6 && code.starts_with("Digit") && code[5] >= '0' && code[5] <= '9')
            return static_cast<ImGuiKey>(ImGuiKey_0 + (code[5] - '0'));
        if (code.size() >= 2 && code.size() <= 3 && code[0] == 'F') {
            const int n = std::atoi(std::string(code.substr(1)).c_str());
            if (n >= 1 && n <= 12)
                return static_cast<ImGuiKey>(ImGuiKey_F1 + n - 1);
        }
        static constexpr std::pair<std::string_view, ImGuiKey> named[] = {
            {"ArrowLeft", ImGuiKey_LeftArrow}, {"ArrowRight", ImGuiKey_RightArrow}, {"ArrowUp", ImGuiKey_UpArrow},
            {"ArrowDown", ImGuiKey_DownArrow}, {"Home", ImGuiKey_Home},           {"End", ImGuiKey_End},
            {"PageUp", ImGuiKey_PageUp},       {"PageDown", ImGuiKey_PageDown},   {"Enter", ImGuiKey_Enter},
            {"NumpadEnter", ImGuiKey_KeypadEnter}, {"Tab", ImGuiKey_Tab},         {"Escape", ImGuiKey_Escape},
            {"Backspace", ImGuiKey_Backspace}, {"Delete", ImGuiKey_Delete},       {"Insert", ImGuiKey_Insert},
            {"Space", ImGuiKey_Space},
        };
        for (const auto& [name, key] : named)
            if (code == name)
                return key;
        return ImGuiKey_None;
    }
} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE char* imshell_text_code() {
    return gCode;
}

// a key the textarea does not type: to imgui, with the modifiers as they are now
extern "C" EMSCRIPTEN_KEEPALIVE void imshell_text_key(int down, int ctrl, int shift, int alt, int meta) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
    io.AddKeyEvent(ImGuiMod_Shift, shift);
    io.AddKeyEvent(ImGuiMod_Alt, alt);
    io.AddKeyEvent(ImGuiMod_Super, meta);
    if (const ImGuiKey key = keyFromCode(gCode); key != ImGuiKey_None)
        io.AddKeyEvent(key, down != 0);
}

extern "C" EMSCRIPTEN_KEEPALIVE int imshell_text_tap() {
    // the click's mouse events are queued: a few frames let the field take them
    for (int i = 0; i < 3 && gRunFrame; ++i)
        gRunFrame();
    return GImGui->PlatformImeData.WantTextInput;
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
        const bool known = data.WantTextInput && ime::field(text, caret, start, end);
        if (known) {
            const size_t from = caret > 200 ? ime::utf16ToByte(text, ime::utf16Length(text.substr(0, caret)) - 100) : 0;
            before = text.substr(from, caret - from);
        }
        // glfw focuses the canvas on every press, which would take the keyboard off the textarea
        if (GLFWwindow* window = glfwGetCurrentContext())
            emscripten::glfw3::SetFocusOnMouseDown(window, !data.WantTextInput);
        imshell_text_follow(data.WantTextInput, data.InputPos.x, data.InputPos.y, data.InputLineHeight,
                            before.c_str(), known, reset, static_cast<int>(imshell::ActiveTextKind()));
        drawComposingUnderline(gComposing.c_str());
    }
} // namespace imshell::detail
