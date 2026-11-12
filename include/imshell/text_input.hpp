#pragma once

#include <cstdint>

// what a text field holds, so the system keyboard and input method offer what fits it: spell
// check and autocorrect for prose, the code from a text message for a one-time code, password
// autofill and secure input for a password. call right after the widget (InputText,
// InputTextMultiline, TextEditor::Render), every frame it is drawn; a field without a call is
// code: no autocorrect, no capitals. an InputText with ImGuiInputTextFlags_Password is a
// password without a call. what each platform does with it: docs/platform-shell.md
namespace imshell {
    enum class TextKind : std::uint8_t {
        Code,        // the default: typed as is
        Prose,       // sentences: spell check, autocorrect, capitals, the full keyboard
        OneTimeCode, // digits, filled from a text message where the system can
        Password,    // never read, never learned, autofilled where the system can
    };

    // for the item just submitted (ImGui::GetItemID())
    void SetTextKind(TextKind kind);
    // the active field's kind
    TextKind ActiveTextKind();
} // namespace imshell
