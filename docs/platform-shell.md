# Platform shell

`imshell::App` (`imshell/app.hpp`) is everything between the operating system and your ImGui code: the native window, the graphics device, the ImGui context with its platform and renderer backends, input, and the frame loop. You implement `imshell::Delegate`; imshell calls it.

## Config

| Field | Default | Meaning |
|---|---|---|
| `title` | empty | window title |
| `appName` | empty | names the app's data folder (`DataDir`), where `imgui.ini` is kept; empty = no `imgui.ini` |
| `width`, `height` | 1280 × 800 | initial window size, in points |
| `docking` | true | turns on `ImGuiConfigFlags_DockingEnable` |
| `vsync` | true | wait for the display refresh |
| `maxFps` | 120 | frame cap with vsync off; 0 = no cap |
| `titleBar` | false | the app draws its own title bar ([Title bar](title-bar.md)) |
| `titleBarHeight` | 0 | that bar's height, in points; 0 = the platform's own (28 macOS, 32 Windows, 36 Linux, 44 Android) |

## Delegate

| Hook | When |
|---|---|
| `setup()` | once, after the ImGui context exists: load fonts, set the style |
| `frame()` | every frame, between `ImGui::NewFrame()` and `ImGui::Render()` |
| `tick()` | every pass of the loop on macOS and Windows, also while the window is unfocused and `frame()` is not called; for work that must not wait for a frame. `requestFrames()` from another thread brings the next pass at once. Not called on the other shells yet |
| `openFile(path)` | a file was dropped on the window or opened with the app |
| `wantsFrames()` | before the loop sleeps; return true to keep drawing (default: `framesRequested()`) |
| `shouldQuit()` | every loop; true closes the window |
| `closeRequested()` | the user closes the window (close button, Cmd-Q, Alt-F4); return false to keep it open, e.g. to ask about unsaved work and quit later through `shouldQuit()` |

## The loop

`imshell::Run(config, delegate)` is the whole app: it creates the window, runs the loop and tears down, and returns 1 if the window could not be made. It is `App::create`, `run` and `destroy`, which a host can call itself (DearSQL does, to keep the `App` as a member). `run()` is the whole loop. It draws only when something happens: input, a resize, or a request to keep drawing. While idle it sleeps in the OS event wait, so an idle app uses no CPU.

Anything that animates calls `imshell::requestFrames()` (`imshell/frames.hpp`) to keep frames coming for a moment (250 ms by default); imshell's spinners already do. It is safe to call from any thread, and a request while the app is idle wakes the loop at once: call it whenever a worker hands the UI something to show. Otherwise an idle loop draws once a second (a heartbeat for producers that never ask).

On macOS, iOS, Android and Windows a frame whose draw data matches the last one is not drawn or presented (`src/frame_skip.cpp`, an exact copy of the last frame's buffers), so a busy loop with nothing moving costs the CPU work of the frame but no GPU work. A frame that uploads a texture, has a draw callback, or changes the display, scale or clear colour always draws. On Android a new or resized surface, or the system asking for a redraw, draws regardless, and the loop waits about one refresh (8 ms) after a skipped frame, since no `eglSwapBuffers` paced it. Linux draws every frame: `GtkGLArea` hands out a fresh buffer each frame, so a skipped frame would show garbage.

A host that drives frames itself, such as the ImGui test engine, calls `pollEvents()` and `renderFrame()` in its own loop instead of `run()`.

## Text input

Every ImGui text field, and `TextEditor`, takes text the way the platform's own fields do: input methods (Vietnamese, Chinese, Japanese, Korean and the rest), dead keys and compose sequences, and the soft keyboard on phones. Nothing to set up; it follows whichever field is active (`PlatformImeData.WantTextInput`).

- **The composition is typed into the field** as it goes and edited in place on each change (backspaces for what changed, then the new text), so the field shows it, and it is underlined under the caret as in a native field. Committing replaces it. This works through ImGui's own characters and keys, so any widget that reads them gets it.
- **macOS:** imshell is the input method client on GLFW's view: it reports the field's text and caret (input methods that rewrite what is already typed read it), keeps Backspace, Enter and the arrows away from the field while a composition is open, and puts the candidate window at the caret.
- **Linux:** a `GtkIMContext` (IBus, Fcitx or GTK's own) sees each key first; it gets the text around the caret and the caret's place for its popup.
- **Web:** a hidden `<textarea>` under the caret has the focus while a field is active, so the browser's input methods, dead keys and suggestions work on it, and what changes there is replayed into the field. A tap on a field focuses it within the tap itself, which is what makes phones open their keyboard. Keys that move or submit (arrows, Enter, Tab, shortcuts) still go to ImGui.
- **iOS and Android:** `UITextInput` and an `InputConnection` (see below).
- **Windows:** GLFW's characters; an input method composes in the system's own window at the caret and sends the result.
- Password fields are never read by an input method.

To see what an input method sends, run with `IMSHELL_IME_TRACE=1` (macOS, logged to stderr).

## Safe area

`imshell::SafeArea()` (`imshell/app.hpp`) gives the edges system UI covers, in ImGui units (`x` left, `y` top, `z` right, `w` bottom): the status (and navigation) bar on iOS and Android, zero on desktops; the rest of what system UI covers is outside the ImGui display. The shells also hand it to ImGui (`Platform_GetWindowWorkAreaInsets`), so the main viewport's `WorkPos`/`WorkSize` leave it out from the next frame on; lay full-screen windows out from `ContentRect()` to avoid that frame of lag: the main viewport inside `SafeArea()` and below the title bar (on Android the bar starts below the status bar).

## Data folder

`imshell::DataDir(appName)` is the app's own folder for settings and data, created on first call: `~/Library/Application Support/<appName>` on macOS and iOS (inside the app's container there), `%APPDATA%\<appName>` on Windows, `$XDG_DATA_HOME/<appName>` or `~/.local/share/<appName>` on Linux, and the app's internal storage on Android. With `Config::appName` set, ImGui keeps `imgui.ini` there instead of the current directory (which is `/` for an app started from Finder).

## Handles

- `glfwWindow()`: the GLFW window (desktop).
- `nativeWindow()`: `NSWindow*` on macOS, `UIWindow*` on iOS, `HWND` on Windows, `GtkWindow*` on Linux.
- `nativeView()`: the view ImGui draws into: the window's content view on macOS, the `MTKView` on iOS, the `HWND` on Windows, the `GtkGLArea` on Linux.
- `createTexture(rgba, w, h)`: upload pixels with the active backend; returns `ImTextureID{}` on failure.

## Screenshots and recordings

Not in imshell: they are host tooling. After `renderFrame()` the frame's `ImGui::GetDrawData()` is still valid until the next one, so a host can draw it again into its own offscreen texture with the platform's ImGui renderer and save or stream that (DearSQL's `tests/ui/frame_capture.mm` does it with Metal). Native views are not part of the draw data.

## iOS

UIKit runs the loop, so `main()` hands control to it:

```objc
int main(int argc, char* argv[]) {
    return imshell::RunUIKit(argc, argv, [MyViewController class]);
}
```

`MyViewController` is `ImShellViewController` (`imshell/ios.h`) or a subclass that adds native chrome such as navigation items or key commands. It owns the Metal view and draws at the display refresh while something happens; after 2 s without input, `requestFrames()` or a busy frame it pauses the view, which then draws only on input, a `requestFrames()` call or a 1 s heartbeat, as the desktop loops do. Create the `imshell::App` in its `loadView`, after calling `[super loadView]`. The controller sits in a `UINavigationController` whose bar stays hidden unless your subclass shows it.

Touch maps to ImGui like this:
- a tap clicks;
- a long press shows the edit menu over text, and right-clicks elsewhere;
- a finger drag scrolls with momentum, unless the cursor under it is a resize or text cursor;
- a text selection gets a handle under each end, and dragging one moves that end (the same on Android; not in password, read-only or word-wrapped fields, or a host's own editor);
- a trackpad or mouse is a mouse.

Text input goes through `UITextInput`, so IME and the space-bar cursor work. The soft keyboard follows `io.WantTextInput`, and the layout moves above it.

## Android

`NativeActivity` loads the app as a shared library, and the NDK's `android_native_app_glue` calls `android_main` on its own thread. Hand the glue's app to imshell, then run as on the desktop:

```cpp
#include "imshell/android.hpp"

void android_main(android_app* state) {
    imshell::SetAndroidApp(state);
    static MyApp delegate; // outlives a recreated activity
    imshell::Run(config, delegate);
}
```

A configuration change the manifest's `configChanges` cannot absorb (a system theme or overlay change, such as switching to three-button navigation) destroys the activity and starts a new one in the same process. `Run` then returns without tearing anything down, and the next `android_main`'s `SetAndroidApp` re-attaches the running shell (new glue thread, looper and window; the same ImGui and EGL contexts), so the app carries on where it was. A host that calls `create`, `run` and `destroy` itself keeps the `App` and the delegate alive the same way and skips `destroy` while `AndroidActivityRecreating()` is true.

The activity must be `io.imshell.ImShellActivity` (`android/io/imshell/ImShellActivity.java`) or a subclass. It brings what the NDK cannot reach: typed characters, the soft keyboard, the clipboard and the document picker. The soft keyboard edits through a real `InputConnection` (an invisible editor view), so suggestions, swipe typing, composing and autocorrect work: the connection keeps a buffer of what was typed since the last tap, and each edit is sent to ImGui as the difference (backspaces, then the new text). The word being composed is underlined under the caret, as in a native field (also on iOS, for marked text). Graphics are EGL with OpenGL ES 3, and ImGui works in dp (`DisplayFramebufferScale` is the screen density), like points on iOS. The activity is edge to edge on every Android version (the manifest targets API 36, where Play requires it): the ImGui area stops at the navigation bar, side cutouts and the soft keyboard, as on iOS (ImGui keeps popups inside it, and taps on the navigation bar go to the system); only the status bar lies inside it, as the safe area. The bar icons turn dark over a light theme. Back stays a key event (`enableOnBackInvokedCallback="false"`; target 36 would make it predictive back). The EGL context outlives the window, so textures survive the app going to the background.

Touch works as on iOS, through the same touch engine (`src/touch.cpp`): iOS feeds it from its gesture recognizers, Android from raw motion events. A long press over text selects the word under the finger and shows the system's floating text toolbar (Cut, Copy, Paste, Select all, sent to ImGui as Ctrl chords); anywhere else it right-clicks. Both give the long-press haptic tick. A mouse or trackpad is a mouse. Hardware keys go through `imgui_impl_android`.

`imshell::PickFile(mimeTypes, intoDir, done)` opens the system document picker. The picked file is copied into `intoDir`, because apps can't open another app's files by path, and `done` gets the copy's path a frame or two later. It is not called when the picker is cancelled.

### Building an APK

`imshell_android_apk(lib PACKAGE id LABEL name ...)` (`cmake/ImShellAndroid.cmake`) adds `<lib>_apk`, which packs the library into a signed APK next to it without Gradle: `javac` and `d8` for the activity, `aapt2` for the manifest, then `zipalign` and `apksigner` with the SDK's debug key. The SDK is found from the NDK's location (`<sdk>/ndk/<version>`), and a JDK must be on `PATH`. Debug builds are debuggable, so `adb shell run-as <package>` reaches the app's files. Optional arguments: `ACTIVITY` (a subclass), `JAVA` (more sources), `ASSETS`, `PERMISSIONS`, `VERSION`, `VERSION_CODE`.

Configure with the NDK toolchain, `-DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28` (the minimum).
