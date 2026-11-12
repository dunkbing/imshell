#pragma once

// work backends run on the ui thread once per pass of the loop (the windows notifications'
// queue, the web's answers, clicks and text input); each adds its own on first use
#include <array>
#include <atomic>

namespace imshell::detail {
    inline std::array<std::atomic<void (*)()>, 4>& loopPasses() {
        static std::array<std::atomic<void (*)()>, 4> passes{};
        return passes;
    }

    inline void addLoopPass(void (*pass)()) {
        for (auto& slot : loopPasses()) {
            void (*empty)() = nullptr;
            if (slot.load() == pass || slot.compare_exchange_strong(empty, pass))
                return;
        }
    }

    inline void runLoopPass() {
        for (auto& slot : loopPasses())
            if (void (*pass)() = slot.load())
                pass();
    }
} // namespace imshell::detail
