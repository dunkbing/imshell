# Node editor

`imshell/node_editor.hpp` brings [imgui-node-editor](https://github.com/thedmd/imgui-node-editor): nodes with pins, links between them, a pannable and zoomable canvas, selection and dragging. The editor is imgui-node-editor's own API, `ax::NodeEditor`; imshell builds it and themes it. It is optional: link the `imshell::node_editor` target, which exists when the `external/imgui-node-editor` submodule (or a folder in `IMSHELL_NODE_EDITOR_DIR`) is there.

```cmake
add_subdirectory(external/imshell)
target_link_libraries(myapp PRIVATE imshell::imshell imshell::node_editor)
```

imgui-node-editor draws with a fixed dark palette. Two calls make it follow the theme:

```cpp
#include "imshell/node_editor.hpp"
namespace ed = ax::NodeEditor;

ed::EditorContext* editor = ed::CreateEditor(&config);
ed::SetCurrentEditor(editor);
imshell::NodeEditor::ApplyStyle(); // rounding, borders and padding: once
ed::SetCurrentEditor(nullptr);

// each frame
ed::SetCurrentEditor(editor);
imshell::NodeEditor::ApplyColors(); // from theme(), so a theme change shows at once
ed::Begin("graph");
// ed::BeginNode / EndNode, BeginPin / EndPin, Link ...
ed::End();
ed::SetCurrentEditor(nullptr);
```

The submodule is the fork at [dunkbing/imgui-node-editor](https://github.com/dunkbing/imgui-node-editor), the one DearSQL runs. `tests/node_editor_test.cpp` draws a small graph on a bare context and checks the style and colours.
