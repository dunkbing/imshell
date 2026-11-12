#pragma once

// work a backend runs on the ui thread once per pass of the loop (the windows notifications'
// queue, the web's answers and clicks); the backend sets it on first use
#include <atomic>

namespace imshell::detail {
    inline std::atomic<void (*)()>& loopPass() {
        static std::atomic<void (*)()> pass{nullptr};
        return pass;
    }

    inline void runLoopPass() {
        if (void (*pass)() = loopPass().load())
            pass();
    }
} // namespace imshell::detail
