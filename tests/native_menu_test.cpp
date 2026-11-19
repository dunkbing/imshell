// the native context menu: where the os has one, a built menu hands back the chosen item's id
// (picked as a click would, no window); everywhere the call is quiet until a right click
#include "../src/native_menu.hpp"
#include "imshell/native_menu.hpp"

#include "imgui.h"

#include <cstdio>

// the shell provides it; the test has no window
namespace imshell::detail {
    void* nativeHost() {
        return nullptr;
    }
} // namespace imshell::detail

int main() {
    using imshell::NativeMenuItem;
    int failed = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL: %s\n", what);
            ++failed;
        }
    };

    if (imshell::NativeMenusAvailable()) {
        const std::vector<NativeMenuItem> items = {
            {.label = "Copy", .id = 1, .icon = "doc.on.doc"},
            {.label = "Paste", .id = 2, .enabled = false},
            NativeMenuItem::Separator(),
            {.label = "More", .id = 9, .children = {{.label = "Rename", .id = 3}, {.label = "Delete", .id = 4, .checked = true}}},
        };
        check(imshell::detail::nativeMenuPick(items, {0}) == 1, "the first item gives its id");
        check(imshell::detail::nativeMenuPick(items, {1}) == -1, "a disabled item gives nothing");
        check(imshell::detail::nativeMenuPick(items, {3, 0}) == 3, "a submenu item gives its id");
        check(imshell::detail::nativeMenuPick(items, {3, 1}) == 4, "the second submenu item gives its id");
        check(imshell::detail::nativeMenuPick(items, {2}) == -1, "a separator gives nothing");
    }

    // no right click, so nothing is chosen and nothing opens, on either path
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(100, 100);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontDefault();
    ImGui::NewFrame();
    ImGui::Begin("w");
    ImGui::Button("row");
    int built = 0;
    const int chosen = imshell::NativeContextMenu("##m", [&] {
        ++built;
        return std::vector<NativeMenuItem>{{.label = "A", .id = 1}};
    });
    check(chosen == -1, "no click, no choice");
    check(built == 0, "the items are built only when the menu opens");
    ImGui::End();
    ImGui::Render();
    ImGui::DestroyContext();

    std::printf(failed ? "native menu: %d failed\n" : "native menu: ok\n", failed);
    return failed ? 1 : 0;
}
