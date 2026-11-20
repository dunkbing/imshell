// native switch, segmented control and pop-up on windows: a WinUI ToggleSwitch, a row of
// ToggleButtons (WinUI has no segmented control) and a ComboBox, each in a grid that NativeView places at the imgui frame's size, so the control keeps
// its own size and sits at the left, in the middle vertically. when WinUI is not available (the
// Windows App Runtime is not installed) the three fall back to imshell's own widgets, as on a
// platform with no backend
#include "../native_controls.hpp"
#include "../native_view.hpp"
#include "imgui_internal.h"
#include "imshell/frames.hpp"
#include "imshell/native_view.hpp"
#include "imshell/themes.hpp"
#include "imshell/winui.hpp"

#include <windows.h>
#undef GetCurrentTime
#include <unknwn.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include <algorithm>
#include <memory>
#include <unordered_map>

namespace imshell::detail {
    namespace {
        namespace mux = winrt::Microsoft::UI::Xaml;

        // what the control's events leave for the next frame
        struct State {
            bool changed = false; // the user changed it
            bool quiet = false;   // we are setting it ourselves: not a change
            bool on = false;      // the switch
            int selected = -1;    // segmented control and pop-up: the chosen index
        };

        struct Control {
            void* view = nullptr; // the window NativeView made: a new one means a new control
            std::shared_ptr<State> state = std::make_shared<State>();
            mux::FrameworkElement root{nullptr};    // the grid in the island
            mux::Controls::Control control{nullptr}; // the control in it
            ImVec2 size{0, 0};                      // its actual size in pixels, once it exists
            bool dark = false;
            bool widthFixed = false; // a combo box: its width is pinned once measured
        };

        std::unordered_map<ImGuiID, Control>& controls() {
            static std::unordered_map<ImGuiID, Control> map;
            return map;
        }

        // `control` in a grid that lets the control keep its size
        mux::FrameworkElement wrap(const mux::Controls::Control& control, bool dark) {
            control.HorizontalAlignment(mux::HorizontalAlignment::Left);
            control.VerticalAlignment(mux::VerticalAlignment::Center);
            mux::Controls::Grid grid;
            grid.Children().Append(control);
            grid.RequestedTheme(dark ? mux::ElementTheme::Dark : mux::ElementTheme::Light);
            return grid;
        }

        ImVec2 frameOf(ImVec2 size) {
            return ImVec2(size.x, std::max(size.y, ImGui::GetFrameHeight()));
        }

        bool disabled() {
            return (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
        }

        // imgui's coordinates are pixels here and WinUI's sizes are DIPs (96 per inch): at 150% a control
        // that is 32 DIPs high is 48 px, and the island scales what it draws by itself
        float dpiScale() {
            const UINT dpi = GetDpiForWindow(static_cast<HWND>(nativeHost()));
            return dpi ? static_cast<float>(dpi) / 96.0f : 1.0f;
        }

        // drop the entries whose window NativeView destroyed (it frees a view not drawn for a while):
        // they hold the XAML objects, which would stay alive. every few hundred frames is enough
        void sweep() {
            static int counter = 0;
            if (++counter % 300 != 0)
                return;
            for (auto it = controls().begin(); it != controls().end();) {
                if (it->second.view && !IsWindow(static_cast<HWND>(it->second.view)))
                    it = controls().erase(it);
                else
                    ++it;
            }
        }

        // the controls are taller than imgui's frame (WinUI's are 30 to 32 high), so a row of them
        // beside a label would sit below the label's centre: start the item higher by half the
        // difference. the first item of a line does it; the ones after a SameLine already start at
        // that height, so they must not do it again
        void centreOnLine(float height) {
            static int frame = -1;
            static ImGuiID window = 0;
            static float shiftedY = 0;
            ImGuiWindow* current = ImGui::GetCurrentWindow();
            const float y = current->DC.CursorPos.y;
            if (frame == ImGui::GetFrameCount() && window == current->ID && y == shiftedY)
                return;
            const float shift = (height - ImGui::GetFrameHeight()) * 0.5f;
            if (shift <= 0)
                return;
            current->DC.CursorPos.y = y - shift;
            frame = ImGui::GetFrameCount();
            window = current->ID;
            shiftedY = y - shift;
        }

        // the imgui item under the view: what the test engine clicks
        bool itemPressed(ImGuiID key) {
            bool hovered = false, held = false;
            return ImGui::ButtonBehavior(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()), key, &hovered, &held);
        }

        void notify(State& state) {
            if (state.quiet)
                return;
            state.changed = true;
            requestFrames(); // the change came outside imgui: wake the idle loop
        }

        // the control for this id: made by `make` on the first frame (and again if NativeView
        // dropped the window after the control was not drawn for a while). nullptr without WinUI
        template <class Make>
        Control* controlFor(const char* id, ImVec2 guess, Make make) {
            sweep();
            const ImGuiID key = ImGui::GetID(id);
            Control& c = controls()[key];
            const bool dark = CurrentTheme().dark;
            const float scale = dpiScale();
            const ImVec2 frame = c.size.x > 0 ? frameOf(c.size) : ImVec2(guess.x * scale, guess.y * scale);
            centreOnLine(frame.y);
            void* view = NativeView(id, frame, [&]() -> void* {
                c = Control();
                c.dark = dark;
                c.control = make(c.state);
                c.root = wrap(c.control, dark);
                return WinUIIsland(winrt::get_abi(c.root));
            });
            if (!view)
                return nullptr;
            c.view = view;
            const auto actual = c.control.ActualSize(); // known once the island laid it out: a guess before
            if (actual.x > 0 && actual.y > 0)
                c.size = ImVec2(actual.x * scale, actual.y * scale);
            if (c.dark != dark) { // the app's theme changed
                c.dark = dark;
                c.root.RequestedTheme(dark ? mux::ElementTheme::Dark : mux::ElementTheme::Light);
            }
            c.control.IsEnabled(!disabled());
            return &c;
        }
    } // namespace

    bool nativeControlsAvailable() {
        return WinUIAvailable();
    }

    bool nativeSwitch(const char* id, bool* value) {
        const ImGuiID key = ImGui::GetID(id);
        Control* c = controlFor(id, ImVec2(60, 32), [&](const std::shared_ptr<State>& state) {
            mux::Controls::ToggleSwitch toggle;
            toggle.OnContent(nullptr); // no "On" / "Off" text: the switch alone
            toggle.OffContent(nullptr);
            toggle.MinWidth(0);
            // the template draws the switch about 3 px below the middle of its 32 px
            mux::Media::TranslateTransform up;
            up.Y(-3);
            toggle.RenderTransform(up);
            toggle.IsOn(*value);
            state->on = *value;
            toggle.Toggled([state](const winrt::Windows::Foundation::IInspectable& sender, const mux::RoutedEventArgs&) {
                state->on = sender.as<mux::Controls::ToggleSwitch>().IsOn();
                notify(*state);
            });
            return toggle;
        });
        const bool pressed = itemPressed(key);
        if (!c) {
            if (pressed)
                *value = !*value;
            return pressed;
        }
        State& state = *c->state;
        bool changed = false;
        if (state.changed) { // a click on the control itself: it already shows the new state
            state.changed = false;
            *value = state.on;
            changed = true;
        } else if (pressed) {
            *value = !*value;
            changed = true;
        }
        if (state.on != *value) { // set from the app (or the test engine)
            state.quiet = true;
            c->control.as<mux::Controls::ToggleSwitch>().IsOn(*value);
            state.on = *value;
            state.quiet = false;
        }
        return changed;
    }

    // WinUI has no segmented control: a row of toggle buttons, the chosen one checked. the grid in the
    // island holds a UserControl (a Control, so IsEnabled reaches the buttons) around the row
    namespace {
        mux::Controls::StackPanel rowOf(const Control& c) {
            return c.control.as<mux::Controls::UserControl>().Content().as<mux::Controls::StackPanel>();
        }

        // the checked button follows `current`
        void showSelection(const Control& c, int current) {
            uint32_t index = 0;
            for (const auto& child : rowOf(c).Children())
                child.as<mux::Controls::Primitives::ToggleButton>().IsChecked(static_cast<int>(index++) == current);
        }
    } // namespace

    bool nativeSegmented(const char* id, std::span<const char* const> labels, int* current) {
        Control* c = controlFor(id, ImVec2(220, 32), [&](const std::shared_ptr<State>& state) {
            mux::Controls::StackPanel row;
            row.Orientation(mux::Controls::Orientation::Horizontal);
            state->selected = *current;
            int index = 0;
            for (const char* label : labels) {
                mux::Controls::Primitives::ToggleButton button;
                button.Content(winrt::box_value(winrt::to_hstring(label)));
                button.IsChecked(index == *current);
                button.Click([state, index](const winrt::Windows::Foundation::IInspectable&, const mux::RoutedEventArgs&) {
                    state->selected = index;
                    notify(*state);
                });
                row.Children().Append(button);
                ++index;
            }
            mux::Controls::UserControl holder;
            holder.Content(row);
            return holder;
        });
        if (!c)
            return false;
        State& state = *c->state;
        bool changed = false;
        if (state.changed) {
            state.changed = false;
            changed = state.selected != *current;
            *current = state.selected;
        }
        showSelection(*c, *current); // also puts back a button the user unchecked by clicking it again
        state.selected = *current;
        return changed;
    }

    bool nativePopUp(const char* id, std::span<const char* const> items, int* current) {
        Control* c = controlFor(id, ImVec2(160, 32), [&](const std::shared_ptr<State>& state) {
            mux::Controls::ComboBox box;
            for (const char* item : items)
                box.Items().Append(winrt::box_value(winrt::to_hstring(item)));
            state->selected = *current;
            box.SelectedIndex(*current);
            box.SelectionChanged([state](const winrt::Windows::Foundation::IInspectable& sender, const mux::Controls::SelectionChangedEventArgs&) {
                state->selected = sender.as<mux::Controls::ComboBox>().SelectedIndex();
                notify(*state);
            });
            return box;
        });
        if (!c)
            return false;
        auto box = c->control.as<mux::Controls::ComboBox>();
        if (!c->widthFixed && c->size.x > 0) {
            // a ComboBox sizes itself to its items, and measures narrower once its drop down has been
            // open (102 px before, 70 px after for Small / Medium / Large): pin the first width
            c->widthFixed = true;
            box.Width(box.ActualWidth()); // in DIPs, as WinUI measures
        }
        State& state = *c->state;
        bool changed = false;
        if (state.changed) {
            state.changed = false;
            changed = state.selected != *current;
            *current = state.selected;
        } else if (state.selected != *current) { // set from the app
            state.quiet = true;
            c->control.as<mux::Controls::ComboBox>().SelectedIndex(*current);
            state.selected = *current;
            state.quiet = false;
        }
        return changed;
    }
} // namespace imshell::detail
