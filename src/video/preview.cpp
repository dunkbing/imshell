// linux and windows: a window that plays a composition. the frames come from the same Rasterizer a
// render uses and are shown through a texture imgui itself keeps up to date (ImTextureData), so no
// platform texture code is needed. the rasterizer runs on its own thread: on linux its GL context
// must not replace the one the window draws with
#include "imgui.h"
#include "imgui_internal.h" // RegisterUserTexture
#include "imshell/app.hpp"
#include "imshell/video.hpp"
#include "rasterizer.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>

namespace imshell::video {
    namespace {
        // runs one job at a time on a thread of its own, and waits for it
        class Worker {
        public:
            Worker() : thread_([this] { loop(); }) {}

            ~Worker() {
                {
                    std::lock_guard lock(mutex_);
                    quit_ = true;
                }
                cv_.notify_all();
                thread_.join();
            }

            void run(std::function<void()> job) {
                std::unique_lock lock(mutex_);
                job_ = std::move(job);
                cv_.notify_all();
                cv_.wait(lock, [this] { return !job_; });
            }

        private:
            void loop() {
                std::unique_lock lock(mutex_);
                while (true) {
                    cv_.wait(lock, [this] { return job_ || quit_; });
                    if (!job_) {
                        return;
                    }
                    job_(); // the lock is held: run() is waiting and nothing else posts
                    job_ = nullptr;
                    cv_.notify_all();
                }
            }

            std::mutex mutex_;
            std::condition_variable cv_;
            std::function<void()> job_;
            bool quit_ = false;
            std::thread thread_;
        };

        class PreviewDelegate : public imshell::Delegate {
        public:
            PreviewDelegate(const Composition& comp, const PreviewOptions& options) : comp_(comp), options_(options) {
                frame_ = std::clamp(options.startFrame, 0, comp.frames - 1);
                playing_ = options.play;
                loop_ = options.loop;
            }

            void setup() override {
                // the rasterizer makes (and sets) an imgui context of its own: hand the app's back after
                ImGuiContext* app = ImGui::GetCurrentContext();
                worker_.run([this] { rasterizer_ = MakeRasterizer(comp_, &error_); });
                ImGui::SetCurrentContext(app);
                if (!rasterizer_) {
                    std::fprintf(stderr, "imshell::video::Preview: %s\n", error_.c_str());
                }
                texture_.Create(ImTextureFormat_RGBA32, comp_.width, comp_.height);
                std::memset(texture_.GetPixels(), 0, texture_.GetSizeInBytes());
                ImGui::RegisterUserTexture(&texture_);
                registered_ = true;
                start_ = std::chrono::steady_clock::now();
                started_ = start_;
                baseFrame_ = frame_;
            }

            ~PreviewDelegate() override { release(); }

            bool wantsFrames() override { return playing_; }

            bool shouldQuit() override {
                const bool done = options_.closeAfter > 0.0 && seconds(started_) >= options_.closeAfter;
                if (done) {
                    release();
                }
                return done;
            }

            bool closeRequested() override {
                release();
                return true;
            }

            void frame() override {
                if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
                    togglePlay();
                }
                if (playing_) {
                    const int total = comp_.frames;
                    const int at = baseFrame_ + static_cast<int>(seconds(start_) * comp_.fps);
                    if (at >= total && !loop_) {
                        playing_ = false;
                        frame_ = total - 1;
                    } else {
                        frame_ = at % total;
                    }
                }
                if (rasterizer_ && (shown_ != frame_ || dirty_)) {
                    show(frame_);
                }

                const ImGuiViewport* viewport = ImGui::GetMainViewport();
                ImGui::SetNextWindowPos(viewport->WorkPos);
                ImGui::SetNextWindowSize(viewport->WorkSize);
                ImGui::Begin("##preview", nullptr,
                             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
                const float bar = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y;
                const ImVec2 area(ImGui::GetContentRegionAvail().x, std::max(ImGui::GetContentRegionAvail().y - bar, 1.f));
                const float scale = std::min(area.x / comp_.width, area.y / comp_.height);
                const ImVec2 size(comp_.width * scale, comp_.height * scale);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (area.x - size.x) * 0.5f);
                ImGui::Image(texture_.GetTexRef(), size);

                ImGui::SetCursorPosY(ImGui::GetWindowHeight() - bar + ImGui::GetStyle().WindowPadding.y * 0.5f);
                if (ImGui::Button(playing_ ? "Pause" : "Play")) {
                    togglePlay();
                }
                ImGui::SameLine();
                ImGui::Checkbox("Loop", &loop_);
                ImGui::SameLine();
                ImGui::Text("%d / %d  %.2fs", frame_ + 1, comp_.frames, static_cast<double>(frame_) / comp_.fps);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-FLT_MIN);
                int scrub = frame_;
                if (ImGui::SliderInt("##frame", &scrub, 0, comp_.frames - 1, "")) {
                    frame_ = scrub;
                    baseFrame_ = scrub;
                    start_ = std::chrono::steady_clock::now();
                }
                ImGui::End();
            }

        private:
            static double seconds(std::chrono::steady_clock::time_point since) {
                return std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count();
            }

            void togglePlay() {
                playing_ = !playing_;
                if (playing_) { // play on from where it stands
                    if (frame_ >= comp_.frames - 1) {
                        frame_ = 0;
                    }
                    baseFrame_ = frame_;
                    start_ = std::chrono::steady_clock::now();
                }
            }

            // draws frame n into the texture; the backend uploads it with this frame's draw data
            void show(int n) {
                dirty_ = false;
                ImGuiContext* app = ImGui::GetCurrentContext();
                bool ok = false;
                worker_.run([&] { ok = rasterizer_->render(n, false, 0.f, rgba_); });
                ImGui::SetCurrentContext(app);
                if (!ok || rgba_.size() != static_cast<size_t>(texture_.GetSizeInBytes())) {
                    return;
                }
                std::memcpy(texture_.GetPixels(), rgba_.data(), rgba_.size());
                texture_.UpdateRect = {0, 0, static_cast<unsigned short>(comp_.width), static_cast<unsigned short>(comp_.height)};
                if (texture_.Status == ImTextureStatus_OK) { // a texture the backend has not made yet is created with these pixels
                    texture_.SetStatus(ImTextureStatus_WantUpdates);
                }
                shown_ = n;
            }

            // the rasterizer goes before the app destroys its context; the texture is unregistered once
            void release() {
                if (rasterizer_) {
                    ImGuiContext* app = ImGui::GetCurrentContext();
                    worker_.run([this] { rasterizer_.reset(); });
                    ImGui::SetCurrentContext(app);
                }
                if (registered_) {
                    ImGui::UnregisterUserTexture(&texture_);
                    registered_ = false;
                }
            }

            Composition comp_;
            PreviewOptions options_;
            Worker worker_;
            std::unique_ptr<Rasterizer> rasterizer_;
            std::string error_;
            std::vector<unsigned char> rgba_;
            ImTextureData texture_;
            bool registered_ = false;
            std::chrono::steady_clock::time_point start_, started_;
            int baseFrame_ = 0;
            int frame_ = 0;
            int shown_ = -1;
            bool dirty_ = true;
            bool playing_ = true;
            bool loop_ = true;
        };
    } // namespace

    void Preview(const Composition& comp, const PreviewOptions& options) {
        if (comp.width <= 0 || comp.height <= 0 || comp.fps <= 0 || comp.frames <= 0) {
            return;
        }
        imshell::Config config;
        config.title = "Preview";
        const float toolbar = 56.f;
        const float fit = std::min({1.f, 1200.f / comp.width, 760.f / comp.height});
        config.width = std::max(480, static_cast<int>(comp.width * fit));
        config.height = static_cast<int>(comp.height * fit + toolbar);
        PreviewDelegate delegate(comp, options);
        imshell::Run(config, delegate);
    }
} // namespace imshell::video
