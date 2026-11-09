#include "mydear/native_view.hpp"
#include "native_view.hpp"

#include <unordered_map>

namespace {
    struct Entry {
        void* clip = nullptr; // the platform container, clipped to the visible part
        void* view = nullptr;
        ImRect rect, visible;
        ImGuiWindow* window = nullptr;
        int frame = -1;
        double lastSeen = 0;
        bool hidden = false;
    };
    std::unordered_map<ImGuiID, Entry> gEntries;
    constexpr double kFreeAfterSeconds = 3;

    // a window drawn above `window` (popup, tooltip, floating window) covers `r`
    bool covered(ImGuiWindow* window, const ImRect& r) {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* root = window->RootWindowDockTree;
        for (int i = ImGui::FindWindowDisplayIndex(root) + 1; i < g.Windows.Size; i++) {
            ImGuiWindow* w = g.Windows[i];
            if (w->WasActive && !w->Hidden && w->RootWindowDockTree == w && w != root &&
                w->Rect().Overlaps(r))
                return true;
        }
        return false;
    }
} // namespace

namespace mydear {
    void* NativeView(const char* id, const ImVec2& size, const std::function<void*()>& create) {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const ImGuiID key = window->GetID(id);
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 itemSize = ImGui::CalcItemSize(size, avail.x, avail.y);
        const ImVec2 pos = window->DC.CursorPos;
        const ImRect bb(pos, ImVec2(pos.x + itemSize.x, pos.y + itemSize.y));
        ImGui::ItemSize(bb);
        ImGui::ItemAdd(bb, key);

        Entry& e = gEntries[key];
        if (!e.view) {
            void* made = create ? create() : nullptr;
            if (!made) {
                gEntries.erase(key);
                return nullptr;
            }
            e.view = made;
            e.clip = detail::nativeAttach(made);
        }
        e.rect = bb;
        e.visible = bb;
        e.visible.ClipWith(window->ClipRect);
        e.window = window;
        e.frame = ImGui::GetFrameCount();
        e.lastSeen = ImGui::GetTime();
        return e.view;
    }

    namespace detail {
        void placeNativeViews() {
            const int frame = ImGui::GetFrameCount();
            const double now = ImGui::GetTime();
            for (auto it = gEntries.begin(); it != gEntries.end();) {
                Entry& e = it->second;
                if (e.frame != frame && now - e.lastSeen > kFreeAfterSeconds) {
                    nativeRemove(e.clip);
                    it = gEntries.erase(it);
                    continue;
                }
                const bool show = e.frame == frame && e.visible.GetWidth() > 0 &&
                                  e.visible.GetHeight() > 0 && !covered(e.window, e.visible);
                if (show)
                    nativePlace(e.clip, e.view, e.visible, e.rect);
                if (e.hidden == show) {
                    e.hidden = !show;
                    nativeSetHidden(e.clip, e.hidden);
                }
                ++it;
            }
        }

        void dropNativeViews() {
            for (auto& [key, e] : gEntries)
                nativeRemove(e.clip);
            gEntries.clear();
        }
    } // namespace detail
} // namespace mydear
