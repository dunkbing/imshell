# Video

`imshell::video` renders Dear ImGui to a video file with no window: a composition says how big, how fast and how long it is, and a function draws frame N.
It is optional, in its own target, `imshell::video`, and builds on macOS, Linux and Windows.
The model is the one fframes and mygo's render plugin share: a frame is a pure function of its number, so a render is fast, repeatable and easy for an agent to write.

## Quick start

`scripts/new-video NAME` checks the tools (and says what to install), copies `examples/video/starter/` (a `CMakeLists.txt` and a `main.cpp`, a 4 s title card) and builds it. By hand:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DIMSHELL_DIR=/path/to/imshell # a C++20 compiler and CMake; glfw is fetched, Linux also needs ffmpeg
cmake --build build
./build/starter timeline                      # the scenes and their times, no pixels
./build/starter strip all -n 12 -o strip.png  # a contact sheet to look at
./build/starter inspect all && ./build/starter verify
./build/starter render -o out.mp4
```

The project's CMake is six lines: set `IMSHELL_FILE_DIALOGS` OFF, `add_subdirectory` imshell, glob `${IMSHELL_IMGUI_DIR}/imgui*.cpp`, link `imshell::video`.
Load a font in `comp.setup` (`ImGui::GetIO().Fonts->AddFontFromFileTTF(path, 0.f)`) and draw with it; the built-in one is a pixel font.

Easy to get wrong:

- Never multiply `ImGui::GetStyle().Alpha` and divide it back: the drift survives in a playing context and not in a seek one, so `verify` fails. Use `PushStyleVar(ImGuiStyleVar_Alpha, ...)` / `PopStyleVar()`.
- Draw-list colours ignore style alpha; apply it to the colour yourself.
- `frame` renders one frame in a fresh context, so auto-sizing child windows come out clipped. `render --images` or the last frame of a `strip` lets the layout settle.

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
The sound is decoded by ffmpeg there, the PNG writer is uncompressed (a 1080p frame is about 8 MB), `Encoder::Default` is ffmpeg, and `Preview` is a window over the same rasterizer (below).
The caller's current GL context is not restored after a render on Linux, so a host that draws with GL itself makes its context current again afterwards.
On Windows (`d3d11.cpp`) the target is an offscreen RGBA texture read back through a staging copy, on the GPU or on WARP, Direct3D's software renderer, when there is none (the build host has none and ran the tests that way).
`Encoder::Default` there is Media Foundation (`mf.cpp`): the system's h.264 encoder (software, so no GPU is needed) and aac encoder into an mp4, and its source reader decodes the sound clips, so a render needs no ffmpeg; `Encoder::Ffmpeg` still works.
The sound is written in step with the frames, as on Apple. Checked on the build host with no ffmpeg on `PATH`: h.264 baseline, 30 frames, aac 48 kHz stereo, audible.

Preview on Linux and Windows (`preview.cpp`) shows each frame the rasterizer draws through a texture imgui keeps up to date itself (`ImTextureData`, so no platform texture code), with the rasterizer on its own thread so its GL context does not replace the window's.
Run on Linux under xvfb, where frames advance and the picture shows; on Windows it builds, but the build host's SSM session has no desktop, so the window was not run there.
A frame is drawn and waited for before the next, so the GPU and the encoder do not overlap yet; double buffering is a later speed-up.

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
The tool is for desktops (macOS, Linux, Windows), where an agent drives it from a shell; there are no phone encoders or footage decoders, and none are planned.
Nothing is linked for the video target unless the host links `imshell::video`; the Apple encoder needs `AVFoundation`, `CoreVideo` and `CoreMedia`, which the system provides. A host that only renders can configure imshell with `-DIMSHELL_FILE_DIALOGS=OFF`: the file dialogs (and the nativefiledialog-extended package) are left out, and a dialog never opens.

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

## Scenes
`imshell/video_scenes.hpp`: a composition of ideas that follow each other.
A `Scene` has a name, its `frames` (its share of the timeline), an `overlap` and a draw function whose `Frame::index` counts from the scene's own first frame (`Frame::scene` is its number, for window names that must differ between scenes: `ImGui::Begin("card##a")` and `"card##b"`, since two windows with one name are one window).
An overlap fades the scene in over the end of the previous one, which stays fully visible underneath so the picture never dips; the scene starts drawing, and its entrance animation starts, at the first overlap frame, and the overlap is not added to the total length.
`SceneLayers(position, scenes)` is pure: which scenes are on screen at a frame, their local frame, length and alpha (and the blur fraction), bottom first; past the end the last scene holds its last frame.
`DrawScenes` draws them with `ImGuiStyleVar_Alpha` pushed while a scene fades, so text and widgets fade with it; `MakeComposition(w, h, fps, scenes)` returns a `Composition` whose `marks` name the scenes, so `timeline` lists them and the command line takes `outro`, `outro@1.2s`, `outro@end`, `#2` and a scene name as a range (`strip outro`).
`SpringIn(frame, fps, from, to, start, frames)` is a spring that rests after `frames` frames: the unit spring's settle time is stretched to that length, so the bounce stays and the timing is the planned one.

## Command line
A video's `main` can be one line, `return imshell::video::Cli(comp, argc, argv, "name");` (`imshell/video_cli.hpp`, `cli.cpp`, over the public API only), and then the video is worked on from a shell, by a person or by an agent that cannot watch it.
`timeline` prints the size, rate, length and the sound clips; `frame TIMES` writes full-size PNGs, with `--crop x,y,w,h` shown zoomed for small details; `strip` writes a contact sheet of evenly spread frames (or chosen times) with each cell labelled by its frame number; `render [RANGE]` encodes, with `--draft` at half size and `--scale F` at any size; `preview [TIME]` opens the window at a time; `verify` runs `Verify` over a spread of frames (exit 2 when playing and seeking differ).
Times are a frame (`120`), `3.2s`, `500ms`, `1:05.5`, `50%`, `start` or `end`; ranges are `a..b` (b excluded), `a..`, `..b` or `all`.
`--json` prints one JSON object on stdout, progress goes to stderr.
A draft or a scale changes `Composition::width/height`, so a draw function has to lay out from `Frame::width/height`.
`Options::codec` (`h264`, `hevc`, `vp9`, `prores`), `bitrate` (kbit/s) and `crf` (constant quality) choose how `RenderVideo` encodes; any of them, or `Encoder::Ffmpeg`, goes through the ffmpeg binary (`NeedsFfmpeg`), since the platform encoders only do h.264 at their own bitrate.
The file's extension picks the container (`.mp4`, `.webm`, `.mov`); vp9 gets opus sound and the others aac, and prores is 10 bit 4:2:2 with no bitrate or crf.
The command line has `--codec`, `--bitrate`, `--crf`, and `render --images -o dir` writes `frame_000000.png` and so on through `Frames`.
`Composition::transparent` (`--transparent`) clears to transparent instead of black: the renderers leave colour premultiplied (ImGui's blending), `Unpremultiply` makes it straight before it reaches an `Image`, a png or ffmpeg, and averaging motion blur happens before that.
The outputs that hold it are `--images` (png), prores (`yuva444p`, 4444) and vp9 (`yuva420p`, `.webm`); h264 and hevc refuse with a message, since libx265 has no alpha and the platform encoders are not asked for it.

`inspect [RANGE]` (`video_check.hpp`, `check.cpp`) plays the frames in order and reports what a viewer would notice: a window whose vertices leave the canvas (a warning at the first or last frames of its scene, where it is probably an entrance or an exit, an error anywhere else, with the overhang in pixels and the command to look at it), frames with nothing drawn (a note), and pops, a part of the picture that changes in one frame between frames that hardly move, with its region (a hard cut at a scene boundary is not one).
It reads two things: the draw data after imgui laid the frame out, through `drawProbe()` (a process-wide callback each renderer calls once per frame), and the pixels, in 4x4 cells.
The window checks skip the background and foreground draw lists, which are meant to be bigger than the canvas; windows are checked by name, so two windows with one name are one.
`--fail-on warning` makes warnings fail too; the exit code is 2 on a failure.
`AudioClip::scene` names a mark: that clip's `start` counts from the scene's first frame (`PlacedAudio(comp)` gives the clips with absolute starts, which every mixer and `timeline` use), so changing a scene's length moves its sounds; an unknown name leaves the start as written and `timeline` says so.
Captions (`video_captions.hpp`, header only): `ParseCaptions` reads WebVTT or SRT into `Cue`s with per-word times (WebVTT inline `<00:01.500>` stamps, otherwise the words are spread evenly over the cue; untimed words after a timed one ride with it); `DrawCaptions(f, cues, style)` wraps the cue showing at `f.time()` over the bottom of the frame on the foreground draw list, optionally lighting spoken words (`CaptionStyle::karaoke`), and takes the alpha of the style stack so a fading scene fades its captions.
It draws no window, so `inspect` does not see it; check it with `frame` at a few cue times.
Audio visualizers (`video_audio_viz.hpp`, header only): `AudioAnalysis(comp)` mixes the composition's sound once (`RenderAudio`, from frame 0) and answers for any time: `level(t)` (rms, 0..1 over -60 to 0 dBFS), `bands(t, out, release)` (log-spaced 40 Hz to 16 kHz from a 2048 sample Hann FFT, the loudest bin per band; `release` is a decay over the last six 20 ms windows, which keeps it a pure function of `t`) and `waveform(t, span, out)`; `DrawBars` and `DrawWave` draw them.
Build it from the composition before its `draw` is assigned, and ask it with `f.time()`; with `--draft`/`--scale` it still holds, as it does not depend on the size.
Footage (`video_footage.hpp`, `footage.cpp`): `Footage clip(path, comp.fps, maxWidth)` reads a video through the `ffmpeg` binary (`ProbeVideo` takes its size, rate and length from `ffmpeg -i`, so no ffprobe; a clip rotated by metadata is read upright) and `clip.Draw(drawList, t, pos, size, Fit, tint, rounding, crop)` draws the picture `t` seconds into the clip (`Fit::Cover`, `Contain` or `Stretch`, applied to `crop`, the part to show as left, top, right, bottom fractions); `clip.pixels(t)` gives the rgba. A png, jpeg, bmp or tga path is a one-frame footage decoded in process (no ffmpeg, no process per picture): use it for logos and screenshots.
The picture is a pure function of `t`: frame `n = floor(t * fps)` is read on from the running decoder when it is next (or up to 30 ahead), and a seek restarts ffmpeg at `n / fps` (`-ss` before `-i`, decoded and trimmed to the frame), which the tests show gives the same pixels as playing to it; before 0 is the first frame and past the end the last is held.
One texture per ImGui context is kept (each renderer has its own), as an `ImTextureData` the backend uploads from its `Updates` list (not `UpdateRect`: the three backends read `Updates`); a context created at a freed one's address is detected by asking it for the texture.
The footage must outlive every render that draws it, a decoder runs on whichever thread asks (under a lock), and it needs the ffmpeg binary on the rendering machine. The clip's sound is an `AudioClip` with the same path (`ffmpeg` and the platform decoders both read the audio of a video file).
Effects (`video_effects.hpp`, header only): `Composition::post` is `void(const Frame&, Image&)`, run on every finished frame after the blur is averaged and the colour made straight (render, `frame`, `strip`, `inspect`, `verify`, `Frames` and Preview all see it; a filter that changes the size is ignored). The filters are `Grade` (brightness, contrast, saturation, gamma), `Vignette`, `Grain` (hash noise from a seed: pass `f.index` for grain that moves), `Blur`, `Glow`, `Chromatic` and `Pixelate`; sizes are fractions of the height, so `--draft` looks like the full render.
They run on the CPU on purpose: the same maths on every renderer and machine keeps a frame a pure function of its number, which `verify` relies on, and there is no shader dialect to keep in step across Metal, GLSL and HLSL. The price is time: the chain in the demo (glow, fringing, grade, vignette, grain) costs about 50 ms per 720p frame, so a long 1080p render with effects takes minutes longer.
GPU fragment shaders are not offered. `Verify` on Apple now plays through the same exposure path a render uses, so a transparent frame or a post filter is compared in the form that is encoded.
`audio analyze [RANGE]` mixes the sound as a render would (`RenderAudio`) and prints the integrated loudness (ITU-R BS.1770: K-weighting, 400 ms blocks every 100 ms, the -70 LUFS gate and the -10 LU relative gate), the true peak (4x oversampled), sample peak, rms, clipped samples (exit 2) and silences of 0.3 s or more under -60 dBFS; `audio at TIMES` says what plays at a moment, from where in its file and how loud; `audio render` writes the mix as a 32 bit float wav.
`Frames(comp, options, fn)` plays a range in order and hands each frame to `fn`: the building block of the checks, and of anything that wants the pixels (image sequences, thumbnails).
The contact sheet labels are digits only (a 3x5 bitmap font): the frame number is the only text it needs.
`skills/imshell-video/SKILL.md` is the working method for an agent (plan first, no template leftovers, the loop above, motion and sound rules).

## Scripts (no compiler)

`imshell-video scene.js <command>` runs a scene written in JavaScript through the same command line as a C++ composition (`timeline`, `frame`, `strip`, `inspect`, `verify`, `render`, `preview`).
It is one binary with an embedded JavaScript engine (quickjs-ng) and a font (JetBrains Mono), so a video needs no compiler, CMake or package manager.
Install it with one line, which downloads the release binary for your system and checks its SHA-256:

```sh
curl -fsSL https://raw.githubusercontent.com/dunkbing/imshell/main/scripts/install-video.sh | sh      # macOS arm64, Linux x64
irm https://raw.githubusercontent.com/dunkbing/imshell/main/scripts/install-video.ps1 | iex             # Windows (PowerShell)
```

Or build it with `-DIMSHELL_VIDEO_JS=ON` (default off; quickjs-ng is fetched at a pinned tag; `-DIMSHELL_FETCH_GLFW=ON` links glfw statically so the binary runs on other machines).
`imshell-video api` prints everything a scene can call, and `examples/video/script/scene.js` is the starter.
A tag `video-vX.Y` runs `.github/workflows/video-release.yml`, which builds macOS arm64, Windows x64 and (experimental, untested) Linux x64 and publishes the archives with a `SHA256SUMS`.

```js
video({
  width: 1280, height: 720, fps: 30, frames: 120,
  draw(f) {
    layer('back');
    rect(0, 0, f.width, f.height, '#0e1220');
    text('Hello', 100, 100 + springIn(f.position, f.fps, 40, 0, 6, 30), 96, rgba(255, 255, 255, interpolate(f.position, [6, 20], [0, 1], 'out')));
  },
});
```

It has the drawing calls, motion helpers, ImGui windows with real widgets, scenes with dissolves, audio and post effects (as data: the filters run natively, never per pixel in the script).
A thrown error prints its message and `file:line:col` stack and exits 1; a frame never renders blank because a script failed.
A frame must depend only on its number: `Math.random` is reseeded from the frame and `Date.now()` is 0, and a global that changes between frames is the script version of a `static`, which `verify` catches.
Rendering runs at the same speed as the C++ path (the same 120-frame 720p scene renders in 0.38 s either way on an M4), because drawing and encoding are native; what a script removes is the toolchain.
Not in the script front end: custom ImGui calls beyond the widgets above.
Colours are `rgba(r, g, b, a)`, `'#rrggbb'` (also `#rgb`, `#rrggbbaa`) or CSS-style `'rgba(255, 255, 255, 0.3)'`; any other string is an error naming it.
Scenes crossfade (the one leaving stays opaque while the next fades in over it); `crossfade: false` on a scene fades both, which dips dark at the middle.
`gradientRect`, `shadow` (soft, from stacked rounded rects) and `clip` (cuts what a callback draws, e.g. an image panning in a frame) cover the common looks; `--set key=value` on any command becomes `args.key`, so one script renders many videos (`render --set name=Ada -o ada.mp4`).
`onion -n 8` lays frames of a range over each other on their median background, so a movement's path and easing show in one picture.
`silentAudio: true` (or `render --silent-audio`) writes a silent stereo AAC track when a scene has no sound: an App Store preview is refused without one.
`verify` ignores a one level (1/255) difference per channel, which is the GPU rounding a few edge pixels between a long-running context and a fresh one, and reports the largest difference of any pixels beyond that.
Known: on a 768-frame portrait scene the last frame differed by up to 28/255 in about 26 pixels between playing and seeking; smaller scenes do not reproduce it and the cause is not found.

## Order of work

1. `video.hpp`, the composition, `Frame` and the closed-form helpers, with tests that need no GPU (easing, spring, sequence).
2. macOS: the Metal offscreen target, `RenderFrame` to PNG, then the `AVAssetWriter` encoder; `Verify`.
3. `ffmpeg` pipe encoder (Linux and the fallback), then the Linux EGL target.
4. Sound mixer.
5. `Preview`.
6. Motion blur.
7. Windows D3D11 and Media Foundation (done); phones are out of scope.
8. Web is last and may stay out.

## What it leaves out

Video decoding as a source (a clip inside a composition), transitions beyond what a composition draws itself, subtitles as a format, GPU effects shaders, and any editor UI: a composition is code.
