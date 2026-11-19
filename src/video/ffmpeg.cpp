#include "ffmpeg.hpp"

#include <signal.h>
#include <string>

#ifdef _WIN32
#define IMSHELL_POPEN _popen
#define IMSHELL_WRITE_MODE "wb" // text mode would turn every 0x0a in the frames into 0x0d 0x0a
#define IMSHELL_READ_MODE "rb"
#define IMSHELL_PCLOSE _pclose
#else
#define IMSHELL_POPEN popen
#define IMSHELL_WRITE_MODE "w"
#define IMSHELL_READ_MODE "r"
#define IMSHELL_PCLOSE pclose
#endif

namespace imshell::video {
    // one argument for the shell: single quotes, with any in the text closed and escaped
    std::string ShellQuote(const std::string& s) {
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

    // cmd.exe drops the first and last quote of a line that starts with one, which would unquote a
    // quoted ffmpeg path: wrap the whole line once more
    static std::string ShellCommand(const std::string& command) {
#ifdef _WIN32
        return "\"" + command + "\"";
#else
        return command;
#endif
    }

    bool FfmpegPipe::open(const Options& options, int width, int height, int fps, const std::string& path, const std::string& audioFile,
                          std::string* error) {
        close();
#ifndef _WIN32
        signal(SIGPIPE, SIG_IGN); // a dead ffmpeg is a failed write, not a dead app
#endif
        const std::string& ffmpeg = options.ffmpeg;
        const std::string& codec = options.codec;
        const bool vp9 = codec == "vp9", prores = codec == "prores";
        std::string command = ShellQuote(ffmpeg) + " -v error -y -f rawvideo -pix_fmt rgba -s " + std::to_string(width) + "x" +
                              std::to_string(height) + " -r " + std::to_string(fps) + " -i -";
        if (!audioFile.empty()) {
            command += " -f f32le -ar 48000 -ac 2 -i " + ShellQuote(audioFile) + " -map 0:v -map 1:a " + (vp9 ? "-c:a libopus -b:a 128k" : "-c:a aac -b:a 160k");
        } else {
            command += " -an";
        }
        if (prores) { // an intermediate for editors: 10 bit 4:2:2, high quality
            command += " -c:v prores_ks -profile:v 3 -pix_fmt yuv422p10le";
        } else if (vp9) {
            command += " -c:v libvpx-vp9 -pix_fmt yuv420p";
        } else if (codec == "hevc") {
            command += " -c:v libx265 -pix_fmt yuv420p -tag:v hvc1 -movflags +faststart";
        } else {
            command += " -c:v libx264 -pix_fmt yuv420p -movflags +faststart";
        }
        if (!prores) {
            if (options.crf >= 0) {
                command += " -crf " + std::to_string(options.crf) + (vp9 && options.bitrate <= 0 ? " -b:v 0" : ""); // vp9 takes constant quality only with no bitrate
            }
            if (options.bitrate > 0) {
                command += " -b:v " + std::to_string(options.bitrate) + "k";
            }
        }
        command += " -shortest " + ShellQuote(path);
        pipe_ = IMSHELL_POPEN(ShellCommand(command).c_str(), IMSHELL_WRITE_MODE);
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

    bool DecodeWithFfmpeg(const std::string& ffmpeg, const std::string& path, std::vector<float>& pcm, std::string* error) {
        const std::string command = ShellQuote(ffmpeg) + " -v error -i " + ShellQuote(path) + " -vn -f f32le -ac 2 -ar 48000 -";
        FILE* pipe = IMSHELL_POPEN(ShellCommand(command).c_str(), IMSHELL_READ_MODE);
        if (!pipe) {
            if (error) {
                *error = "could not start " + ffmpeg;
            }
            return false;
        }
        pcm.clear();
        float chunk[4096];
        for (size_t n; (n = std::fread(chunk, sizeof(float), 4096, pipe)) > 0;) {
            pcm.insert(pcm.end(), chunk, chunk + n);
        }
        const bool ok = IMSHELL_PCLOSE(pipe) == 0 && pcm.size() >= 2;
        if (!ok && error) {
            *error = "ffmpeg could not decode " + path;
        }
        pcm.resize(pcm.size() / 2 * 2); // whole frames
        return ok;
    }
} // namespace imshell::video
