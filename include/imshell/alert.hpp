#pragma once

#include <functional>
#include <string>
#include <vector>

// imgui modals that stack over everything, one at a time. show() queues one from any thread
// (it wakes the loop) and returns; call Alert::render() once a frame, after the rest of the ui
//
//   imshell::Alert::show("Delete?", "It cannot be undone.",
//                       {{"Delete", [] { ... }, imshell::AlertButton::Style::Destructive},
//                        {"Cancel", nullptr, imshell::AlertButton::Style::Cancel}});
namespace imshell {
    struct AlertButton {
        std::string text;
        std::function<void()> onPress;
        // cancel sits left and answers escape; default sits right and answers return
        enum class Style { Default, Cancel, Destructive };
        Style style = Style::Default;
    };

    class Alert {
    public:
        // no buttons: one OK
        static void show(const std::string& title, const std::string& message = "",
                         std::vector<AlertButton> buttons = {});
        // the queued alert or input prompt, drawn over the popups open this frame
        static void render();
    };

    // a one-line text prompt in the same queue
    class InputDialog {
    public:
        // return an error message to keep the prompt open with it, empty to close
        using ConfirmCallback = std::function<std::string(const std::string&)>;
        using ValidatorCallback = std::function<std::string(const std::string&)>;
        using CancelCallback = std::function<void()>;

        static void show(const std::string& title, const std::string& label,
                         const std::string& initialValue, const std::string& confirmButtonText,
                         ConfirmCallback onConfirm, CancelCallback onCancel = nullptr,
                         ValidatorCallback validator = nullptr);
    };
} // namespace imshell
