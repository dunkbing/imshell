#include "activity.hpp"
#include "imgui_internal.h"

namespace mydear::detail {
    bool isImGuiUserActive() {
        ImGuiIO& io = ImGui::GetIO();

        if (ImGui::IsAnyItemActive() || ImGui::IsAnyItemFocused() || ImGui::IsAnyItemHovered()) {
            return true;
        }

        if (ImGuiContext* ctx = ImGui::GetCurrentContext()) {
            if (ctx->MovingWindow != nullptr || ctx->DragDropActive) {
                return true;
            }

            if (ctx->LastActiveId != 0 && ctx->LastActiveIdTimer < 0.05f) {
                return true;
            }
        }

        const ImVec2 mouseDelta = io.MouseDelta;
        if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f || io.MouseWheel != 0.0f ||
            io.MouseWheelH != 0.0f) {
            return true;
        }

        for (bool down : io.MouseDown) {
            if (down) {
                return true;
            }
        }

        for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
            const ImGuiKeyData* keyData = ImGui::GetKeyData(static_cast<ImGuiKey>(key));
            if (keyData != nullptr && (keyData->Down || keyData->DownDuration == 0.0f)) {
                return true;
            }
        }

        if (io.InputQueueCharacters.Size > 0) {
            return true;
        }

        return false;
    }
} // namespace mydear::detail
