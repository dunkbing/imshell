#pragma once

#include <imgui_node_editor.h>

// optional node editor (target imshell::node_editor): imgui-node-editor, styled from
// CurrentTheme(). the editor itself is ax::NodeEditor; these only theme it
namespace imshell::NodeEditor {
    // rounding, borders and padding; once, with the editor current
    void ApplyStyle();
    // the colours from CurrentTheme(); each frame, with the editor current (its own palette
    // is a fixed dark one)
    void ApplyColors();
} // namespace imshell::NodeEditor
