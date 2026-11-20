#include "imshell/video_footage.hpp"

#include "ffmpeg.hpp"
#include "imgui_internal.h" // RegisterUserTexture
#include "imshell/image.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <vector>

namespace imshell::video {
    struct Footage::Impl {
        std::string path, ffmpeg, error;
        int fps = 30;
        int width = 0, height = 0;
        int count = 0; // frames at `fps`
        double seconds = 0.0;
        bool ok = false;

        std::mutex mutex; // a preview's worker and the app may both ask
        FfmpegReader reader;
        bool reading = false;
        int next = 0;   // the frame the reader gives next
        int shown = -1; // the frame in `pixels`
        std::vector<unsigned char> pixels;

        // one texture per imgui context (each renderer has its own), with the frame it holds
        struct Entry {
            std::unique_ptr<ImTextureData> texture;
            int frame = -1;
        };
        std::map<ImGuiContext*, Entry> textures;

        size_t bytes() const { return static_cast<size_t>(width) * static_cast<size_t>(height) * 4; }

        // frame `n` into `pixels`: read on when it is next (or a few ahead), restart the decoder otherwise
        bool load(int n) {
            n = std::clamp(n, 0, std::max(count - 1, 0));
            if (n == shown) {
                return true;
            }
            const int skip = n - next;
            if (!reading || skip < 0 || skip > 30) {
                if (!reader.open(ffmpeg, path, static_cast<double>(n) / fps, fps, width, height)) {
                    return false;
                }
                reading = true;
                next = n;
            }
            std::vector<unsigned char> frame(bytes());
            while (next <= n) {
                if (!reader.read(frame.data(), frame.size())) { // the end of the file came early: hold the last frame
                    reader.close();
                    reading = false;
                    if (shown < 0) {
                        return false;
                    }
                    count = std::min(count, shown + 1); // the file has fewer frames than its length said
                    return true;
                }
                ++next;
            }
            pixels = std::move(frame);
            shown = n;
            return true;
        }
    };

    namespace {
        bool isStill(const std::string& path) {
            const size_t dot = path.find_last_of('.');
            if (dot == std::string::npos) {
                return false;
            }
            std::string ext = path.substr(dot + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" || ext == "tga";
        }
    } // namespace

    Footage::Footage(std::string path, int fps, int maxWidth, std::string ffmpeg) : impl_(std::make_unique<Impl>()) {
        Impl& d = *impl_;
        d.path = std::move(path);
        d.ffmpeg = std::move(ffmpeg);
        d.fps = std::max(fps, 1);
        if (isStill(d.path)) { // a picture: decoded here once, no ffmpeg
            std::ifstream file(d.path, std::ios::binary);
            const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            const ImTextureID decoded = LoadTexture(
                bytes.data(), bytes.size(),
                [&d](const uint8_t* rgba, int w, int h) {
                    d.width = w;
                    d.height = h;
                    d.pixels.assign(rgba, rgba + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
                    return ImTextureID{1};
                });
            if (decoded == ImTextureID{}) {
                d.error = "could not read a picture from " + d.path;
                return;
            }
            d.count = 1;
            d.shown = 0; // load() has nothing to do
            d.ok = true;
            return;
        }
        const VideoInfo info = ProbeVideo(d.ffmpeg, d.path);
        if (!info.ok) {
            d.error = "could not read a video from " + d.path + " (is ffmpeg installed, and is it a video?)";
            return;
        }
        d.width = info.width;
        d.height = info.height;
        if (maxWidth > 0 && d.width > maxWidth) {
            d.height = std::max(2, static_cast<int>(std::lround(static_cast<double>(d.height) * maxWidth / d.width / 2.0)) * 2);
            d.width = maxWidth & ~1;
        }
        d.seconds = info.seconds;
        d.count = std::max(1, static_cast<int>(std::floor(d.seconds * d.fps + 1e-6)));
        d.ok = true;
    }

    Footage::~Footage() = default;

    bool Footage::ok() const { return impl_->ok; }
    int Footage::width() const { return impl_->width; }
    int Footage::height() const { return impl_->height; }
    double Footage::seconds() const { return impl_->seconds; }
    const std::string& Footage::error() const { return impl_->error; }

    const unsigned char* Footage::pixels(double t) {
        Impl& d = *impl_;
        if (!d.ok) {
            return nullptr;
        }
        std::lock_guard<std::mutex> lock(d.mutex);
        const int n = static_cast<int>(std::floor(std::max(t, 0.0) * d.fps + 1e-6));
        return d.load(n) ? d.pixels.data() : nullptr;
    }

    void Footage::Draw(ImDrawList* dl, double t, ImVec2 pos, ImVec2 size, Fit fit, ImU32 tint, float rounding, ImVec4 crop) {
        Impl& d = *impl_;
        const unsigned char* rgba = pixels(t);
        if (!rgba || size.x <= 0.f || size.y <= 0.f) {
            return;
        }
        std::lock_guard<std::mutex> lock(d.mutex);
        ImGuiContext* context = ImGui::GetCurrentContext();
        Impl::Entry& entry = d.textures[context];
        // a context made after another one was destroyed can sit at the same address: ask it whether it has the texture
        if (entry.texture && context->UserTextures.find(entry.texture.get()) == context->UserTextures.end()) {
            entry = Impl::Entry{};
        }
        if (!entry.texture) {
            entry.texture = std::make_unique<ImTextureData>();
            entry.texture->Create(ImTextureFormat_RGBA32, d.width, d.height);
            std::memcpy(entry.texture->GetPixels(), rgba, d.bytes());
            ImGui::RegisterUserTexture(entry.texture.get());
            entry.frame = d.shown;
        } else if (entry.frame != d.shown) {
            std::memcpy(entry.texture->GetPixels(), rgba, d.bytes());
            entry.texture->UpdateRect = {0, 0, static_cast<unsigned short>(d.width), static_cast<unsigned short>(d.height)};
            entry.texture->Updates.resize(0); // the backends upload from Updates, not UpdateRect
            entry.texture->Updates.push_back(entry.texture->UpdateRect);
            if (entry.texture->Status == ImTextureStatus_OK) { // one the backend has not made yet is made with these pixels
                entry.texture->SetStatus(ImTextureStatus_WantUpdates);
            }
            entry.frame = d.shown;
        }

        crop.x = std::clamp(crop.x, 0.f, 1.f);
        crop.y = std::clamp(crop.y, 0.f, 1.f);
        crop.z = std::clamp(crop.z, crop.x, 1.f);
        crop.w = std::clamp(crop.w, crop.y, 1.f);
        if (crop.z - crop.x <= 0.f || crop.w - crop.y <= 0.f) {
            return;
        }
        ImVec2 a = pos, b(pos.x + size.x, pos.y + size.y), uv0(0.f, 0.f), uv1(1.f, 1.f); // within the crop, then mapped into it
        const float boxAspect = size.x / size.y;
        const float picAspect = static_cast<float>(d.width) * (crop.z - crop.x) / (static_cast<float>(d.height) * (crop.w - crop.y));
        if (fit == Fit::Contain) {
            const float w = boxAspect > picAspect ? size.y * picAspect : size.x, h = boxAspect > picAspect ? size.y : size.x / picAspect;
            a = ImVec2(pos.x + (size.x - w) * 0.5f, pos.y + (size.y - h) * 0.5f);
            b = ImVec2(a.x + w, a.y + h);
        } else if (fit == Fit::Cover) { // crop the picture instead
            if (boxAspect > picAspect) {
                const float keep = picAspect / boxAspect;
                uv0.y = (1.f - keep) * 0.5f;
                uv1.y = uv0.y + keep;
            } else {
                const float keep = boxAspect / picAspect;
                uv0.x = (1.f - keep) * 0.5f;
                uv1.x = uv0.x + keep;
            }
        }
        const ImVec2 cropped0(crop.x + uv0.x * (crop.z - crop.x), crop.y + uv0.y * (crop.w - crop.y));
        const ImVec2 cropped1(crop.x + uv1.x * (crop.z - crop.x), crop.y + uv1.y * (crop.w - crop.y));
        ImColor col(tint);
        col.Value.w *= ImGui::GetStyle().Alpha;
        dl->AddImageRounded(entry.texture->GetTexRef(), a, b, cropped0, cropped1, static_cast<ImU32>(col), rounding);
    }
} // namespace imshell::video
