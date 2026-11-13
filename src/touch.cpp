#include "touch.hpp"

#include "imgui_internal.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace imshell::touch {
    namespace {
        State gState = State::None;
        ImVec2 gStart, gLast;
        double gStartTime = 0, gLastMoveTime = 0;
        int gDownFrame = 0;  // imgui's frame when the finger landed
        bool gMoved = false; // past the slop, though maybe not decided yet
        ImVec2 gVelocity, gFling;
        bool gLift = false;

        // selection handles, as drawn last frame: where each end's character is (its line's
        // middle), the knob below it, and the knob's radius; none when hidden
        int gHandleCount = 0;
        ImVec2 gHandleChar[2], gHandleKnob[2];
        float gHandleRadius = 0;
        bool gGrab = false; // dragging one: shift is held and the finger is offset to the text
        ImVec2 gGrabOffset;

        // a handle under the finger: the selection turns so the other end anchors, then a
        // shift-press at this end's character lets imgui's own drag move it
        bool grabHandle(ImVec2 p) {
            ImGuiContext& g = *GImGui;
            ImGuiInputTextState& s = g.InputTextState;
            for (int k = 0; k < gHandleCount; ++k) {
                const ImVec2 d(p.x - gHandleKnob[k].x, p.y - gHandleKnob[k].y);
                if (std::hypot(d.x, d.y) > gHandleRadius * 2 || s.ID != g.ActiveId)
                    continue;
                const int a = s.GetSelectionStart(), b = s.GetSelectionEnd();
                const int lo = std::min(a, b), hi = std::max(a, b);
                s.SetSelection(k == 0 ? hi : lo, k == 0 ? lo : hi);
                gGrab = true;
                gGrabOffset = ImVec2(gHandleChar[k].x - p.x, gHandleChar[k].y - p.y);
                ImGuiIO& io = ImGui::GetIO();
                io.AddKeyEvent(ImGuiMod_Shift, true);
                gState = State::Pressed;
                hover(gHandleChar[k]);
                io.AddMouseButtonEvent(0, true);
                return true;
            }
            return false;
        }

        void letGo() {
            if (gGrab)
                ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift, false);
            gGrab = false;
            gGrabOffset = ImVec2(0, 0);
        }

        // where character i is drawn, from the text alone: lines and widths before it
        ImVec2 layout(const char* text, int i, float size) {
            int lines = 0, lineStart = 0;
            for (int j = 0; j < i; ++j)
                if (text[j] == '\n')
                    ++lines, lineStart = j + 1;
            const float x =
                ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0, text + lineStart, text + i).x;
            return ImVec2(x, lines * size);
        }

        float length(ImVec2 v) {
            return std::hypot(v.x, v.y);
        }
    } // namespace

    State state() {
        return gState;
    }
    ImVec2 start() {
        return gStart;
    }

    void hover(ImVec2 p) {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
        io.AddMousePosEvent(p.x, p.y);
        gLast = p;
    }

    void down(ImVec2 p, double now) {
        gFling = gVelocity = ImVec2(0, 0);
        gLift = false;
        letGo();
        if (grabHandle(p))
            return;
        gStart = p;
        gStartTime = gLastMoveTime = now;
        gDownFrame = ImGui::GetFrameCount();
        gMoved = false;
        gState = State::Pending;
        hover(p);
    }

    bool scrolls() {
        const ImGuiMouseCursor cursor = ImGui::GetMouseCursor();
        return cursor == ImGuiMouseCursor_Arrow || cursor == ImGuiMouseCursor_Hand;
    }

    void press(ImVec2 from, ImVec2 to) {
        gState = State::Pressed;
        hover(from);
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        hover(to);
    }

    void follow(ImVec2 p) {
        if (gState == State::Pressed)
            hover(ImVec2(p.x + gGrabOffset.x, p.y + gGrabOffset.y));
    }

    void scrollBy(ImVec2 d) {
        const float font = std::max(ImGui::GetFontSize(), 1.0f);
        ImGui::GetIO().AddMouseWheelEvent(d.x / (2.0f * font), d.y / (5.0f * font));
    }

    void move(ImVec2 p, double now) {
        // the cursor says what lies under the finger only once a frame has hovered there: a
        // quick swipe passes the slop before that, and would read the last gesture's cursor
        if (gState == State::Pending && length(ImVec2(p.x - gStart.x, p.y - gStart.y)) > kSlop)
            gMoved = true;
        if (gState == State::Pending && gMoved && ImGui::GetFrameCount() > gDownFrame) {
            // a splitter or column edge drags at once. a quick swipe scrolls, over text too; a
            // finger held still on text first selects. anything else scrolls: a rest before a
            // swipe is common in lists, and a row would take it
            const bool text = ImGui::GetMouseCursor() == ImGuiMouseCursor_TextInput;
            const bool edge = !scrolls() && !text;
            if (edge || (text && now - gStartTime >= kHold)) {
                press(gStart, gStart);
            } else {
                gState = State::Scrolling;
                gLast = gStart;
            }
        }
        if (gState == State::Scrolling) {
            const ImVec2 d(p.x - gLast.x, p.y - gLast.y);
            hover(p); // scroll the window under the finger
            scrollBy(d);
            const float dt = static_cast<float>(std::max(now - gLastMoveTime, 1e-3));
            gVelocity = ImVec2(d.x / dt * 0.5f + gVelocity.x * 0.5f,
                               d.y / dt * 0.5f + gVelocity.y * 0.5f);
        } else if (gState == State::Pressed) {
            hover(ImVec2(p.x + gGrabOffset.x, p.y + gGrabOffset.y));
        }
        gLastMoveTime = now;
    }

    void up(double now, bool lift) {
        ImGuiIO& io = ImGui::GetIO();
        if (gState == State::Pending && !gMoved) { // a tap; a swipe lifted before any frame is none
            hover(gStart);
            io.AddMouseButtonEvent(0, true);
            io.AddMouseButtonEvent(0, false);
        } else if (gState == State::Pressed) {
            io.AddMouseButtonEvent(0, false);
            letGo();
        } else if (gState == State::Scrolling && now - gLastMoveTime < 0.1) {
            gFling = gVelocity;
        }
        gState = State::None;
        gLift = lift;
    }

    void releaseAway() {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        io.AddMouseButtonEvent(0, false);
    }

    void cancel() {
        if (gState == State::Pressed)
            releaseAway();
        letGo();
        gState = State::None;
    }

    void fling(ImVec2 velocity) {
        gFling = velocity;
    }

    bool longPressDue(double now) {
        if (gState != State::Pending || gMoved || now - gStartTime <= kLongPress)
            return false;
        gState = State::None;
        hover(gStart);
        return true;
    }

    void rightClick() {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMouseButtonEvent(1, true);
        io.AddMouseButtonEvent(1, false);
    }

    // a flick coasts, slowing like a UIScrollView
    void coast(float dt) {
        if (length(gFling) <= kFlingStop)
            return;
        hover(gLast);
        scrollBy(ImVec2(gFling.x * dt, gFling.y * dt));
        const float decay = std::pow(0.998f, dt * 1000.0f);
        gFling = ImVec2(gFling.x * decay, gFling.y * decay);
    }

    bool takeLift() {
        if (!gLift || gState != State::None || length(gFling) > kFlingStop)
            return false;
        gLift = false;
        ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        return true;
    }

    bool animating() {
        return gState == State::Pending || length(gFling) > kFlingStop || gLift;
    }
} // namespace imshell::touch

namespace imshell::touch {
    bool grabbing() {
        return gGrab;
    }

    void drawHandles() {
        gHandleCount = 0;
        ImGuiContext& g = *GImGui;
        const ImGuiInputTextState& s = g.InputTextState;
        const ImGuiPlatformImeData& ime = g.PlatformImeData;
        if (gGrab && gState != State::Pressed)
            letGo(); // never leave shift held
        // shortcut: no handles in password, read-only or word-wrapped fields (the layout below
        // knows only line breaks); a host's own editor draws its own
        if (!ime.WantTextInput || g.ActiveId == 0 || s.ID != g.ActiveId || !s.HasSelection() ||
            (s.Flags & (ImGuiInputTextFlags_Password | ImGuiInputTextFlags_ReadOnly)) ||
            s.WrapWidth > 0)
            return;
        const float size = ime.InputLineHeight;
        const char* text = s.TextA.Data;
        const int cursor = s.GetCursorPos();
        const int ends[2] = {std::min(s.GetSelectionStart(), s.GetSelectionEnd()),
                             std::max(s.GetSelectionStart(), s.GetSelectionEnd())};
        // the caret is drawn at the cursor: every other position is measured from it
        const ImVec2 caret(ime.InputPos.x + 1, ime.InputPos.y), at = layout(text, cursor, size);
        gHandleRadius = std::max(size * 0.45f, 8.0f);
        ImVec4 c = ImGui::GetStyleColorVec4(ImGuiCol_TextSelectedBg);
        c.w = 1;
        ImDrawList* fg = ImGui::GetForegroundDrawList(ImGui::FindViewportByID(ime.ViewportId));
        for (int k = 0; k < 2; ++k) {
            const ImVec2 l = layout(text, ends[k], size);
            const ImVec2 top(caret.x + l.x - at.x, caret.y + l.y - at.y);
            gHandleChar[k] = ImVec2(top.x, top.y + size * 0.5f);
            gHandleKnob[k] = ImVec2(top.x, top.y + size + gHandleRadius);
            fg->AddLine(top, ImVec2(top.x, top.y + size), ImGui::GetColorU32(c), 2);
            fg->AddCircleFilled(gHandleKnob[k], gHandleRadius, ImGui::GetColorU32(c));
        }
        gHandleCount = 2;
    }
} // namespace imshell::touch
