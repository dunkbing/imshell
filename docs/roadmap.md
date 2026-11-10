# Roadmap

What comes next, roughly in order. Each item has an issue with the problem, the proposal and when it counts as done.

## Shells

1. ✅ **Wake the loop instead of polling** ([#12](https://github.com/dunkbing/mydear/issues/12)): `requestFrames()` wakes an idle loop at once; idle apps stop drawing 5 frames a second.
2. ✅ **One touch engine for iOS and Android** ([#13](https://github.com/dunkbing/mydear/issues/13)): one state machine for taps, scrolling, drags and long presses, fed by each shell.
3. ✅ **Safe-area insets and Android edge-to-edge** ([#14](https://github.com/dunkbing/mydear/issues/14)): `WorkPos`/`WorkSize` and `mydear::SafeArea()` keep clear of bars, notches and cutouts; the Android shell targets API 36.
4. **iOS idles** ([#22](https://github.com/dunkbing/mydear/issues/22)): ✅ the Metal view pauses when nothing happens instead of drawing every display refresh.
5. ✅ **Android survives activity recreation** ([#24](https://github.com/dunkbing/mydear/issues/24)): a system theme or overlay change closes the app today.
6. **Skip identical frames** ([#20](https://github.com/dunkbing/mydear/issues/20)): ✅ macOS and iOS; Windows, Linux and Android to do. no render or present when the draw data did not change.

## Input and platform services

7. **Native text input on Android** ([#15](https://github.com/dunkbing/mydear/issues/15)): ✅ an `InputConnection` for composition, suggestions and swipe typing; next selection handles, autofill and a composing underline.
8. **File dialogs** ([#16](https://github.com/dunkbing/mydear/issues/16)): ✅ asynchronous open and save on every platform, saves through the system on mobile.
9. **Haptics** ([#18](https://github.com/dunkbing/mydear/issues/18)): ✅ `mydear::Haptic(kind)`.
10. **Font loading with icon merging** ([#23](https://github.com/dunkbing/mydear/issues/23)): ✅ `AddFont`, with the system CJK font merged in.

## App API

14. **What a new app needs from the shell**: ✅ `Run(config, delegate)` on desktops and Android, `ContentRect()`, `DataDir(appName)` with `imgui.ini` there, `Delegate::closeRequested()`, platform-default title bar heights, the title bar calls on iOS, alerts and prompts (`alert.hpp`). Next: window size and position restored across launches (no position on Wayland), and a CMake helper for the bundle, icon and installer.

## Tooling

11. **Frame capture everywhere** ([#17](https://github.com/dunkbing/mydear/issues/17)): not doing; screenshots and recordings are the host's tooling (see `docs/platform-shell.md`), so mydear has none.
12. **Standalone build and smoke test** ([#19](https://github.com/dunkbing/mydear/issues/19)): ✅ (run on macOS; Windows and Linux written, not run yet) `hello` and its probe on every platform without a host.
    Also: draw background windows that are still visible (`occlusionState` on macOS), verified for app switching with the screen unlocked; the loop only draws focused windows today.

## Long term

13. **Screen readers** ([#21](https://github.com/dunkbing/mydear/issues/21)): expose labelled ImGui items to each platform's accessibility API.
