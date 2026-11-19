# Accessibility (design)

This is the design for screen reader support ([#21](https://github.com/dunkbing/imshell/issues/21)).
Nothing here is built yet.
ImGui draws everything into one surface, so to a screen reader an imshell window is a single unlabelled image.
The goal is that VoiceOver, Narrator/NVDA, Orca, TalkBack and Switch Control see the same buttons, fields and labels a sighted user does, and can press them.

## The model

Each frame imshell collects a flat list of accessible items, then hands the finished list to the platform after `ImGui::Render()`, the same point `placeNativeViews()` runs.

```cpp
namespace imshell::a11y {
    enum class Role { Button, CheckBox, Switch, RadioButton, Tab, TextField, Slider, Select, MenuItem, Link, Heading, Text, Group, Image };
    struct Item {
        ImGuiID id;          // stable across frames: the platform keeps focus on it
        ImGuiID parent;      // a group, a window, or 0
        Role role;
        std::string label;   // what is read
        std::string value;   // a field's text, a slider's number; empty for a password field
        ImRect rect;         // screen coordinates
        bool enabled, focused, checked, selected, expanded;
    };
}
```

- **Flat list, ids, parents:** the platform builds its own tree from `parent`.
  A window is a group, a card or a tab bar a nested group.
- **Stable ids:** the ImGui item id is the identity, so VoiceOver's cursor survives a re-layout.
- **Only while someone listens:** collection costs a little per item, so it runs only once a client has asked.
  The platform tells the shell: `UiaClientsAreListening()` on Windows, `UIAccessibilityIsVoiceOverRunning()` on iOS, `AccessibilityManager.isEnabled` on Android, the first query on macOS, and GTK on Linux builds its accessible tree only when AT-SPI is active.

## Where items come from

ImGui has no accessibility layer ([ocornut/imgui#2000](https://github.com/ocornut/imgui/issues/2000)), so there are two sources, used together.

1. **imshell's own widgets** (`Button`, `ToggleButton`, `SegmentedTabs`, `Select`, `IconButton`, the components) report themselves, with the role and state they already know.
   This is exact and free of guessing.
2. **Plain ImGui widgets** are reported through the hooks the test engine uses (`IMGUI_TEST_ENGINE_ITEM_ADD` and `ITEM_INFO` in `imgui_internal.h`), which carry the id, rect, label and item status flags of every item.
   imshell would provide those two hook functions itself when built with accessibility on, so a host does not need the test engine.
   The cost is `IMGUI_ENABLE_TEST_ENGINE` defined for the host's ImGui, which also compiles the test engine's per-item calls; this has to be measured before it becomes the default.

Labels need care.
An icon-only button has no text, so imshell widgets take an explicit name: `IconButton(icon, variant, size)` gets an optional accessible label, and `imshell::AccessLabel("Delete")` names the next plain item.
`##id` suffixes are stripped; an item with no label and no override is reported as `Group`, not read as "button".
A text field's value is exposed, except password fields (`ImGuiInputTextFlags_Password`), which report the length only.

## What the platform side does

Each platform keeps one object per item, created and destroyed as ids appear and vanish, and answers its API from the last published list.

| Platform | API | Notes |
|---|---|---|
| macOS | `NSAccessibilityElement` children of the content view | `accessibilityFrame` in screen coordinates (flip y), `accessibilityPerformPress` posts a click at the item's centre, focus changes post `NSAccessibilityFocusedUIElementChangedNotification` |
| iOS | `UIAccessibilityElement`s in the host view's `accessibilityElements` | shares the Apple code with macOS where the object model matches; the frame is in screen points; Switch Control and Voice Control use the same elements |
| Windows | UI Automation: `WM_GETOBJECT` returns an `IRawElementProviderFragmentRoot`, one `IRawElementProviderFragment` per item | patterns: Invoke, Toggle, Value, RangeValue, SelectionItem; `UiaRaiseAutomationEvent` for focus and value changes |
| Linux | GTK4's `GtkAccessible` on the `GtkGLArea`: a custom `GtkAccessible` object per item, with `get_first_accessible_child` and `get_next_accessible_sibling` (GTK 4.10 and later) | AT-SPI is GTK's job; the roles map to `GtkAccessibleRole`; needs the host's GTK to be 4.10 or newer, older ones stay silent |
| Android | `AccessibilityNodeProvider` on the activity's view, a virtual view per item | through JNI in `ImShellActivity`; `performAction(ACTION_CLICK)` becomes a tap at the node's centre; `ACTION_SET_TEXT` goes to the focused field |

Actions never call widgets directly.
A "press" becomes the input the user would have given: a click at the item's centre (desktop) or a tap (touch), or `ImGuiKey_Enter` when the item has keyboard focus.
This keeps ImGui the only place widget logic lives.
Focus runs both ways: when the screen reader moves to an item, imshell focuses it with `ImGui::SetFocusID`; when keyboard navigation moves, imshell posts the platform's focus event.

## Native views

Controls that are already real OS views (`NativeSwitch`, `NativeSegmented`, `NativePopUp`, `NativeButton`, web and video views) are accessible by themselves.
imshell must not report them a second time: the item collector skips an id that has a native view, and the platform's own element stays where the view was placed.
A native view that sits under a popup is hidden, so it also leaves the accessibility tree at that moment, matching what is on screen.

## Order of work

1. The collector and `Role`/`Item` API, imshell widgets reporting, a pure test of the list for a fixed frame.
2. macOS and iOS (one shared file in `src/apple`), tested by querying `accessibilityChildren` and pressing an element in a test.
3. Windows UIA, tested with a UI Automation client in the existing Windows host; the SSM session has no desktop, so the tree is read, not clicked.
4. Android `AccessibilityNodeProvider`.
5. Linux `GtkAccessible`.
6. The hook route for plain ImGui widgets, after step 2 shows what the imshell widgets alone cover.

## Open questions

- Is the test engine hook route acceptable for hosts, or should plain widgets be wrapped by a macro instead?
- Live regions: toasts and alerts should be announced when they appear (`NSAccessibilityAnnouncementRequestedNotification`, `UIAccessibilityPostNotification`, UIA notification events, TalkBack `announceForAccessibility`); the toast and alert components need to call one `imshell::a11y::Announce(text)`.
- The text editor (`imshell::TextEditor`) needs a text pattern (line, selection, caret); this is the largest single piece and probably follows the rest.
- Dynamic type and the system's text size setting are a separate topic: they change font scale, not the tree.
