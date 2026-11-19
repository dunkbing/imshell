#pragma once

// the command line every video gets: `main` hands argv over and returns the exit code. it is the
// loop for a person or an agent that cannot watch: timeline, frame (png, crop, zoom), strip (a
// labelled contact sheet), render (a range, a draft at half size), preview at a time, verify.
// output goes to stdout, progress to stderr, --json makes stdout one json object
#include "imshell/video.hpp"

namespace imshell::video {
    // runs one command of the table in docs/video.md (`timeline`, `frame`, `strip`, `render`,
    // `preview`, `verify`, `help`); 0 on success, 1 on a usage error or a failure, 2 when `verify` finds a difference
    int Cli(const Composition& comp, int argc, char** argv, const char* name = "video");

    // time words as the command line takes them: a frame number (`120`), `3.2s`, `500ms`, `1:05.5`,
    // `50%`, `start`, `end`. false when `text` is none of them; the frame is clamped to the composition
    bool ParseTime(const Composition& comp, const std::string& text, int& frame);
} // namespace imshell::video
