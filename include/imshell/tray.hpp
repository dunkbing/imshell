#pragma once

#include "imshell/frames.hpp"
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if (defined(__APPLE__) && !TARGET_OS_IPHONE) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
#define IMSHELL_DESKTOP_SHELL 1
#endif

// a tray (macos menu bar, windows notification area, linux StatusNotifierItem) icon with a menu, hiding and showing the main
// window, and quitting, so an app can keep running with its window closed. the icon calls need a
// running App; click callbacks run on the main thread. everything is a no-op on ios, android and
// the web, so shared code needs no #if
namespace imshell {
    namespace tray {
        struct Item {
            std::string label;
            std::function<void()> onClick;
            bool separator = false; // a line; the rest is ignored
            bool enabled = true;
        };
        struct Options {
            std::string tooltip;
            // a png (a square, 64 px or more): a template image on macos (drawn in the menu bar's
            // colour from its alpha), the icon on windows. none: the first letter of the tooltip
            struct Icon {
                const unsigned char* png = nullptr;
                size_t size = 0;
            } icon;
            std::vector<Item> items;
            // a left click on the icon; none: it opens the menu. a right click always does
            std::function<void()> onActivate;
        };
        // creates the icon, or updates it to match options (call it again when the menu changes)
#ifdef IMSHELL_DESKTOP_SHELL
        void Set(const Options& options);
        void Remove();
        // true when an icon is really shown: false before Set, when Shell_NotifyIcon failed, or on linux
        // with no StatusNotifierWatcher (stock GNOME). then an app that hides on close would be
        // unreachable: quit instead
        bool Available();
#else
        inline void Set(const Options&) {}
        inline void Remove() {}
        inline bool Available() {
            return false;
        }
#endif
    } // namespace tray

    // the main window: hidden it leaves the screen, the taskbar and the Dock's window list while
    // the app keeps running (and the frame loop idles). ShowWindow also restores a minimised
    // window and brings the app to the front
#ifdef IMSHELL_DESKTOP_SHELL
    void HideWindow();
    void ShowWindow();
    bool WindowVisible(); // shown and not minimised
#else
    inline void HideWindow() {}
    inline void ShowWindow() {}
    inline bool WindowVisible() {
        return true;
    }
#endif

    namespace detail {
        struct QuitState {
            bool requested = false;   // Quit()
            bool terminating = false; // the system is terminating the app, for the length of that call
        };
        inline QuitState& quit() {
            static QuitState s;
            return s;
        }
    } // namespace detail
    // ends the run loop, bypassing Delegate::closeRequested(): the quit item of a tray menu
    inline void Quit() {
        detail::quit().requested = true;
        requestFrames();
    }
    // true when the app is quitting for real: Quit() was called, or the system asked (cmd-q and
    // the Dock's Quit on macos). a closeRequested() that hides the window instead of closing it
    // should still return true then
    inline bool QuitRequested() {
        return detail::quit().requested || detail::quit().terminating;
    }

} // namespace imshell
