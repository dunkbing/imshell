# Roadmap

What comes next, roughly in order. Each item has an issue with the problem, the proposal and when it counts as done.

## Shells

1. ✅ **Wake the loop instead of polling** ([#12](https://github.com/dunkbing/mydear/issues/12)): `requestFrames()` wakes an idle loop at once; idle apps stop drawing 5 frames a second.
2. **One touch engine for iOS and Android** ([#13](https://github.com/dunkbing/mydear/issues/13)): one state machine for taps, scrolling, drags and long presses, fed by each shell.
3. **Safe-area insets and Android edge-to-edge** ([#14](https://github.com/dunkbing/mydear/issues/14)): `WorkPos`/`WorkSize` and `mydear::SafeArea()` keep clear of bars, notches and cutouts; the Android shell targets API 36.
4. **iOS idles** ([#22](https://github.com/dunkbing/mydear/issues/22)): the Metal view pauses when nothing happens instead of drawing every display refresh.
5. **Skip identical frames** ([#20](https://github.com/dunkbing/mydear/issues/20)): no render or present when the draw data did not change.

## Input and platform services

6. **Native text input on Android** ([#15](https://github.com/dunkbing/mydear/issues/15)): an `InputConnection` for IME composition, suggestions and swipe typing; then selection handles and autofill.
7. **File dialogs** ([#16](https://github.com/dunkbing/mydear/issues/16)): asynchronous open and save on every platform, saves through the system on mobile.
8. **Haptics** ([#18](https://github.com/dunkbing/mydear/issues/18)): `mydear::Haptic(kind)`.
9. **Font loading with icon merging** ([#23](https://github.com/dunkbing/mydear/issues/23)).

## Tooling

10. **Frame capture everywhere** ([#17](https://github.com/dunkbing/mydear/issues/17)): `requestFrameCapture` beyond macOS, for docs and store screenshots.
11. **Standalone build and smoke test** ([#19](https://github.com/dunkbing/mydear/issues/19)): `hello` and its probe on every platform without a host.

## Long term

12. **Screen readers** ([#21](https://github.com/dunkbing/mydear/issues/21)): expose labelled ImGui items to each platform's accessibility API.
