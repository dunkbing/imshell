#pragma once

// the desktop frame loop over glfw, shared by the macos and windows shells
#include "activity.hpp"
#include "mydear/app.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <thread>

namespace mydear::detail {
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
<<<<<<< HEAD
        // the close button, cmd-q, alt-f4: the delegate may keep the window open
        static Delegate* closing;
        closing = &delegate;
        glfwSetWindowCloseCallback(window, [](GLFWwindow* w) {
            if (!closing->closeRequested())
                glfwSetWindowShouldClose(w, GLFW_FALSE);
        });
=======
>>>>>>> origin/main
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
            if (!focused && !busy)
                wait = kMaxWaitSeconds;
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
            // never render unfocused: on macos CAMetalLayer nextDrawable blocks while the
            // window is in the background, which makes switching apps sluggish
            if (!nowFocused)
                continue;

            render();

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
} // namespace mydear::detail
