#include "ffmpeg.hpp"

#include <signal.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
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

    bool FfmpegPipe::open(const Options& options, int width, int height, int fps, const std::string& path, const std::string& audioFile, bool alpha,
                          std::string* error) {
        close();
#ifndef _WIN32
        signal(SIGPIPE, SIG_IGN); // a dead ffmpeg is a failed write, not a dead app
#endif
        const std::string& ffmpeg = options.ffmpeg;
        const std::string& codec = options.codec;
        const bool vp9 = codec == "vp9", prores = codec == "prores";
        if (alpha && !vp9 && !prores) {
            if (error) {
                *error = "h264 and hevc cannot hold transparency here: use prores (.mov), vp9 (.webm) or --images";
            }
            return false;
        }
        std::string command = ShellQuote(ffmpeg) + " -v error -y -f rawvideo -pix_fmt rgba -s " + std::to_string(width) + "x" +
                              std::to_string(height) + " -r " + std::to_string(fps) + " -i -";
        if (!audioFile.empty()) {
            command += " -f f32le -ar 48000 -ac 2 -i " + ShellQuote(audioFile) + " -map 0:v -map 1:a " + (vp9 ? "-c:a libopus -b:a 128k" : "-c:a aac -b:a 160k");
        } else {
            command += " -an";
        }
        if (prores) { // an intermediate for editors: 10 bit 4:2:2, high quality
            command += alpha ? " -c:v prores_ks -profile:v 4 -pix_fmt yuva444p10le" : " -c:v prores_ks -profile:v 3 -pix_fmt yuv422p10le";
        } else if (vp9) {
            command += alpha ? " -c:v libvpx-vp9 -pix_fmt yuva420p -auto-alt-ref 0" : " -c:v libvpx-vp9 -pix_fmt yuv420p";
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

namespace imshell::video {
    VideoInfo ProbeVideo(const std::string& ffmpeg, const std::string& path) {
        VideoInfo info;
        FILE* pipe = IMSHELL_POPEN(ShellCommand(ShellQuote(ffmpeg) + " -hide_banner -i " + ShellQuote(path) + " 2>&1").c_str(), IMSHELL_READ_MODE);
        if (!pipe) {
            return info;
        }
        std::string text;
        char chunk[4096];
        for (size_t n; (n = std::fread(chunk, 1, sizeof chunk, pipe)) > 0;) {
            text.append(chunk, n);
        }
        IMSHELL_PCLOSE(pipe); // ffmpeg exits with an error when given no output: only the text matters

        const size_t dur = text.find("Duration: ");
        if (dur != std::string::npos) {
            int h = 0, m = 0;
            double s = 0.0;
            if (std::sscanf(text.c_str() + dur + 10, "%d:%d:%lf", &h, &m, &s) == 3) {
                info.seconds = h * 3600.0 + m * 60.0 + s;
            }
        }
        const size_t video = text.find("Video: ");
        if (video == std::string::npos) {
            return info;
        }
        const size_t end = text.find('\n', video);
        const std::string line = text.substr(video, end == std::string::npos ? std::string::npos : end - video);
        for (size_t i = 0; i + 3 < line.size(); ++i) { // the "1280x720" token: not the codec tag's 0x31637661
            int w = 0, h = 0, used = 0;
            if (line[i] == ' ' && std::sscanf(line.c_str() + i + 1, "%dx%d%n", &w, &h, &used) == 2 && w >= 2 && h >= 2 && w <= 16384 && h <= 16384) {
                info.width = w;
                info.height = h;
                break;
            }
        }
        const size_t fpsAt = line.find(" fps");
        if (fpsAt != std::string::npos) {
            size_t from = fpsAt;
            while (from > 0 && (std::isdigit(static_cast<unsigned char>(line[from - 1])) || line[from - 1] == '.')) {
                --from;
            }
            info.fps = std::atof(line.substr(from, fpsAt - from).c_str());
        }
        const size_t rot = text.find("rotation of ", video);
        if (rot != std::string::npos && std::abs(std::atof(text.c_str() + rot + 12)) > 45.0 && std::abs(std::atof(text.c_str() + rot + 12)) < 135.0) {
            std::swap(info.width, info.height); // ffmpeg turns the picture upright when it decodes
        }
        info.ok = info.width > 0 && info.height > 0;
        return info;
    }

    bool FfmpegReader::open(const std::string& ffmpeg, const std::string& path, double start, int fps, int width, int height) {
        close();
        char at[32];
        std::snprintf(at, sizeof at, "%.6f", std::max(start, 0.0));
        const std::string command = ShellQuote(ffmpeg) + " -v error -ss " + at + " -i " + ShellQuote(path) + " -an -vf fps=" + std::to_string(fps) + ",scale=" + std::to_string(width) + ":" +
                                    std::to_string(height) + " -f rawvideo -pix_fmt rgba -";
        pipe_ = IMSHELL_POPEN(ShellCommand(command).c_str(), IMSHELL_READ_MODE);
        return pipe_ != nullptr;
    }

    bool FfmpegReader::read(unsigned char* rgba, size_t size) {
        return pipe_ && std::fread(rgba, 1, size, pipe_) == size;
    }

    void FfmpegReader::close() {
        if (pipe_) {
            IMSHELL_PCLOSE(pipe_);
            pipe_ = nullptr;
        }
    }
} // namespace imshell::video
