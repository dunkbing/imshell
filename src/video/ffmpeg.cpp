#include "ffmpeg.hpp"

#include <signal.h>
#include <string>

#ifdef _WIN32
#define IMSHELL_POPEN _popen
#define IMSHELL_PCLOSE _pclose
#else
#define IMSHELL_POPEN popen
#define IMSHELL_PCLOSE pclose
#endif

namespace imshell::video {
    namespace {
        // one argument for the shell: single quotes, with any in the text closed and escaped
        std::string quote(const std::string& s) {
#ifdef _WIN32
            return "\"" + s + "\"";
#else
            std::string out = "'";
            for (char c : s) {
                out += c == '\'' ? "'\\''" : std::string(1, c);
            }
            return out + "'";
#endif
        }
    } // namespace

    bool FfmpegPipe::open(const std::string& ffmpeg, int width, int height, int fps, const std::string& path, const std::string& audioFile,
                          std::string* error) {
        close();
#ifndef _WIN32
        signal(SIGPIPE, SIG_IGN); // a dead ffmpeg is a failed write, not a dead app
#endif
        std::string command = quote(ffmpeg) + " -v error -y -f rawvideo -pix_fmt rgba -s " + std::to_string(width) + "x" +
                              std::to_string(height) + " -r " + std::to_string(fps) + " -i -";
        if (!audioFile.empty()) {
            command += " -f f32le -ar 48000 -ac 2 -i " + quote(audioFile) + " -map 0:v -map 1:a -c:a aac -b:a 160k";
        } else {
            command += " -an";
        }
        command += " -c:v libx264 -pix_fmt yuv420p -movflags +faststart -shortest " + quote(path);
        pipe_ = IMSHELL_POPEN(command.c_str(), "w");
        if (!pipe_) {
            if (error) {
                *error = "could not start " + ffmpeg;
            }
            return false;
        }
        return true;
    }

    bool FfmpegPipe::write(const unsigned char* rgba, size_t size) {
        return pipe_ && std::fwrite(rgba, 1, size, pipe_) == size;
    }

    bool FfmpegPipe::close() {
        if (!pipe_) {
            return true;
        }
        const int status = IMSHELL_PCLOSE(pipe_);
        pipe_ = nullptr;
        return status == 0;
    }
} // namespace imshell::video
