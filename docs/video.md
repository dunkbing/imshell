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

macOS, Linux and Windows are built.
The portable part (`generic.cpp`: `RenderFrame`, `RenderVideo` through ffmpeg, `Verify`, blur averaging, the sound mix, a small PNG writer) sits on a `Rasterizer` (`rasterizer.hpp`) that a backend implements as imgui into an offscreen target read back as rgba, so each platform adds one backend file, not a design.
Its tests run over a fake rasterizer, so they need no GPU and run anywhere.
On Linux (`egl.cpp`) the target is a framebuffer object on a pbuffer EGL context with desktop OpenGL 3.3, using mesa's surfaceless platform when it is there; with `libegl1`, `libgl1` and `libgl1-mesa-dri` it renders on a machine with no GPU and no display (llvmpipe), which is how the Linux tests ran.
The sound is decoded by ffmpeg there, the PNG writer is uncompressed (a 1080p frame is about 8 MB), `Encoder::Default` is ffmpeg, and `Preview` is not available yet.
The caller's current GL context is not restored after a render on Linux, so a host that draws with GL itself makes its context current again afterwards.
On Windows (`d3d11.cpp`) the target is an offscreen RGBA texture read back through a staging copy, on the GPU or on WARP, Direct3D's software renderer, when there is none (the build host has none and ran the tests that way).
ffmpeg is the encoder there too and has to be on `PATH` or named in `Options::ffmpeg`; Media Foundation as a native encoder is not done.
Rendering is double buffered: the GPU draws frame N+1 while the encoder takes frame N.

## Determinism

Time comes from the frame number: before each frame the renderer sets `io.DeltaTime = 1 / fps` and never reads a clock, and nothing reaches ImGui's input.
Two things still make a frame depend on the ones before it: ImGui's own state and animations that integrate over time.

- ImGui state: a window's first frame has no size (auto-resize and layout need a second pass), so the renderer draws each frame twice and keeps the second, with state reset between frames by starting from a clean context state (no ini, no hover, no focus).
  A fresh frame and the same frame reached by playing are then the same picture.
- `imshell::Animate` (a spring) and ImAnim (`imshell::anim`) keep state and advance by `DeltaTime`, so they are right when frames are drawn in order and wrong after a jump: frame 300 reached by seeking is not frame 300 reached by playing.
  A composition that wants to seek, or be previewed with a scrubber, uses closed-form helpers instead: `Interpolate(frame, in, out, ease)`, `Spring(frame, fps, from, to, config)` (the analytic damped spring, a pure function of the frame), `Sequence`/`Series` for timing scenes, and `Loop`.
  `Frame` carries `sequential`, true only while a render plays frames in order, so a composition that does use a stateful animation can at least say so.
- `video::Verify(comp, frames)` draws each listed frame in order and again on a fresh context, compares the pixels and reports the frames that differ: the test for this rule.
  The preview seeks through the same pure path, so what the scrubber shows is what the file will contain.

Some imshell state is per process, not per ImGui context: the current theme, the `Animate` state map, the icon registry, native views and the wake flags of `frames.hpp`.
A render therefore calls `ApplyTheme()` inside its own context after creating it, never creates a native view or calls `requestFrames`, and runs with `Animate` jumping to its target (the reduce-motion switch, once it is in) so widgets such as `imshell::Button`, which ease their fill, draw the same in any frame order.
The switch is `imshell::SetReduceMotion(true)`, a process wide override: a render running while an app draws on another thread stops that thread's easing too, so render on the app's thread or while it is idle.

Text is the other source of drift: fonts rasterise through the same atlas for every frame, and the renderer builds it once before frame 0, so glyph positions do not change mid-render.

## Encoders

The `Encoder` is a small interface (`begin(width, height, fps, audio)`, `frame(pixels or buffer)`, `end()`), chosen by `options.encoder` and defaulting to the platform's own.

| Encoder | Where | Notes |
|---|---|---|
| `AVAssetWriter` (VideoToolbox, hardware) | macOS, iOS | H.264 or HEVC, the pixel buffer from the render goes straight in; audio is an AAC input on the same writer |
| Media Foundation sink writer | Windows | H.264 hardware where the GPU has it; later |
| `ffmpeg` (`Encoder::Ffmpeg`, `Options::ffmpeg` is the binary) | everywhere | raw rgba frames over a pipe, `libx264`, yuv420p; the Linux default, and the fallback for any platform; no link dependency, so a host started from the Dock may need the full path |

Linux has no system encoder that is always there, and GStreamer would be a link dependency on every host, so Linux shells out to `ffmpeg` and says so clearly when it is missing.
Android's `MediaCodec` comes after the desktops.
Nothing is linked for the video target unless the host links `imshell::video`; the Apple encoder needs `AVFoundation`, `CoreVideo` and `CoreMedia`, which the system provides.

## Sound

A composition's `audio` is a list of clips: `path`, `start` (seconds on the timeline), `offset` and `duration` (trim), `volume`, `fadeIn`, `fadeOut`.
The mixer decodes each clip to 48 kHz stereo float, sums them into one buffer as long as the video, clamps, and gives the result to the encoder as one track.
Decoding is the platform's (`AVAssetReader` on Apple, Media Foundation on Windows, `ffmpeg` elsewhere); the mixer itself is a few dozen lines and has no effects, ducking or resampling beyond the rate conversion.

A clip with `start` before the first rendered frame (or a frame range that begins mid-clip) is cut to the window.
The whole mix is held in memory as floats (about 23 MB a minute), which is fine for the clips a composition plays and would need streaming for hours of sound.
On Apple the track is AAC through the same `AVAssetWriter` as the picture, fed in step with the frames; with `Encoder::Ffmpeg` the mix goes to a temporary raw file next to the output that ffmpeg reads as a second input and the renderer removes afterwards.
A clip that cannot be read fails the render and leaves no file.

## Motion blur

`Options::blur = {samples, shutter}` draws each frame `samples` times at moments spread over `shutter` of a frame interval (0.5 is a 180 degree shutter), centred on the frame, and averages them.
`Frame::offset` carries the sample's distance from the frame in frames, and `Frame::position()` and `time()` include it: a composition that gives `position()` to `Interpolate` and `Spring` moves between samples, one that reads `index` does not and comes out sharp.
It is off by default (`samples` 1), and `RenderFrame(comp, n, blur)` shows a blurred frame without encoding.
The samples are averaged on the CPU after a readback, in the encoded (not linear) values, so the cost is `samples` draws and reads per frame; accumulating on the GPU in a 16 bit float target is the way to make it cheaper later.
Like seeking, it only means something for compositions that are pure functions of time.

## Preview

`Preview(comp, options)` opens an `imshell::App` window that plays the composition with a toolbar: play and pause (space too), loop, the frame and time, and a scrubber.
It runs in real time: the frame shown is the start frame plus `elapsed * fps` of a clock, dropping frames if the machine is slower, never stretching time.
Each frame is drawn by the same renderer a file render uses, on its own ImGui context, and copied into one texture that the window shows scaled to fit, so the preview is the file's picture and the scrubber seeks through the pure path (`Frame::sequential` is false).
The cost is a readback of every shown frame, fine for a preview.
It plays no sound yet, and the reduce-motion override a render sets stays on while the window is open, so the toolbar does not ease either.
`PreviewOptions::closeAfter` closes the window by itself, for a smoke test; `examples/video` previews or renders a small composition.
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
