#include "imshell/node_editor.hpp"
#include "imshell/themes.hpp"

namespace imshell::NodeEditor {
    namespace ed = ax::NodeEditor;

    void ApplyStyle() {
        auto& style = ed::GetStyle();
        style.NodeRounding = Theme::CornerRadius::MEDIUM;
        style.NodeBorderWidth = 1.0f;
        style.HoveredNodeBorderWidth = 2.0f;
        style.SelectedNodeBorderWidth = 2.0f;
        style.NodePadding = ImVec4(0, 0, 0, Theme::Spacing::M);
    }

    void ApplyColors() {
        const auto& c = Theme::Current()->colors;
        auto& s = ed::GetStyle();
        const auto a = [](ImVec4 v, float alpha) { return ImVec4(v.x, v.y, v.z, alpha); };
        s.Colors[ed::StyleColor_Bg] = c.mantle;
        s.Colors[ed::StyleColor_Grid] = a(c.overlay0, 0.35f);
        s.Colors[ed::StyleColor_NodeBg] = c.surface0;
        s.Colors[ed::StyleColor_NodeBorder] = c.overlay0;
        s.Colors[ed::StyleColor_HovNodeBorder] = c.overlay2;
        s.Colors[ed::StyleColor_SelNodeBorder] = c.blue;
        s.Colors[ed::StyleColor_NodeSelRect] = a(c.blue, 0.15f);
        s.Colors[ed::StyleColor_NodeSelRectBorder] = a(c.blue, 0.6f);
        s.Colors[ed::StyleColor_PinRect] = ImVec4(0, 0, 0, 0);
        s.Colors[ed::StyleColor_PinRectBorder] = ImVec4(0, 0, 0, 0);
    }
} // namespace imshell::NodeEditor
