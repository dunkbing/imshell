// the os's own context menu on windows: a popup menu from CreatePopupMenu, tracked by
// TrackPopupMenu in the system's loop (it returns when the menu closes)
#include "../native_menu.hpp"
#include "../native_view.hpp"

#include <string>
#include <windows.h>

namespace imshell::detail {
    namespace {
        std::wstring wide(const std::string& text) {
            if (text.empty())
                return {};
            const int n = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
            std::wstring out(static_cast<size_t>(n), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), n);
            out.resize(static_cast<size_t>(n) - 1);
            return out;
        }

        // a command is its index + 1 in `ids` (0 means "nothing chosen" to TrackPopupMenu); item.id is free for the caller to use
        // shortcut: no icons (windows menus take bitmaps, and there is no Windows icon loader yet), add them with the Segoe Fluent loader
        HMENU build(const std::vector<NativeMenuItem>& items, std::vector<int>& ids) {
            HMENU menu = CreatePopupMenu();
            for (const NativeMenuItem& item : items) {
                if (item.separator) {
                    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                    continue;
                }
                UINT flags = MF_STRING | (item.enabled ? MF_ENABLED : MF_GRAYED) | (item.checked ? MF_CHECKED : MF_UNCHECKED);
                if (!item.children.empty()) {
                    HMENU sub = build(item.children, ids);
                    AppendMenuW(menu, flags | MF_POPUP, reinterpret_cast<UINT_PTR>(sub), wide(item.label).c_str());
                } else {
                    ids.push_back(item.id);
                    AppendMenuW(menu, flags, ids.size(), wide(item.label).c_str());
                }
            }
            return menu;
        }
    } // namespace

    bool nativeMenusAvailable() {
        return true;
    }

    void nativeMenuPopUp(const std::vector<NativeMenuItem>& items, ImVec2 pos, const std::function<void(int)>& done) {
        HWND window = static_cast<HWND>(nativeHost());
        if (!window) {
            done(-1);
            return;
        }
        std::vector<int> ids;
        HMENU menu = build(items, ids);
        POINT at = {static_cast<LONG>(pos.x), static_cast<LONG>(pos.y)}; // imgui's coordinates are client pixels
        ClientToScreen(window, &at);
        SetForegroundWindow(window); // or the menu stays open when the window loses focus
        const UINT cmd = static_cast<UINT>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
                                                          at.x, at.y, 0, window, nullptr));
        PostMessageW(window, WM_NULL, 0, 0);
        DestroyMenu(menu); // and its submenus
        // the menu's loop swallowed the button release: imgui must not think a button is still down
        ImGuiIO& io = ImGui::GetIO();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
        done(cmd > 0 && cmd <= ids.size() ? ids[cmd - 1] : -1);
    }

    // reads a built menu back the way a click would resolve it, no window
    int nativeMenuPick(const std::vector<NativeMenuItem>& items, const std::vector<int>& path) {
        std::vector<int> ids;
        HMENU root = build(items, ids);
        HMENU menu = root;
        int chosen = -1;
        for (size_t i = 0; i < path.size() && menu; ++i) {
            const UINT index = static_cast<UINT>(path[i]);
            if (i + 1 < path.size()) {
                menu = GetSubMenu(menu, index);
            } else {
                const UINT state = GetMenuState(menu, index, MF_BYPOSITION);
                const UINT cmd = GetMenuItemID(menu, index); // -1 for a submenu or separator
                if (state != static_cast<UINT>(-1) && !(state & MF_GRAYED) && cmd > 0 && cmd <= ids.size())
                    chosen = ids[cmd - 1];
            }
        }
        DestroyMenu(root);
        return chosen;
    }
} // namespace imshell::detail
