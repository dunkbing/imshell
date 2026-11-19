#include "imshell/native_menu.hpp"

#include "imshell/frames.hpp"
#include "imshell/icons.hpp"
#include "imshell/menu.hpp"
#include "native_menu.hpp"

#include <unordered_map>

namespace imshell {
    namespace {
        struct Pending {
            ImGuiID key;
            std::vector<NativeMenuItem> items;
            ImVec2 pos;
        };
        std::vector<Pending> gPending;           // asked for by the frame, opened after it
        std::unordered_map<ImGuiID, int> gChosen; // the pick, until the next frame reads it

        // the fallback: imshell's own menu
        int drawItems(const std::vector<NativeMenuItem>& items) {
            int chosen = -1;
            for (const NativeMenuItem& item : items) {
                if (item.separator) {
                    ImGui::Separator();
                } else if (!item.children.empty()) {
                    if (BeginSubMenu(item.label.c_str(), item.enabled)) {
                        if (const int inner = drawItems(item.children); inner >= 0)
                            chosen = inner;
                        ImGui::EndMenu();
                    }
                } else {
                    std::string label = item.label;
                    if (!item.icon.empty())
                        if (const char* glyph = Icon(item.icon.c_str()))
                            label = std::string(glyph) + "  " + label;
                    if (ImGui::MenuItem(label.c_str(), nullptr, item.checked, item.enabled))
                        chosen = item.id;
                }
            }
            return chosen;
        }
    } // namespace

    bool NativeMenusAvailable() {
        return detail::nativeMenusAvailable();
    }

    int NativeContextMenu(const char* id, const std::function<std::vector<NativeMenuItem>()>& items) {
        const ImGuiID key = ImGui::GetID(id);
        if (detail::nativeMenusAvailable()) {
            if (const auto done = gChosen.find(key); done != gChosen.end()) {
                const int chosen = done->second;
                gChosen.erase(done);
                return chosen;
            }
            const bool pending = std::any_of(gPending.begin(), gPending.end(), [key](const Pending& p) { return p.key == key; });
            if (!pending && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                gPending.push_back({key, items(), ImGui::GetIO().MousePos});
                requestFrames(); // open it after this frame
            }
            return -1;
        }
        int chosen = -1;
        if (BeginContextMenu(id)) {
            chosen = drawItems(items());
            ImGui::EndPopup();
        }
        return chosen;
    }

    namespace detail {
        void runNativeMenus() {
            if (gPending.empty())
                return;
            std::vector<Pending> now;
            now.swap(gPending);
            for (const Pending& p : now) {
                const ImGuiID key = p.key;
                nativeMenuPopUp(p.items, p.pos, [key](int chosen) {
                    if (chosen >= 0)
                        gChosen[key] = chosen;
                    requestFrames(); // show the pick
                });
            }
        }
    } // namespace detail
} // namespace imshell
