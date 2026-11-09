#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

// an idle app sleeps between events; anything animating (a spinner, a stream) asks for
// frames a little longer so the loop keeps drawing
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

    inline void requestFrames(std::chrono::milliseconds for_ = std::chrono::milliseconds(250)) {
        framesWantedUntilMs().store(steadyNowMs() + for_.count(), std::memory_order_relaxed);
    }

    // what the frame loop checks before it sleeps
    inline bool framesRequested() {
        return steadyNowMs() < framesWantedUntilMs().load(std::memory_order_relaxed);
    }
} // namespace mydear
