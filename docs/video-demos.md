# Video demos

Each of these was made with imshell's video tool (the first from C++, the rest as `imshell-video` scripts); the source is in [`examples/video/script/`](https://github.com/dunkbing/imshell/tree/main/examples/video/script) next to the name under the video.
How to make one is in [Video](video.md).

## Introducing imshell

The components gallery, drawn by imshell's own widgets in four themes, a typed form and the code editor, the native symbols and controls, measured speed, the platforms and the video tool itself. It is a C++ composition, `examples/intro/main.cpp`, over the same gallery code the website demo runs.

<video controls preload="metadata" width="100%" src="/video-demos/imshell-intro.mp4"></video>

## imshell-video intro

A video is a function: a script, then `strip`, `inspect`, `verify` and `render`. Script: `demo-intro.js`.

<video controls preload="metadata" width="100%" src="/video-demos/imshell-video-intro.mp4"></video>

## Real widgets

ImGui windows, checkboxes and progress bars drawn by a script. Script: `demo-widgets.js`.

<video controls preload="metadata" width="100%" src="/video-demos/imshell-video-widgets.mp4"></video>

## Post effects

Glow, chromatic aberration, grain and a vignette over the finished frame. Script: `demo-neon.js`.

<video controls preload="metadata" width="100%" src="/video-demos/imshell-video-neon.mp4"></video>

## A screen recording with a camera

A real recording of an app, an automatic zoom on clicks, a drawn cursor with click ripples and a stopwatch, at 60 fps. Script: `dearsql-screen.js`, with the recorded mouse in `dearsql-screen.track.js`.

<video controls preload="metadata" width="100%" src="/video-demos/dearsql-screen.mp4"></video>

## Neon, with real screenshots

Screenshots in glowing frames with post effects. Script: `dearsql-neon.js`.

<video controls preload="metadata" width="100%" src="/video-demos/dearsql-neon.mp4"></video>

## One question, start to finish

A typed prompt and a camera over real screens. Script: `dearsql-question.js`.

<video controls preload="metadata" width="100%" src="/video-demos/dearsql-question.mp4"></video>
