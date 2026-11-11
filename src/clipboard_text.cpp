#include "imshell/clipboard.hpp"

#include "imgui.h"

// the platforms with no clipboard code of their own yet: text only, through imgui
namespace imshell::Clipboard {
    std::string ReadText() {
        const char* text = ImGui::GetClipboardText();
        return text ? text : "";
    }
    void WriteText(const std::string& text) {
        ImGui::SetClipboardText(text.c_str());
    }
    std::vector<Item> Read() {
        const std::string text = ReadText();
        return text.empty() ? std::vector<Item>{} : std::vector<Item>{{{{"text/plain", text}}}};
    }
    void Write(const std::vector<Item>& items) {
        for (const Item& item : items) {
            if (item.has("text/plain")) {
                WriteText(item.get("text/plain"));
                return;
            }
        }
    }
} // namespace imshell::Clipboard
