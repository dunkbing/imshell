#include "imshell/clipboard.hpp"

#include "imgui.h"

// plain text through imgui's own clipboard hooks, which the shell fills (linux, windows,
// android, the web). Read and Write are the platform's, except on the web: text only there
namespace imshell::Clipboard {
    std::string ReadText() {
        const char* text = ImGui::GetClipboardText();
        return text ? text : "";
    }
    void WriteText(const std::string& text) {
        ImGui::SetClipboardText(text.c_str());
    }
#ifdef __EMSCRIPTEN__
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
#endif
} // namespace imshell::Clipboard
