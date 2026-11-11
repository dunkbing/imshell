#pragma once

// a short vibration for feedback: the taptic engine on ios, the vibrator on android, a force
// touch trackpad on macos (felt only while the finger is down). nothing on windows and linux,
// or where the user turned haptics off. ui thread
namespace imshell {
    enum class Haptic {
        Tap,       // a button or toggle
        Selection, // a value ticks over (a picker, a slider step)
        LongPress, // a long press took hold
        Success,
        Warning,
        Error,
    };

    void PlayHaptic(Haptic kind);
} // namespace imshell
