#pragma once

#include "imgui.h"

// one finger driving imgui's mouse, shared by the phone and browser shells: a tap clicks, a
// drag scrolls (held on text it selects, a splitter or column edge drags at once), a flick
// coasts, a long press is the shell's. android and the browser feed raw events (down, move,
// up); ios lets its gesture recognizers decide the slop and velocity and calls press() and
// fling() itself. coordinates are imgui units
namespace imshell::touch {
    enum class State { None, Pending, Pressed, Scrolling };

    constexpr float kSlop = 8;          // before a touch becomes a drag
    constexpr double kHold = 0.15;      // seconds still on text before a drag selects
    constexpr double kLongPress = 0.5;  // seconds
    constexpr float kFlingStop = 20;    // units per second

    State state();

    // the finger lands: hover only, so the cursor shape says what lies under it; stops a flick
    void down(ImVec2 p, double now);
    // raw moves (android): past the slop, scroll or press by the cursor under the finger
    void move(ImVec2 p, double now);
    // the finger lifts: a tap clicks, a press releases, a quick scroll coasts. lift: move the
    // cursor away after the next frame, so nothing stays hovered
    void up(double now, bool lift);
    void cancel();

    // a drag starts here: true when it should scroll the content (an arrow or hand cursor)
    bool scrolls();
    // drag from `from` (button down there), the cursor now at `to`
    void press(ImVec2 from, ImVec2 to);
    // the cursor follows a pressed finger
    void follow(ImVec2 p);
    // 2 font sizes sideways, 5 down per wheel step: the content stays under the finger
    void scrollBy(ImVec2 d);
    void fling(ImVec2 velocity);

    // the pending finger has rested kLongPress: it is the shell's now (state None)
    bool longPressDue(double now);
    void rightClick();
    void releaseAway(); // let go without clicking whatever is under the finger

    // each frame before NewFrame: coast. after the frame: the lift, once
    void coast(float dt);
    bool takeLift();
    bool animating();

    void hover(ImVec2 p);
    ImVec2 start();

    // the active text field's selection gets a handle at each end: call between the frame and
    // ImGui::Render(). down() on a handle drags that end (the other end stays)
    void drawHandles();
    bool grabbing(); // a handle is being dragged: not a scroll, not a long press
} // namespace imshell::touch
