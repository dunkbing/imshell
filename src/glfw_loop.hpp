#pragma once

// the desktop frame loop over glfw, shared by the macos and windows shells
#include "activity.hpp"
#include "frame_skip.hpp"
#include "imshell/app.hpp"
#include "loop_pass.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <thread>

namespace imshell::detail {
    constexpr double kIdleAfterSeconds = 2.0; // no input for this long: start idling
    constexpr double kMinWaitSeconds = 1.0 / 120.0;
    // requestFrames() wakes the loop (glfwPostEmptyEvent); an idle app still wakes this often
    // shortcut: a heartbeat for work on other threads that never asks for frames; drop it
    // once hosts always call requestFrames() when they hand the ui something
    constexpr double kMaxWaitSeconds = 1.0;

    // full speed while the user works or something animates; after kIdleAfterSeconds without
    // either, the wait between frames grows to kMaxWaitSeconds. vsync and maxFps are read
    // every frame, so setVSync / setMaxFps apply at once
    template <class Render>
    void runGlfwLoop(GLFWwindow* window, Delegate& delegate, const bool& vsync, const int& maxFps,
                     Render render) {
        wakeLoopHook().store([] { glfwPostEmptyEvent(); });
        // the close button, cmd-q, alt-f4: the delegate may keep the window open
        static Delegate* closing;
        closing = &delegate;
        glfwSetWindowCloseCallback(window, [](GLFWwindow* w) {
            if (!closing->closeRequested())
                glfwSetWindowShouldClose(w, GLFW_FALSE);
        });
        double lastInput = glfwGetTime();
        bool wasFocused = glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0;
        while (!glfwWindowShouldClose(window)) {
            if (delegate.shouldQuit())
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            const double start = glfwGetTime();
            const double sinceInput = start - lastInput;
            const bool focused = glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0;
            const bool busy = delegate.wantsFrames();

            double wait = 0.0;
            if (!focused || glfwGetWindowAttrib(window, GLFW_ICONIFIED))
                wait = kMaxWaitSeconds; // nothing is drawn: no spinning, requestFrames() wakes it
            else if (focused && !busy && sinceInput >= kIdleAfterSeconds)
                wait = std::clamp(sinceInput - kIdleAfterSeconds, kMinWaitSeconds, kMaxWaitSeconds);
            if (wait > 0.0)
                glfwWaitEventsTimeout(wait);
            else
                glfwPollEvents();

            const bool nowFocused = glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0;
            if (nowFocused && !wasFocused)
                lastInput = glfwGetTime();
            wasFocused = nowFocused;
            delegate.tick();
            runLoopPass();
            // never render unfocused: on macos CAMetalLayer nextDrawable blocks while the
            // window is in the background, which makes switching apps sluggish. nor minimized:
            // a minimized window can stay active on windows, and imgui would lay out for a
            // 0x0 display (dialogs capped to the screen shrink and stay small)
            if (!nowFocused || glfwGetWindowAttrib(window, GLFW_ICONIFIED))
                continue;

            render();
            // nothing changed, so nothing was presented and vsync did not pace this frame
            if (frameSkipped()) {
                frameSkipped() = false;
                glfwWaitEventsTimeout(kMinWaitSeconds);
            }

            // without vsync nothing paces the loop: sleep out the rest of the frame
            if (!vsync && maxFps > 0) {
                const double left = start + 1.0 / maxFps - glfwGetTime();
                if (left > 0)
                    std::this_thread::sleep_for(std::chrono::duration<double>(left));
            }
            if (isImGuiUserActive() || delegate.wantsFrames())
                lastInput = glfwGetTime();
        }
        wakeLoopHook().store(nullptr);
    }
} // namespace imshell::detail
