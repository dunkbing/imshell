#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

// an idle app sleeps between events; anything animating (a spinner, a stream) asks for
// frames a little longer so the loop keeps drawing. safe from any thread: a request from
// an idle app wakes the loop at once
namespace mydear {
    inline std::atomic<std::int64_t>& framesWantedUntilMs() {
        static std::atomic<std::int64_t> until{0};
        return until;
    }

    inline std::int64_t steadyNowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    namespace detail {
        // set by the shell: wakes its sleeping loop from any thread
        inline std::atomic<void (*)()>& wakeLoopHook() {
            static std::atomic<void (*)()> hook{nullptr};
            return hook;
        }
    } // namespace detail

    inline void requestFrames(std::chrono::milliseconds for_ = std::chrono::milliseconds(250)) {
        const std::int64_t now = steadyNowMs();
        const std::int64_t before =
            framesWantedUntilMs().exchange(now + for_.count(), std::memory_order_relaxed);
        // only the step from idle to wanting frames wakes: a spinner asks every frame
        if (before <= now)
            if (auto wake = detail::wakeLoopHook().load(std::memory_order_relaxed))
                wake();
    }

    // what the frame loop checks before it sleeps
    inline bool framesRequested() {
        return steadyNowMs() < framesWantedUntilMs().load(std::memory_order_relaxed);
    }
} // namespace mydear
