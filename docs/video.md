# Video

`imshell::video` renders Dear ImGui to a video file with no window: a composition says how big, how fast and how long it is, and a function draws frame N.
It is optional, in its own target, `imshell::video`, and it is a design only for now: nothing here is built yet.
The model is the one fframes and mygo's render plugin share: a frame is a pure function of its number, so a render is fast, repeatable and easy for an agent to write.

## The shape

```cpp
#include "imshell/video.hpp"

imshell::video::Composition comp;
comp.width = 1920;
comp.height = 1080;
comp.fps = 60;
comp.frames = 5 * 60;
comp.setup = [] { /* fonts, theme: once, on the video's own ImGui context */ };
comp.draw = [](const imshell::video::Frame& f) {
    // f.index, f.time (seconds), f.progress (0..1), f.fps, f.size
    float x = imshell::video::Interpolate(f.index, {0, 60}, {-200.f, 100.f}, imshell::video::Ease::OutCubic);
    ImGui::SetNextWindowPos({x, 100});
    ImGui::Begin("card", nullptr, imshell::video::kBareWindow);
    ImGui::TextUnformatted("hello");
    ImGui::End();
};
comp.audio.push_back({.path = "music.mp3", .start = 0.0, .volume = 0.6, .fadeOut = 1.0});

imshell::video::RenderVideo(comp, "out.mp4");   // blocking; options below
imshell::video::Preview(comp);                  // a window, play/pause, a scrubber
```

`RenderVideo(comp, path, options)` takes `from`/`to` frame ranges (draft renders), a `progress` callback that can cancel, `motionBlur` and `encoder` (below).
`RenderFrame(comp, n)` returns one frame as RGBA pixels, which is how a test or an agent looks at a frame without encoding anything.

## Offscreen

The video owns its ImGui context and a render target; it never touches a window or the app's context.
It makes a context of its own, sets `DisplaySize` to the composition's size and `IniFilename` to null, and restores the caller's context when it returns, so it can run inside an app that is already drawing.
Per backend the ImGui renderer backend is initialised on that context without a platform backend, since there is no input.

| Platform | Target | Reaches the encoder by |
|---|---|---|
| macOS, iOS | a `MTLTexture` backed by a `CVPixelBuffer` (IOSurface) | the buffer goes to the encoder untouched, no copy |
| Windows | an `ID3D11Texture2D` render target | a staging texture and `Map`, then the encoder |
| Linux, Android | a framebuffer object on a surfaceless EGL context (no window, no `GtkGLArea`) | `glReadPixels` into a pixel buffer |

The first version covers macOS; the readback path is shared, so Windows and Linux add a backend file each, not a design.
Rendering is double buffered: the GPU draws frame N+1 while the encoder takes frame N.

## Determinism

Time comes from the frame number: before each frame the renderer sets `io.DeltaTime = 1 / fps` and never reads a clock, and nothing reaches ImGui's input.
Two things still make a frame depend on the ones before it: ImGui's own state and animations that integrate over time.

- ImGui state: a window's first frame has no size (auto-resize and layout need a second pass), so the renderer draws each frame twice and keeps the second, with state reset between frames by starting from a clean context state (no ini, no hover, no focus).
  A fresh frame and the same frame reached by playing are then the same picture.
- `imshell::Animate` (a spring) and ImAnim (`imshell::anim`) keep state and advance by `DeltaTime`, so they are right when frames are drawn in order and wrong after a jump: frame 300 reached by seeking is not frame 300 reached by playing.
  A composition that wants to seek, or be previewed with a scrubber, uses closed-form helpers instead: `Interpolate(frame, in, out, ease)`, `Spring(frame, fps, from, to, config)` (the analytic damped spring, a pure function of the frame), `Sequence`/`Series` for timing scenes, and `Loop`.
  `Frame` carries `sequential`, true only while a render plays frames in order, so a composition that does use a stateful animation can at least say so.
- `video::Verify(comp, {frames...})` draws each listed frame sequentially and by seeking and compares the pixels, which is the test for this rule.
  The preview seeks through the same pure path, so what the scrubber shows is what the file will contain.

Some imshell state is per process, not per ImGui context: the current theme, the `Animate` state map, the icon registry, native views and the wake flags of `frames.hpp`.
A render therefore calls `ApplyTheme()` inside its own context after creating it, never creates a native view or calls `requestFrames`, and runs with `Animate` jumping to its target (the reduce-motion switch, once it is in) so widgets such as `imshell::Button`, which ease their fill, draw the same in any frame order.
Until that switch is available, a composition that draws such widgets is only exact when played in order.

Text is the other source of drift: fonts rasterise through the same atlas for every frame, and the renderer builds it once before frame 0, so glyph positions do not change mid-render.

## Encoders

The `Encoder` is a small interface (`begin(width, height, fps, audio)`, `frame(pixels or buffer)`, `end()`), chosen by `options.encoder` and defaulting to the platform's own.

| Encoder | Where | Notes |
|---|---|---|
| `AVAssetWriter` (VideoToolbox, hardware) | macOS, iOS | H.264 or HEVC, the pixel buffer from the render goes straight in; audio is an AAC input on the same writer |
| Media Foundation sink writer | Windows | H.264 hardware where the GPU has it; later |
| `ffmpeg` on `PATH` | everywhere | raw frames over a pipe, `-c:v libx264` or the system's hardware codec; the Linux default, and the fallback for any platform; no link dependency |

Linux has no system encoder that is always there, and GStreamer would be a link dependency on every host, so Linux shells out to `ffmpeg` and says so clearly when it is missing.
Android's `MediaCodec` comes after the desktops.
Nothing is linked for the video target unless the host links `imshell::video`; the Apple encoder needs `AVFoundation`, `CoreVideo` and `CoreMedia`, which the system provides.

## Sound

A composition's `audio` is a list of clips: `path`, `start` (seconds on the timeline), `offset` and `duration` (trim), `volume`, `fadeIn`, `fadeOut`.
The mixer decodes each clip to 48 kHz stereo float, sums them into one buffer as long as the video, clamps, and gives the result to the encoder as one track.
Decoding is the platform's (`ExtAudioFile` on Apple, Media Foundation on Windows, `ffmpeg` elsewhere); the mixer itself is a few dozen lines and has no effects, ducking or resampling beyond the rate conversion.

## Motion blur

`options.motionBlur = {samples, shutter}` draws each frame `samples` times at times spread over `shutter` of a frame interval (0.5 is a 180 degree shutter) and averages them.
The sub-frames are accumulated on the GPU into a 16 bit float target, so the cost is `samples` draws and one resolve, and it only works for compositions that are pure functions of time, which is the same rule as seeking.
It is off by default.

## Preview

`Preview(comp)` opens an `imshell::App` window that draws the composition at the window's scale with a toolbar: play and pause, a scrubber, the frame and time, loop.
It runs in real time: the frame shown is `floor(elapsed * fps)` of a clock, dropping frames if the machine is slower, never stretching time.
It draws straight into the window on the window's own ImGui context, not through the offscreen path, so it needs no readback; the file render is the reference for how it looks.
The preview target links `imshell::imshell`; `RenderVideo` alone does not need a window system.

## Order of work

1. `video.hpp`, the composition, `Frame` and the closed-form helpers, with tests that need no GPU (easing, spring, sequence).
2. macOS: the Metal offscreen target, `RenderFrame` to PNG, then the `AVAssetWriter` encoder; `Verify`.
3. `ffmpeg` pipe encoder (Linux and the fallback), then the Linux EGL target.
4. Sound mixer.
5. `Preview`.
6. Motion blur.
7. Windows D3D11 and Media Foundation; iOS (the Metal path already works), Android after.
8. Web is last and may stay out.

## What it leaves out

Video decoding as a source (a clip inside a composition), transitions beyond what a composition draws itself, subtitles as a format, GPU effects shaders, and any editor UI: a composition is code.
