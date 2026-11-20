// video on metal: imgui draws into a pixel buffer's texture (an iosurface), which goes to
// AVAssetWriter untouched, or is copied out for RenderFrame
#import <AVFoundation/AVFoundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreVideo/CoreVideo.h>
#import <ImageIO/ImageIO.h>
#import <Metal/Metal.h>
#include "imshell/animation.hpp"
#include "imshell/app.hpp"
#include "imshell/themes.hpp"
#include "imshell/video.hpp"
#include "imgui.h"
#include "imgui_impl_metal.h"
#include "audio.hpp"
#include "ffmpeg.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <unistd.h>

namespace imshell::video {
namespace {
    NSDictionary* bufferAttributes(int w, int h) {
        return @{
            (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
            (id)kCVPixelBufferWidthKey : @(w),
            (id)kCVPixelBufferHeightKey : @(h),
            (id)kCVPixelBufferMetalCompatibilityKey : @YES,
            (id)kCVPixelBufferIOSurfacePropertiesKey : @{},
        };
    }

    // owns the metal objects and a private imgui context; the caller's context comes back on destruction
    class Renderer {
    public:
        explicit Renderer(const Composition& comp) : comp_(comp) {
            device_ = MTLCreateSystemDefaultDevice();
            queue_ = [device_ newCommandQueue];
            if (!queue_ || CVMetalTextureCacheCreate(nullptr, nullptr, device_, nullptr, &cache_) != kCVReturnSuccess) {
                return;
            }
            previous_ = ImGui::GetCurrentContext();
            context_ = ImGui::CreateContext();
            ImGui::SetCurrentContext(context_);
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.LogFilename = nullptr;
            io.DisplaySize = ImVec2(static_cast<float>(comp.width), static_cast<float>(comp.height));
            io.DisplayFramebufferScale = ImVec2(1.f, 1.f);
            ImGui_ImplMetal_Init(device_);
            imshell::ApplyTheme(); // the theme is process wide, the style is per context
            imshell::SetReduceMotion(true); // Animate jumps to its target: no value depends on the frames before
            if (comp.setup) {
                comp.setup();
            }
            ok_ = true;
        }

        ~Renderer() {
            if (context_) {
                ImGui::SetCurrentContext(context_);
                ImGui_ImplMetal_Shutdown();
                ImGui::DestroyContext(context_);
                imshell::SetReduceMotion(std::nullopt);
                ImGui::SetCurrentContext(previous_);
            }
            if (cache_) {
                CFRelease(cache_);
            }
        }

        bool ok() const { return ok_; }

        // draws frame `index` into the buffer (bgra, opaque black behind the composition)
        bool draw(int index, bool sequential, CVPixelBufferRef buffer, float offset = 0.f) {
            ImGuiContext* saved = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(context_);
            const bool done = drawFrame(index, sequential, buffer, offset);
            ImGui::SetCurrentContext(saved);
            return done;
        }

    private:
        bool drawFrame(int index, bool sequential, CVPixelBufferRef buffer, float offset) {
            CVMetalTextureRef cvTexture = nullptr;
            if (CVMetalTextureCacheCreateTextureFromImage(nullptr, cache_, buffer, nullptr, MTLPixelFormatBGRA8Unorm,
                                                          comp_.width, comp_.height, 0, &cvTexture) != kCVReturnSuccess) {
                return false;
            }
            id<MTLTexture> texture = CVMetalTextureGetTexture(cvTexture);
            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = texture;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, comp_.transparent ? 0 : 1);
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;

            const Frame frame{.index = index, .frames = comp_.frames, .fps = comp_.fps,
                              .width = comp_.width, .height = comp_.height, .sequential = sequential, .offset = offset};
            ImGui::GetIO().DeltaTime = 1.f / static_cast<float>(comp_.fps);
            // twice: windows size themselves on their second frame, so the first is thrown away
            for (int i = 0; i < 2; ++i) {
                ImGui_ImplMetal_NewFrame(pass);
                ImGui::NewFrame();
                if (comp_.draw) {
                    comp_.draw(frame);
                }
                if (i == 0) {
                    ImGui::EndFrame();
                } else {
                    ImGui::Render();
                    if (drawProbe()) {
                        drawProbe()(index, ImGui::GetDrawData());
                    }
                }
            }

            id<MTLCommandBuffer> commands = [queue_ commandBuffer];
            id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
            ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), commands, encoder);
            [encoder endEncoding];
            [commands commit];
            [commands waitUntilCompleted];
            CFRelease(cvTexture);
            return commands.status == MTLCommandBufferStatusCompleted;
        }

        const Composition& comp_;
        id<MTLDevice> device_ = nil;
        id<MTLCommandQueue> queue_ = nil;
        CVMetalTextureCacheRef cache_ = nullptr;
        ImGuiContext* context_ = nullptr;
        ImGuiContext* previous_ = nullptr;
        bool ok_ = false;
    };

    bool fail(std::string* error, const char* what) {
        if (error) {
            *error = what;
        }
        return false;
    }

    bool validSize(const Composition& comp, std::string* error) {
        if (comp.width <= 0 || comp.height <= 0 || comp.fps <= 0 || comp.frames <= 0) {
            return fail(error, "the composition needs a size, a rate and at least one frame");
        }
        return true;
    }

    // the buffer's pixels as rgba
    Image readImage(CVPixelBufferRef buffer, const Composition& comp) {
        const int width = comp.width, height = comp.height;
        Image image;
        CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
        const auto* src = static_cast<const unsigned char*>(CVPixelBufferGetBaseAddress(buffer));
        const size_t stride = CVPixelBufferGetBytesPerRow(buffer);
        image.width = width;
        image.height = height;
        image.rgba.resize(static_cast<size_t>(width) * height * 4);
        for (int y = 0; y < height; ++y) {
            const unsigned char* row = src + y * stride;
            unsigned char* out = &image.rgba[static_cast<size_t>(y) * width * 4];
            for (int x = 0; x < width; ++x) { // bgra to rgba
                out[x * 4 + 0] = row[x * 4 + 2];
                out[x * 4 + 1] = row[x * 4 + 1];
                out[x * 4 + 2] = row[x * 4 + 0];
                out[x * 4 + 3] = row[x * 4 + 3];
            }
        }
        CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
        if (comp.transparent) {
            Unpremultiply(image.rgba);
        }
        return image;
    }

    CVPixelBufferRef makeBuffer(const Composition& comp) {
        CVPixelBufferRef buffer = nullptr;
        CVPixelBufferCreate(nullptr, comp.width, comp.height, kCVPixelFormatType_32BGRA,
                            (__bridge CFDictionaryRef)bufferAttributes(comp.width, comp.height), &buffer);
        return buffer;
    }

    // draws a frame once, or as `blur.samples` draws spread over the shutter and averaged (in the
    // encoded values, not linear light) into the target buffer
    class Exposure {
    public:
        Exposure(const Composition& comp, const MotionBlur& blur) : comp_(comp), blur_(blur) {
            samples_ = std::max(blur.samples, 1);
            if (samples_ > 1) {
                scratch_ = makeBuffer(comp);
                sum_.resize(static_cast<size_t>(comp.width) * comp.height * 4);
            }
        }
        ~Exposure() {
            if (scratch_) {
                CVPixelBufferRelease(scratch_);
            }
        }

        bool ok() const { return samples_ == 1 || scratch_; }

        bool drawOnly(Renderer& renderer, int index, bool sequential, CVPixelBufferRef target) {
            if (samples_ == 1) {
                return renderer.draw(index, sequential, target);
            }
            std::fill(sum_.begin(), sum_.end(), 0.f);
            const size_t row = static_cast<size_t>(comp_.width) * 4;
            for (int s = 0; s < samples_; ++s) {
                const float offset = ((static_cast<float>(s) + 0.5f) / static_cast<float>(samples_) - 0.5f) * blur_.shutter;
                if (!renderer.draw(index, sequential, scratch_, offset)) {
                    return false;
                }
                CVPixelBufferLockBaseAddress(scratch_, kCVPixelBufferLock_ReadOnly);
                const auto* src = static_cast<const unsigned char*>(CVPixelBufferGetBaseAddress(scratch_));
                const size_t stride = CVPixelBufferGetBytesPerRow(scratch_);
                for (int y = 0; y < comp_.height; ++y) {
                    float* acc = &sum_[static_cast<size_t>(y) * row];
                    const unsigned char* line = src + y * stride;
                    for (size_t i = 0; i < row; ++i) {
                        acc[i] += line[i];
                    }
                }
                CVPixelBufferUnlockBaseAddress(scratch_, kCVPixelBufferLock_ReadOnly);
            }
            CVPixelBufferLockBaseAddress(target, 0);
            auto* dst = static_cast<unsigned char*>(CVPixelBufferGetBaseAddress(target));
            const size_t stride = CVPixelBufferGetBytesPerRow(target);
            const float scale = 1.f / static_cast<float>(samples_);
            for (int y = 0; y < comp_.height; ++y) {
                const float* acc = &sum_[static_cast<size_t>(y) * row];
                unsigned char* line = dst + y * stride;
                for (size_t i = 0; i < row; ++i) {
                    line[i] = static_cast<unsigned char>(acc[i] * scale + 0.5f);
                }
            }
            CVPixelBufferUnlockBaseAddress(target, 0);
            return true;
        }

        // the exposure, then Composition::post on the pixels in place (straight rgba for the filter, the buffer's own form after)
        bool draw(Renderer& renderer, int index, bool sequential, CVPixelBufferRef target) {
            if (!drawOnly(renderer, index, sequential, target)) {
                return false;
            }
            if (comp_.post) {
                Image image = readImage(target, comp_);
                ApplyPost(comp_, index, image);
                CVPixelBufferLockBaseAddress(target, 0);
                auto* dst = static_cast<unsigned char*>(CVPixelBufferGetBaseAddress(target));
                const size_t stride = CVPixelBufferGetBytesPerRow(target);
                for (int y = 0; y < comp_.height; ++y) {
                    const unsigned char* in = &image.rgba[static_cast<size_t>(y) * comp_.width * 4];
                    unsigned char* out = dst + y * stride;
                    for (int x = 0; x < comp_.width; ++x) { // rgba back to bgra, premultiplied again when the frame is transparent
                        const unsigned a = in[x * 4 + 3], k = comp_.transparent ? a : 255;
                        out[x * 4 + 0] = static_cast<unsigned char>((in[x * 4 + 2] * k + 127) / 255);
                        out[x * 4 + 1] = static_cast<unsigned char>((in[x * 4 + 1] * k + 127) / 255);
                        out[x * 4 + 2] = static_cast<unsigned char>((in[x * 4 + 0] * k + 127) / 255);
                        out[x * 4 + 3] = static_cast<unsigned char>(a);
                    }
                }
                CVPixelBufferUnlockBaseAddress(target, 0);
            }
            return true;
        }

    private:
        const Composition& comp_;
        MotionBlur blur_;
        int samples_ = 1;
        CVPixelBufferRef scratch_ = nullptr;
        std::vector<float> sum_;
    };

    // the frame range an Options asks for
    void frameRange(const Composition& comp, const Options& options, int& from, int& to) {
        from = std::clamp(options.from, 0, comp.frames - 1);
        to = options.to < 0 ? comp.frames : std::clamp(options.to, from + 1, comp.frames);
    }

    // a file decoded to 48 kHz stereo float by AVAssetReader (it converts rate, channels and codec)
    bool decodeClip(const std::string& path, Pcm& pcm, std::string* error) {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
        AVURLAsset* asset = [AVURLAsset URLAssetWithURL:url options:nil];
        __block NSArray<AVAssetTrack*>* tracks = nil;
        dispatch_semaphore_t loaded = dispatch_semaphore_create(0);
        [asset loadTracksWithMediaType:AVMediaTypeAudio
                     completionHandler:^(NSArray<AVAssetTrack*>* result, NSError*) {
                         tracks = result;
                         dispatch_semaphore_signal(loaded);
                     }];
        dispatch_semaphore_wait(loaded, DISPATCH_TIME_FOREVER);
        if (tracks.count == 0) {
            return fail(error, ("no audio in " + path).c_str());
        }
        NSError* nsError = nil;
        AVAssetReader* reader = [AVAssetReader assetReaderWithAsset:asset error:&nsError];
        NSDictionary* settings = @{
            AVFormatIDKey : @(kAudioFormatLinearPCM),
            AVLinearPCMBitDepthKey : @32,
            AVLinearPCMIsFloatKey : @YES,
            AVLinearPCMIsBigEndianKey : @NO,
            AVLinearPCMIsNonInterleaved : @NO,
            AVSampleRateKey : @(kAudioRate),
            AVNumberOfChannelsKey : @2,
        };
        AVAssetReaderTrackOutput* output = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:tracks[0] outputSettings:settings];
        if (!reader || ![reader canAddOutput:output]) {
            return fail(error, ("cannot read " + path).c_str());
        }
        [reader addOutput:output];
        [reader startReading];
        pcm.clear();
        while (CMSampleBufferRef sample = [output copyNextSampleBuffer]) {
            CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample);
            const size_t bytes = CMBlockBufferGetDataLength(block);
            const size_t old = pcm.size();
            pcm.resize(old + bytes / sizeof(float));
            CMBlockBufferCopyDataBytes(block, 0, bytes, pcm.data() + old);
            CFRelease(sample);
        }
        if (reader.status == AVAssetReaderStatusFailed) {
            return fail(error, ("cannot decode " + path).c_str());
        }
        return true;
    }

    // every clip mixed for frames [from, to); empty when the composition has no sound
    bool mixAudio(const Composition& comp, int from, int to, Pcm& mix, std::string* error) {
        mix.clear();
        if (comp.audio.empty()) {
            return true;
        }
        const double windowStart = static_cast<double>(from) / comp.fps;
        mix.assign(WindowFrames(static_cast<double>(to - from) / comp.fps) * 2, 0.f);
        for (const AudioClip& clip : PlacedAudio(comp)) {
            Pcm pcm;
            if (!decodeClip(clip.path, pcm, error)) {
                return false;
            }
            MixClip(mix, windowStart, pcm, clip);
        }
        Limit(mix);
        return true;
    }

    // frames [first, last) of the mix to the writer's audio input as one sample buffer
    bool appendAudio(AVAssetWriterInput* input, const Pcm& mix, size_t first, size_t last) {
        const size_t frames = last - first;
        const size_t bytes = frames * 2 * sizeof(float);
        AudioStreamBasicDescription format = {};
        format.mSampleRate = kAudioRate;
        format.mFormatID = kAudioFormatLinearPCM;
        format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        format.mChannelsPerFrame = 2;
        format.mBitsPerChannel = 32;
        format.mBytesPerFrame = 2 * sizeof(float);
        format.mFramesPerPacket = 1;
        format.mBytesPerPacket = format.mBytesPerFrame;
        CMAudioFormatDescriptionRef description = nullptr;
        CMBlockBufferRef block = nullptr;
        CMSampleBufferRef sample = nullptr;
        bool ok = CMAudioFormatDescriptionCreate(nullptr, &format, 0, nullptr, 0, nullptr, nullptr, &description) == noErr &&
                  CMBlockBufferCreateWithMemoryBlock(nullptr, nullptr, bytes, nullptr, nullptr, 0, bytes, kCMBlockBufferAssureMemoryNowFlag,
                                                     &block) == noErr &&
                  CMBlockBufferReplaceDataBytes(mix.data() + first * 2, block, 0, bytes) == noErr &&
                  CMAudioSampleBufferCreateReadyWithPacketDescriptions(nullptr, block, description, frames,
                                                                       CMTimeMake(static_cast<int64_t>(first), kAudioRate), nullptr,
                                                                       &sample) == noErr &&
                  [input appendSampleBuffer:sample];
        if (sample) {
            CFRelease(sample);
        }
        if (block) {
            CFRelease(block);
        }
        if (description) {
            CFRelease(description);
        }
        return ok;
    }

    // frames through ffmpeg: read back as rgba and piped
    bool renderWithFfmpeg(const Composition& comp, const std::string& path, const Options& options, std::string* error) {
        int from = 0, to = 0;
        frameRange(comp, options, from, to);
        Renderer renderer(comp);
        Exposure exposure(comp, options.blur);
        if (!renderer.ok() || !exposure.ok()) {
            return fail(error, "no metal device");
        }
        CVPixelBufferRef buffer = makeBuffer(comp);
        if (!buffer) {
            return fail(error, "no pixel buffer");
        }
        // the mix goes to a raw file ffmpeg reads as a second input
        Pcm mix;
        std::string audioFile;
        if (!mixAudio(comp, from, to, mix, error)) {
            CVPixelBufferRelease(buffer);
            return false;
        }
        if (!mix.empty()) {
            audioFile = path + ".audio.f32";
            FILE* f = std::fopen(audioFile.c_str(), "wb");
            const bool wrote = f && std::fwrite(mix.data(), sizeof(float), mix.size(), f) == mix.size();
            if (f) {
                std::fclose(f);
            }
            if (!wrote) {
                CVPixelBufferRelease(buffer);
                return fail(error, "could not write the audio file next to the output");
            }
        }
        FfmpegPipe pipe;
        bool ok = pipe.open(options, comp.width, comp.height, comp.fps, path, audioFile, comp.transparent, error);
        bool cancelled = false;
        for (int n = from; ok && n < to; ++n) {
            const Image image = exposure.draw(renderer, n, true, buffer) ? readImage(buffer, comp) : Image{};
            ok = image.width > 0 && pipe.write(image.rgba.data(), image.rgba.size());
            if (!ok) {
                fail(error, "ffmpeg stopped taking frames (is it installed, and does it have libx264?)");
            } else if (options.progress && !options.progress(n - from + 1, to - from)) {
                cancelled = true;
                break;
            }
        }
        CVPixelBufferRelease(buffer);
        const bool finished = pipe.close();
        if (!audioFile.empty()) {
            std::remove(audioFile.c_str());
        }
        if (ok && !cancelled && !finished) {
            ok = fail(error, "ffmpeg failed");
        }
        if (!ok || cancelled) {
            std::remove(path.c_str());
            return cancelled ? fail(error, "cancelled") : false;
        }
        return true;
    }
} // namespace

bool RenderVideo(const Composition& comp, const std::string& path, const Options& options, std::string* error) {
    @autoreleasepool {
        if (!validSize(comp, error)) {
            return false;
        }
        if (comp.width % 2 || comp.height % 2) {
            return fail(error, "h.264 needs an even width and height");
        }
        if (NeedsFfmpeg(comp, options)) {
            return renderWithFfmpeg(comp, path, options, error);
        }
        int from = 0, to = 0;
        frameRange(comp, options, from, to);

        Renderer renderer(comp);
        Exposure exposure(comp, options.blur);
        if (!renderer.ok() || !exposure.ok()) {
            return fail(error, "no metal device");
        }

        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
        [[NSFileManager defaultManager] removeItemAtURL:url error:nil];
        NSError* nsError = nil;
        AVAssetWriter* writer = [AVAssetWriter assetWriterWithURL:url fileType:AVFileTypeMPEG4 error:&nsError];
        if (!writer) {
            return fail(error, nsError.localizedDescription.UTF8String);
        }
        NSDictionary* settings = @{
            AVVideoCodecKey : AVVideoCodecTypeH264,
            AVVideoWidthKey : @(comp.width),
            AVVideoHeightKey : @(comp.height),
            AVVideoCompressionPropertiesKey : @{
                AVVideoAverageBitRateKey : @(static_cast<int>(comp.width * comp.height * comp.fps * 0.15)),
                AVVideoExpectedSourceFrameRateKey : @(comp.fps),
            },
        };
        AVAssetWriterInput* input = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:settings];
        input.expectsMediaDataInRealTime = NO;
        AVAssetWriterInputPixelBufferAdaptor* adaptor = [AVAssetWriterInputPixelBufferAdaptor
            assetWriterInputPixelBufferAdaptorWithAssetWriterInput:input
                                       sourcePixelBufferAttributes:bufferAttributes(comp.width, comp.height)];
        [writer addInput:input];
        Pcm mix;
        if (!mixAudio(comp, from, to, mix, error)) {
            return false;
        }
        AVAssetWriterInput* audioInput = nil;
        if (!mix.empty()) {
            NSDictionary* audioSettings = @{
                AVFormatIDKey : @(kAudioFormatMPEG4AAC),
                AVSampleRateKey : @(kAudioRate),
                AVNumberOfChannelsKey : @2,
                AVEncoderBitRateKey : @160000,
            };
            audioInput = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeAudio outputSettings:audioSettings];
            audioInput.expectsMediaDataInRealTime = NO;
            [writer addInput:audioInput];
        }
        const size_t mixFrames = mix.size() / 2;
        size_t audioDone = 0;
        if (![writer startWriting]) {
            return fail(error, writer.error.localizedDescription.UTF8String);
        }
        [writer startSessionAtSourceTime:kCMTimeZero];

        bool ok = true;
        bool cancelled = false;
        // sound up to `upTo` frames when the input will take it; the writer holds the pictures back until the sound catches up, so it is also fed while waiting
        auto feedAudio = [&](size_t upTo) {
            upTo = std::min(upTo, mixFrames);
            if (ok && audioInput && audioInput.readyForMoreMediaData && upTo > audioDone) {
                ok = appendAudio(audioInput, mix, audioDone, upTo);
                audioDone = upTo;
            }
        };
        for (int n = from; n < to && ok; ++n) {
            // a second ahead of the picture: the writer holds a picture until sound past it is in, so sound only up to the picture can leave both inputs waiting
            const size_t frameEnd = static_cast<size_t>(n - from + 1) * kAudioRate / comp.fps + kAudioRate;
            while (ok && !input.readyForMoreMediaData) {
                feedAudio(frameEnd);
                usleep(1000);
            }
            if (!ok) {
                fail(error, "the audio did not encode");
                break;
            }
            CVPixelBufferRef buffer = nullptr;
            if (CVPixelBufferPoolCreatePixelBuffer(nullptr, adaptor.pixelBufferPool, &buffer) != kCVReturnSuccess) {
                ok = fail(error, "no pixel buffer from the encoder's pool");
                break;
            }
            ok = exposure.draw(renderer, n, true, buffer) && [adaptor appendPixelBuffer:buffer withPresentationTime:CMTimeMake(n - from, comp.fps)];
            CVPixelBufferRelease(buffer);
            feedAudio(frameEnd);
            if (!ok) {
                fail(error, writer.error ? writer.error.localizedDescription.UTF8String : "drawing the frame failed");
            } else if (options.progress && !options.progress(n - from + 1, to - from)) {
                cancelled = true;
                break;
            }
        }

        if (!ok || cancelled) {
            [writer cancelWriting];
            [[NSFileManager defaultManager] removeItemAtURL:url error:nil];
            return cancelled ? fail(error, "cancelled") : false;
        }
        while (audioInput && audioDone < mixFrames) { // the rest of the sound, in chunks of a second
            if (!audioInput.readyForMoreMediaData) {
                usleep(1000);
                continue;
            }
            const size_t upTo = std::min(mixFrames, audioDone + kAudioRate);
            if (!appendAudio(audioInput, mix, audioDone, upTo)) {
                [writer cancelWriting];
                [[NSFileManager defaultManager] removeItemAtURL:url error:nil];
                return fail(error, writer.error ? writer.error.localizedDescription.UTF8String : "the audio did not encode");
            }
            audioDone = upTo;
        }
        [audioInput markAsFinished];
        [input markAsFinished];
        dispatch_semaphore_t finished = dispatch_semaphore_create(0);
        [writer finishWritingWithCompletionHandler:^{
            dispatch_semaphore_signal(finished);
        }];
        dispatch_semaphore_wait(finished, DISPATCH_TIME_FOREVER);
        if (writer.status != AVAssetWriterStatusCompleted) {
            return fail(error, writer.error.localizedDescription.UTF8String);
        }
        return true;
    }
}

Image RenderFrame(const Composition& comp, int index, std::string* error) {
    return RenderFrame(comp, index, MotionBlur{}, error);
}

Image RenderFrame(const Composition& comp, int index, const MotionBlur& blur, std::string* error) {
    @autoreleasepool {
        if (!validSize(comp, error)) {
            return {};
        }
        Renderer renderer(comp);
        Exposure exposure(comp, blur);
        if (!renderer.ok() || !exposure.ok()) {
            fail(error, "no metal device");
            return {};
        }
        CVPixelBufferRef buffer = makeBuffer(comp);
        if (!buffer) {
            fail(error, "no pixel buffer");
            return {};
        }
        Image image;
        if (exposure.draw(renderer, std::clamp(index, 0, comp.frames - 1), false, buffer)) {
            image = readImage(buffer, comp);
        } else {
            fail(error, "drawing the frame failed");
        }
        CVPixelBufferRelease(buffer);
        return image;
    }
}

bool Verify(const Composition& comp, std::span<const int> frames, std::string* report) {
    @autoreleasepool {
        std::string error;
        if (!validSize(comp, &error)) {
            if (report) {
                *report = error;
            }
            return false;
        }
        std::set<int> wanted;
        for (int f : frames) {
            wanted.insert(std::clamp(f, 0, comp.frames - 1));
        }
        // one renderer plays every frame up to the last wanted, as a render does
        std::map<int, Image> played;
        {
            Renderer renderer(comp);
            Exposure exposure(comp, MotionBlur{});
            CVPixelBufferRef buffer = makeBuffer(comp);
            if (!renderer.ok() || !buffer) {
                if (report) {
                    *report = "no metal device";
                }
                return false;
            }
            for (int n = 0; !wanted.empty() && n <= *wanted.rbegin(); ++n) {
                if (!exposure.draw(renderer, n, true, buffer)) { // as a render makes it, post included
                    CVPixelBufferRelease(buffer);
                    if (report) {
                        *report = "drawing frame " + std::to_string(n) + " failed";
                    }
                    return false;
                }
                if (wanted.count(n)) {
                    played[n] = readImage(buffer, comp);
                }
            }
            CVPixelBufferRelease(buffer);
        }
        bool same = true;
        std::string text;
        for (int f : wanted) {
            const Image seeked = RenderFrame(comp, f); // a fresh context, as a jump would give
            if (seeked.rgba != played[f].rgba) {
                same = false;
                size_t differing = 0;
                for (size_t i = 0; i + 3 < seeked.rgba.size(); i += 4) {
                    differing += std::memcmp(&seeked.rgba[i], &played[f].rgba[i], 4) != 0;
                }
                text += "frame " + std::to_string(f) + ": " + std::to_string(differing) + " pixels differ between playing and seeking\n";
            }
        }
        if (report) {
            *report = text;
        }
        return same;
    }
}

bool Frames(const Composition& comp, const Options& options, const std::function<bool(int, const Image&)>& fn, std::string* error) {
    @autoreleasepool {
        if (!validSize(comp, error)) {
            return false;
        }
        int from = 0, to = 0;
        frameRange(comp, options, from, to);
        Renderer renderer(comp);
        Exposure exposure(comp, options.blur);
        CVPixelBufferRef buffer = makeBuffer(comp);
        if (!renderer.ok() || !exposure.ok() || !buffer) {
            if (buffer) {
                CVPixelBufferRelease(buffer);
            }
            return fail(error, "no metal device");
        }
        bool ok = true;
        for (int n = from; ok && n < to; ++n) {
            if (!exposure.draw(renderer, n, true, buffer)) {
                ok = fail(error, ("drawing frame " + std::to_string(n) + " failed").c_str());
            } else if (!fn(n, readImage(buffer, comp)) || (options.progress && !options.progress(n - from + 1, to - from))) {
                break;
            }
        }
        CVPixelBufferRelease(buffer);
        return ok;
    }
}

bool RenderAudio(const Composition& comp, std::vector<float>& pcm, const Options& options, std::string* error) {
    @autoreleasepool {
        if (!validSize(comp, error)) {
            return false;
        }
        int from = 0, to = 0;
        frameRange(comp, options, from, to);
        pcm.clear();
        return mixAudio(comp, from, to, pcm, error);
    }
}

bool SavePng(const Image& image, const std::string& path) {
    if (image.width <= 0 || image.rgba.size() < static_cast<size_t>(image.width) * image.height * 4) {
        return false;
    }
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(const_cast<unsigned char*>(image.rgba.data()), image.width, image.height, 8,
                                                 static_cast<size_t>(image.width) * 4, space,
                                                 (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
    CGImageRef cgImage = context ? CGBitmapContextCreateImage(context) : nullptr;
    bool ok = false;
    if (cgImage) {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
        CGImageDestinationRef destination = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, CFSTR("public.png"), 1, nullptr);
        if (destination) {
            CGImageDestinationAddImage(destination, cgImage, nullptr);
            ok = CGImageDestinationFinalize(destination);
            CFRelease(destination);
        }
        CGImageRelease(cgImage);
    }
    if (context) {
        CGContextRelease(context);
    }
    CGColorSpaceRelease(space);
    return ok;
}

namespace {
    // the preview window: the composition drawn by the same renderer a file render uses, copied into
    // one texture the window shows, under a play button and a scrubber
    class PreviewDelegate : public imshell::Delegate {
    public:
        PreviewDelegate(const Composition& comp, const PreviewOptions& options) : comp_(comp), options_(options) {
            frame_ = std::clamp(options.startFrame, 0, comp.frames - 1);
            playing_ = options.play;
            loop_ = options.loop;
        }

        void setup() override {
            renderer_ = std::make_unique<Renderer>(comp_); // inside the app's context: it hands it back when it goes
            MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                                  width:comp_.width
                                                                                                 height:comp_.height
                                                                                              mipmapped:NO];
            descriptor.usage = MTLTextureUsageShaderRead;
            descriptor.storageMode = MTLStorageModeShared;
            texture_ = [MTLCreateSystemDefaultDevice() newTextureWithDescriptor:descriptor];
            buffer_ = makeBuffer(comp_);
            start_ = std::chrono::steady_clock::now();
            started_ = start_;
            baseFrame_ = frame_;
        }

        ~PreviewDelegate() override {
            if (buffer_) {
                CVPixelBufferRelease(buffer_);
            }
        }

        bool wantsFrames() override { return playing_; }

        bool shouldQuit() override {
            const bool done = options_.closeAfter > 0.0 && seconds(started_) >= options_.closeAfter;
            if (done) {
                renderer_.reset(); // before the app destroys the context it was made in
            }
            return done;
        }

        bool closeRequested() override {
            renderer_.reset();
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
            if (renderer_ && (shown_ != frame_ || dirty_)) {
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
            ImGui::Image(ImTextureID(reinterpret_cast<intptr_t>((__bridge void*)texture_)), size);

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

        // draws frame n and copies it into the texture
        void show(int n) {
            dirty_ = false;
            if (!renderer_->draw(n, false, buffer_)) {
                return;
            }
            CVPixelBufferLockBaseAddress(buffer_, kCVPixelBufferLock_ReadOnly);
            [texture_ replaceRegion:MTLRegionMake2D(0, 0, comp_.width, comp_.height)
                        mipmapLevel:0
                          withBytes:CVPixelBufferGetBaseAddress(buffer_)
                        bytesPerRow:CVPixelBufferGetBytesPerRow(buffer_)];
            CVPixelBufferUnlockBaseAddress(buffer_, kCVPixelBufferLock_ReadOnly);
            shown_ = n;
        }

        Composition comp_;
        PreviewOptions options_;
        std::unique_ptr<Renderer> renderer_;
        id<MTLTexture> texture_ = nil;
        CVPixelBufferRef buffer_ = nullptr;
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
    @autoreleasepool {
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
}
} // namespace imshell::video
