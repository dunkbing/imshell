#include "touch.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace mydear::touch {
    namespace {
        State gState = State::None;
        ImVec2 gStart, gLast;
        double gStartTime = 0, gLastMoveTime = 0;
        ImVec2 gVelocity, gFling;
        bool gLift = false;

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
        gStart = p;
        gStartTime = gLastMoveTime = now;
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
            hover(p);
    }

    void scrollBy(ImVec2 d) {
        const float font = std::max(ImGui::GetFontSize(), 1.0f);
        ImGui::GetIO().AddMouseWheelEvent(d.x / (2.0f * font), d.y / (5.0f * font));
    }

    void move(ImVec2 p, double now) {
        if (gState == State::Pending &&
            length(ImVec2(p.x - gStart.x, p.y - gStart.y)) > kSlop) {
            if (scrolls()) {
                gState = State::Scrolling;
                gLast = gStart;
            } else { // a splitter, column edge or text: drag it
                press(gStart, gStart);
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
            hover(p);
        }
        gLastMoveTime = now;
    }

    void up(double now, bool lift) {
        ImGuiIO& io = ImGui::GetIO();
        if (gState == State::Pending) { // a tap
            hover(gStart);
            io.AddMouseButtonEvent(0, true);
            io.AddMouseButtonEvent(0, false);
        } else if (gState == State::Pressed) {
            io.AddMouseButtonEvent(0, false);
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
        gState = State::None;
    }

    void fling(ImVec2 velocity) {
        gFling = velocity;
    }

    bool longPressDue(double now) {
        if (gState != State::Pending || now - gStartTime <= kLongPress)
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
} // namespace mydear::touch
