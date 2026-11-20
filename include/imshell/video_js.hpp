#pragma once

// a composition written as a script: `imshell-video scene.js <command>` runs the same command line as Cli()
// (timeline, frame, strip, inspect, verify, render, preview, bench) on a scene that is JavaScript, so a
// video needs no compiler. the script calls video({...}) once; see `imshell-video api` for what it can draw
namespace imshell::video {
    // argv[1] is the script, the rest is Cli's; 0 on success
    int RunScript(int argc, char** argv);
} // namespace imshell::video
