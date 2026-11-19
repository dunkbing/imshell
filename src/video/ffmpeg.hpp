#pragma once

// raw rgba frames into an ffmpeg process over a pipe. no link dependency: the binary is found at run time
#include "imshell/video.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace imshell::video {
    // one argument for the shell, quoted
    std::string ShellQuote(const std::string& s);
    // a sound file decoded by ffmpeg to interleaved 48 kHz stereo float; false and `error` on failure
    bool DecodeWithFfmpeg(const std::string& ffmpeg, const std::string& path, std::vector<float>& pcm, std::string* error);

    // what `ffmpeg -i` says about a video file (read from its stderr; no ffprobe needed)
    struct VideoInfo {
        bool ok = false;
        int width = 0;
        int height = 0;
        double fps = 0.0;
        double seconds = 0.0;
    };
    VideoInfo ProbeVideo(const std::string& ffmpeg, const std::string& path);

    // raw rgba frames out of an ffmpeg process: the file from `start` seconds at `fps`, scaled to width x height
    class FfmpegReader {
    public:
        FfmpegReader() = default;
        FfmpegReader(const FfmpegReader&) = delete;
        FfmpegReader& operator=(const FfmpegReader&) = delete;
        ~FfmpegReader() { close(); }
        bool open(const std::string& ffmpeg, const std::string& path, double start, int fps, int width, int height);
        // one frame, width * height * 4 bytes; false at the end of the file
        bool read(unsigned char* rgba, size_t size);
        void close();

    private:
        FILE* pipe_ = nullptr;
    };

    class FfmpegPipe {
    public:
        FfmpegPipe() = default;
        FfmpegPipe(const FfmpegPipe&) = delete;
        FfmpegPipe& operator=(const FfmpegPipe&) = delete;
        ~FfmpegPipe() { close(); }

        // starts `ffmpeg` writing `path` with the codec, bitrate and quality of `options` (h.264 by default, yuv420p, so every player opens it)
        // `audioFile`: raw 48 kHz stereo f32le to add as an aac track, or empty for none
        bool open(const Options& options, int width, int height, int fps, const std::string& path, const std::string& audioFile, bool alpha, std::string* error);
        // one frame, width * height * 4 bytes
        bool write(const unsigned char* rgba, size_t size);
        // ends the input and waits for ffmpeg; false when it failed
        bool close();

    private:
        FILE* pipe_ = nullptr;
    };
} // namespace imshell::video
