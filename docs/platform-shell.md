# Platform shell

`mydear::App` (`mydear/app.hpp`) is everything between the operating system and your ImGui code: the native window, the graphics device, the ImGui context with its platform and renderer backends, input, and the frame loop. You implement `mydear::Delegate`; mydear calls it.

## Config

| Field | Default | Meaning |
|---|---|---|
| `title` | empty | window title |
| `width`, `height` | 1280 × 800 | initial window size, in points |
| `docking` | true | turns on `ImGuiConfigFlags_DockingEnable` |
| `vsync` | true | wait for the display refresh |
| `maxFps` | 120 | frame cap with vsync off; 0 = no cap |
| `titleBar` | false | the app draws its own title bar ([Title bar](title-bar.md)) |
| `titleBarHeight` | 28 | that bar's height, in points |

## Delegate

| Hook | When |
|---|---|
| `setup()` | once, after the ImGui context exists: load fonts, set the style |
| `frame()` | every frame, between `ImGui::NewFrame()` and `ImGui::Render()` |
| `openFile(path)` | a file was dropped on the window or opened with the app |
| `wantsFrames()` | before the loop sleeps; return true to keep drawing (default: `framesRequested()`) |
| `shouldQuit()` | every loop; true closes the window |

## The loop

`run()` is the whole loop. It draws only when something happens: input, a resize, or a request to keep drawing. While idle it sleeps in the OS event wait, so an idle app uses no CPU.

Anything that animates calls `mydear::requestFrames()` (`mydear/frames.hpp`) to keep frames coming for a moment (250 ms by default); mydear's spinners already do. It is safe to call from any thread, and a request while the app is idle wakes the loop at once: call it whenever a worker hands the UI something to show. Otherwise an idle loop draws once a second (a heartbeat for producers that never ask).

A host that drives frames itself, such as the ImGui test engine, calls `pollEvents()` and `renderFrame()` in its own loop instead of `run()`.

## Safe area

`mydear::SafeArea()` (`mydear/app.hpp`) gives the edges system UI covers, in ImGui units (`x` left, `y` top, `z` right, `w` bottom): the status (and navigation) bar on iOS and Android, zero on desktops; the rest of what system UI covers is outside the ImGui display. The shells also hand it to ImGui (`Platform_GetWindowWorkAreaInsets`), so the main viewport's `WorkPos`/`WorkSize` leave it out from the next frame on; lay full-screen windows out from `SafeArea()` itself to avoid that frame of lag. On Android the title bar strip starts below the status bar, so put the content at `SafeArea().y + TitleBarHeight()`.

## Handles

- `glfwWindow()`: the GLFW window (desktop).
- `nativeWindow()`: `NSWindow*` on macOS, `UIWindow*` on iOS, `HWND` on Windows, `GtkWindow*` on Linux.
- `nativeView()`: the view ImGui draws into: the window's content view on macOS, the `MTKView` on iOS, the `HWND` on Windows, the `GtkGLArea` on Linux.
- `createTexture(rgba, w, h)`: upload pixels with the active backend; returns `ImTextureID{}` on failure.

## Frame capture (macOS)

`requestFrameCapture(path, scale)` renders the next frame offscreen and saves it as a PNG, without the title bar strip; `startRecording` / `stopRecording` pipe frames to `ffmpeg`. Native views are not part of the frame, so they don't appear.

## iOS

UIKit runs the loop, so `main()` hands control to it:

```objc
int main(int argc, char* argv[]) {
    return mydear::RunUIKit(argc, argv, [MyViewController class]);
}
```

`MyViewController` is `MydearViewController` (`mydear/ios.h`) or a subclass that adds native chrome such as navigation items or key commands. It owns the Metal view and draws every display refresh. Create the `mydear::App` in its `loadView`, after calling `[super loadView]`. The controller sits in a `UINavigationController` whose bar stays hidden unless your subclass shows it.

Touch maps to ImGui like this:
- a tap clicks;
- a long press shows the edit menu over text, and right-clicks elsewhere;
- a finger drag scrolls with momentum, unless the cursor under it is a resize or text cursor;
- a trackpad or mouse is a mouse.

Text input goes through `UITextInput`, so IME and the space-bar cursor work. The soft keyboard follows `io.WantTextInput`, and the layout moves above it.

## Android

`NativeActivity` loads the app as a shared library, and the NDK's `android_native_app_glue` calls `android_main` on its own thread. Hand the glue's app to mydear, then run as on the desktop:

```cpp
#include "mydear/android.hpp"

void android_main(android_app* state) {
    mydear::SetAndroidApp(state);
    mydear::App app;
    if (app.create(config, delegate))
        app.run();
    app.destroy();
}
```

The activity must be `io.mydear.MydearActivity` (`android/io/mydear/MydearActivity.java`) or a subclass. It brings what the NDK cannot reach: typed characters, the soft keyboard, the clipboard and the document picker. Graphics are EGL with OpenGL ES 3, and ImGui works in dp (`DisplayFramebufferScale` is the screen density), like points on iOS. The activity is edge to edge on every Android version (the manifest targets API 36, where Play requires it): the ImGui area stops at the navigation bar, side cutouts and the soft keyboard, as on iOS (ImGui keeps popups inside it, and taps on the navigation bar go to the system); only the status bar lies inside it, as the safe area. The bar icons turn dark over a light theme. Back stays a key event (`enableOnBackInvokedCallback="false"`; target 36 would make it predictive back). The EGL context outlives the window, so textures survive the app going to the background.

Touch works as on iOS, through the same touch engine (`src/touch.cpp`): iOS feeds it from its gesture recognizers, Android from raw motion events. A long press over text selects the word under the finger and shows the system's floating text toolbar (Cut, Copy, Paste, Select all, sent to ImGui as Ctrl chords); anywhere else it right-clicks. Both give the long-press haptic tick. A mouse or trackpad is a mouse. Hardware keys go through `imgui_impl_android`.

`mydear::PickFile(mimeTypes, intoDir, done)` opens the system document picker. The picked file is copied into `intoDir`, because apps can't open another app's files by path, and `done` gets the copy's path a frame or two later. It is not called when the picker is cancelled.

### Building an APK

`mydear_android_apk(lib PACKAGE id LABEL name ...)` (`cmake/MydearAndroid.cmake`) adds `<lib>_apk`, which packs the library into a signed APK next to it without Gradle: `javac` and `d8` for the activity, `aapt2` for the manifest, then `zipalign` and `apksigner` with the SDK's debug key. The SDK is found from the NDK's location (`<sdk>/ndk/<version>`), and a JDK must be on `PATH`. Debug builds are debuggable, so `adb shell run-as <package>` reaches the app's files. Optional arguments: `ACTIVITY` (a subclass), `JAVA` (more sources), `ASSETS`, `PERMISSIONS`, `VERSION`, `VERSION_CODE`.

Configure with the NDK toolchain, `-DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28` (the minimum).
