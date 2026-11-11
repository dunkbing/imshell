#pragma once

namespace imshell::detail {
    // the ime's composing text (utf-8) sits right before the caret of the active text field:
    // underline it there, as a native field does. call between the frame and ImGui::Render()
    void drawComposingUnderline(const char* text);
} // namespace imshell::detail
