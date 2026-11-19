---
name: imshell-video
description: >-
  Make a video with imshell::video: a C++ function from frame number to a Dear ImGui picture,
  rendered offscreen to an mp4 with sound (macOS, Linux, Windows). Use when the user wants a
  promo, product demo, explainer, title card or any animated clip made from their app's own UI
  or ImGui widgets, or asks to change, check or render such a video. Covers planning, writing
  the composition, motion rules, sound, and checking the result with the command line (timeline,
  frame, strip, verify, render, preview) without watching it.
---

# Making videos with imshell::video

A video is a `Composition`: a size, a rate, a length and a `draw(const Frame&)` function that
draws frame `f.index` with ImGui. Every frame is a pure function of the frame number, so playing,
seeking and rendering give the same picture. Design notes for contributors are in `docs/video.md`;
this file is the working method.

## Rules before anything else

### Plan before you write code

1. Pin down the brief: who watches it and where (the platform decides format and length), the
   one thing the viewer must remember, the tone, and which assets exist (logo, screenshots,
   music, voice). Ask for what is missing. Never invent product facts, numbers or quotes.
2. When the video is cut to music or a voice, choose the sound first: its beats and phrases set
   the timing of everything else.
3. Plan the motion before the layout: where each element comes from, how it changes, what it
   hands over to the next scene. Tie sound effects to the same constants that drive the motion.

### No slop

A video made of default choices could be about any product with the text swapped. Do not ship
it: no placeholder text or colours, no whole-frame zoom on every beat, no single motion (the same
fade-up) for everything, no static scenes where one thing appears and the rest holds, no stock
copy ("Introducing", "Seamless", three-word taglines), and little text overall. Give every
element a reason tied to the subject. Spend effort on motion: staggered entrances, secondary
motion, things that react to each other, an element carried across a cut. Look at a strip and ask
whether it would still work for another product; if so, change the idea, not the colours.

### Check by looking, not by claiming

Look at the PNGs before saying anything looks good. Use `strip` for flow, `frame --crop` for
detail, `verify` before showing a finished video.

## The loop

Every video's `main` is `return imshell::video::Cli(comp, argc, argv, "name");`. Use the
program for everything (the examples use the `imshell_video_demo` name):

| command | use it to |
| --- | --- |
| `timeline [--json]` | size, rate, length and every sound clip with its placement |
| `strip [RANGE \| TIMES] -n 12 --columns 4 --width 480 -o strip.png` | a contact sheet, each cell labelled with its frame number: judge layout and rhythm |
| `frame TIMES -o dir [--crop x,y,w,h] [--zoom N]` | full-size PNGs; a crop is shown zoomed (about 960 px wide) for small details |
| `verify [RANGE]` | frames drawn in order must equal frames jumped to (exit 2 if not) |
| `inspect [RANGE] [--no-pops] [--fail-on warning]` | windows cut off by the canvas (a warning at a scene's start or end, an error in the middle), blank frames, pops between frames; each finding says which command shows it (exit 2 on errors) |
| `audio analyze [RANGE]` | loudness (LUFS), true peak, clipping, silence of the mix (exit 2 on clipping) |
| `audio at TIMES` | which sounds play at a moment, from where in the file, how loud |
| `audio render [RANGE] -o a.wav` | the mix as a wav, to listen to without rendering the video |
| `render [RANGE] -o out.mp4 [--draft] [--scale F] [--ffmpeg] [--codec h264\|hevc\|vp9\|prores] [--bitrate KBPS] [--crf N] [--images]` | encode; `--draft` is half size and quick; a codec, bitrate or crf needs ffmpeg and the extension picks the container (`.webm` for vp9, `.mov` for prores); `--images -o dir` writes a png per frame |
| `preview [TIME] [--paused]` | the real-time window, for the user to watch (no sound); it blocks until closed |

Times: `120` (a frame), `3.2s`, `500ms`, `1:05.5`, `50%`, `start`, `end`; ranges `a..b` (b excluded),
`a..`, `..b`, `all`; several times separated by commas. With scenes (below) a scene name is a time or a range: `outro`, `outro@1.2s`, `outro@end` (counted from the scene's first frame), `#2`, `strip outro`. `--json` prints one JSON object on
stdout; progress goes to stderr.

Working order: `timeline` (does the structure and pacing add up?), `strip all -n 12` (does it
read?), `frame` on the moments that matter, fix, `inspect` and `verify`, `audio analyze`, a `--draft` render to check the encode,
then the full `render`. When the user wants to watch, show the exact command
(`./build/my_video preview 8.5s`) with a start time just before the changed section, and offer to
run it. Do not render the whole video after every edit.

`--draft` and `--scale` change `Composition::width/height`: lay out from `f.width`/`f.height`
(percentages or a scale factor), never fixed pixel numbers, or the draft will be cropped or
empty. Keep a release build; debug renders many times slower.

## Writing the composition

```cpp
Composition comp;
comp.width = 1920; comp.height = 1080; comp.fps = 60; comp.frames = 6 * 60;
comp.setup = [] { /* once, on the video's own ImGui context: fonts (imshell::AddFont), theme */ };
comp.draw = [](const Frame& f) {
    const float x = Spring(f.position(), f.fps, -400.f, 120.f, /*start frame*/ 12);
    const float fill = Interpolate(f.position(), {30, 150}, {0.f, 1.f}, Ease::InOutCubic);
    // ... ImGui::SetNextWindowPos / Begin / widgets / End, using x and fill
};
comp.audio = {{.path = "music.mp3", .volume = 0.6f, .fadeIn = 1.0, .fadeOut = 2.0},
              {.path = "whoosh.wav", .start = 3.1, .volume = 0.8f}};
```

- **Everything is a function of `f`.** No clocks, no `static` state that changes, no input, no
  random numbers without a fixed seed per frame. A value that depends on the previous frame
  breaks seeking; `verify` catches it.
- Use `f.position()` (frame plus the blur sample offset) in `Interpolate`, `Spring`, `Loop` so
  motion blur works; `f.index` is fine for things that must not blur.
- `Interpolate(frame, {in}, {out}, ease)` maps and clamps; `Spring(frame, fps, from, to, start)`
  is the exact damped-spring solution (set `SpringConfig` for bouncier or stiffer);
  `SpringIn(frame, fps, from, to, start, frames)` is a spring that rests after `frames` frames;
  `Loop(frame, length)` repeats.
- **Scenes** (`imshell/video_scenes.hpp`): `MakeComposition(w, h, fps, {Scene{"intro", 150, 0, draw}, Scene{"main", 200, 30, draw}})`.
  A scene's `Frame::index` counts from its own first frame; `overlap` frames fade it in over the end of
  the previous scene (which stays visible underneath), starting its entrance at the same moment.
  Name windows per scene (`"card##" + std::to_string(f.scene)`): two windows with one name are one window.
  `timeline` lists the scenes, and `strip main`, `frame main@1.2s`, `render main` address them.
- Enter with a spring or ease-out over 300–600 ms, leave faster (ease-in 200–300 ms), stagger
  related items 60–120 ms, give the viewer 1–2 s to read after motion settles, and never move
  everything at once. Keep a little motion during holds.
- Draw with the theme's colours (`imshell::CurrentTheme()`) and fonts the app uses. At 1920×1080
  titles are 96–140 px, body 44–60 px, margins 8–10% of the width, about 8 words a line. Portrait
  (1080×1920) is watched on a phone: keep content in the middle 80%.
- ImGui windows size themselves over two passes (each frame is drawn twice, the second kept);
  do not rely on a window's size being known on the first pass.
- Reduce-motion is forced on for a render, so `imshell::Animate` jumps to its target: do the
  animation with the closed-form helpers above, not stateful ones.
- Do not read files, allocate heavily or log inside `draw`: it runs for every frame. Load images
  and fonts in `setup`.

## Sound

`Composition::audio` is a list of `AudioClip` (path, `start` seconds on the video's timeline,
`offset` into the file, `duration`, `volume`, `fadeIn`, `fadeOut`); the mixer sums them to
48 kHz stereo and clamps. Time effects from the constants that drive the motion. Check the placement
with `timeline` and `audio at 3.1s`, the levels with `audio analyze` (aim for about -14 LUFS
integrated for web video, true peak below -1 dBTP, no clipping, no silence you did not plan), and
listen to the mix without rendering the video with `audio render -o mix.wav`. Windows and Apple
encode natively; Linux, and `--ffmpeg` anywhere, need `ffmpeg` on `PATH`.

## Finish

1. `timeline` matches the plan; `strip all` reads; key frames were checked at full size.
2. `inspect` has no unexplained errors (entrance and exit warnings are understood), `verify` passes.
3. `audio analyze` shows sensible levels. Nothing from the "No slop" list is left in any strip.
4. A `render` finished; confirm with
   `ffprobe -v error -show_entries stream=codec_type,width,height,nb_frames,duration out.mp4`.
5. Give the user the output path and the duration.

## Not available yet

Video footage as a layer, captions, audio visualizers, shaders and transparent output (alpha).
If a request needs one of these, say so and give the closest result that works instead of
reaching for another tool.
