# Components

`mydear/components.hpp` adds ready-made pieces for forms, status and feedback, in namespace `mydear::ui`. They are optional: they live in their own target, `mydear::components`, and nothing in mydear's core uses them, so an app that doesn't link it pays nothing. They draw with `Theme::Current()`, so they follow the app's theme, and they are plain ImGui items: they take part in layout, keyboard navigation and mydear's touch handling like any other.

```cmake
target_link_libraries(myapp PRIVATE mydear::mydear mydear::components)
```

```cpp
#include "mydear/components.hpp"
namespace ui = mydear::ui;

ui::BeginCard("signup", "Create an account");
ui::TextField("Email", email, {.placeholder = "you@example.com", .error = valid ? nullptr : "Not an email"});
ui::TextField("Password", password, {.hint = "At least 8 characters", .password = true});
if (mydear::Button("Sign up", mydear::ButtonVariant::Primary))
    ui::Toast("Welcome aboard", ui::Tone::Success);
ui::EndCard();
// once a frame, after the rest of the ui
ui::RenderToasts();
```

`examples/gallery` shows all of them (`-DMYDEAR_BUILD_EXAMPLES=ON`, `mydear_gallery`); built for the web, it is the live demo on the site.

## What there is

| Component | Call | Notes |
|---|---|---|
| Text field | `TextField(label, std::string&, {placeholder, hint, error, password, width, flags})` | label above, hint or error below; an error outlines the field in red. True when edited. |
| Card | `BeginCard(id, title)` … `EndCard()` | a bordered, rounded panel that grows with its content |
| Badge | `Badge(text, tone)` | a pill with a word or a count |
| Callout | `Callout(title, body, tone)` | an inline message with an accent stripe; the body wraps. For a modal, use `mydear::Alert`. |
| Progress | `Progress(fraction, label, width)` | 0..1, with the percentage beside the label; a negative fraction is indeterminate |
| Skeleton | `Skeleton(size)` | a pulsing placeholder while content loads; width 0 fills the row |
| Avatar | `Avatar(name, diameter)` | initials in a circle whose colour comes from the name; the name as a tooltip |
| Metric | `Metric(id, label, value, delta)` | a card with a large value; a delta starting with `-` is red, otherwise green |
| Empty state | `EmptyState(title, body, action)` | centred; true when the action button is pressed |
| Chips | `Chips(id, labels, selected)` | toggle chips that wrap; `selected` is a `std::span<bool>` |
| Toast | `Toast(text, tone, seconds)` and `RenderToasts()` | stacked in the bottom right corner, fading out |

`Tone` is `Neutral`, `Info`, `Success`, `Warning` or `Danger`, and maps to the theme's grey, blue, green, yellow and red.

Buttons, selects, the toggle, segmented tabs and the spinner were already in mydear's core and stay there (see [UI toolkit](ui.md)); the components use them rather than redefining them.

## Behaviour worth knowing

- Anything that moves (the indeterminate progress bar, the skeleton, toasts) calls `requestFrames()`, so an idle app keeps drawing only while one is on screen.
- `Metric`, `EmptyState` and `BeginCard` titles size text with `PushFont(nullptr, size)`, so a large value is the same font at a larger size, rasterized at that size.
- `Avatar` takes ASCII initials; a name starting with a multi-byte letter shows its first byte for now.
- `tests/components_test.cpp` draws every component on a bare ImGui context and fails on a duplicated item id or an ImGui assert (`ctest`).
