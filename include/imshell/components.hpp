#pragma once

#include "imgui.h"
#include <span>
#include <string>

// optional components on top of plain imgui (target imshell::components): forms, status and
// feedback pieces drawn with CurrentTheme(). nothing in imshell's core uses them, so an app
// that does not link the target pays nothing
namespace imshell::ui {
    // what a status piece means; picks its colour from the theme
    enum class Tone {
        Neutral,
        Info,
        Success,
        Warning,
        Danger,
    };

    struct TextFieldOptions {
        const char* placeholder = nullptr;
        const char* hint = nullptr;  // a line under the field
        const char* error = nullptr; // replaces the hint, in red, and outlines the field
        bool password = false;
        float width = 0; // 0 = the available width
        ImGuiInputTextFlags flags = 0;
    };
    // a labelled single-line field (the label above, the hint or error below); true when edited
    bool TextField(const char* label, std::string& value, const TextFieldOptions& options = {});

    // a bordered, rounded panel that grows with its content; a title is optional
    void BeginCard(const char* id, const char* title = nullptr);
    void EndCard();

    // a small pill with a word or a count
    void Badge(const char* text, Tone tone = Tone::Neutral);
    // an inline message with an accent stripe; the body wraps. not a modal (see imshell/alert.hpp)
    void Callout(const char* title, const char* body, Tone tone = Tone::Info);

    // fraction 0..1; a negative fraction is indeterminate (a moving stripe)
    void Progress(float fraction, const char* label = nullptr, float width = 0);
    // a placeholder block that shimmers while content loads; width 0 = the available width
    void Skeleton(ImVec2 size);

    // a circle with the name's initials, coloured by the name
    void Avatar(const char* name, float diameter = 32);

    // a card with a label, a large value and an optional change ("+12%" green, "-3%" red)
    void Metric(const char* id, const char* label, const char* value, const char* delta = nullptr);

    // centred title, body and an optional action button; true when the button is pressed
    bool EmptyState(const char* title, const char* body, const char* action = nullptr);

    // toggle chips in a wrapping row; selected[i] flips on click. true when one changed
    bool Chips(const char* id, std::span<const char* const> labels, std::span<bool> selected);

    // short-lived messages in the bottom right corner: Toast from anywhere on the ui thread,
    // RenderToasts once a frame after the rest of the ui
    void Toast(const char* text, Tone tone = Tone::Neutral, float seconds = 3);
    void RenderToasts();
} // namespace imshell::ui
