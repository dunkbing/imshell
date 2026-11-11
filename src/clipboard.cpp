#include "imshell/clipboard.hpp"

#include "imgui.h"

// text goes through imgui, which every shell already wires to the system clipboard
namespace imshell::Clipboard {
    std::string ReadText() {
        const char* text = ImGui::GetClipboardText();
        return text ? text : "";
    }
    void WriteText(const std::string& text) {
        ImGui::SetClipboardText(text.c_str());
    }
} // namespace imshell::Clipboard
