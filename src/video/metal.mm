// video on metal: imgui draws into a pixel buffer's texture (an iosurface), which goes to
// AVAssetWriter untouched, or is copied out for RenderFrame
#import <AVFoundation/AVFoundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreVideo/CoreVideo.h>
#import <ImageIO/ImageIO.h>
#import <Metal/Metal.h>
#include "imshell/animation.hpp"
#include "imshell/themes.hpp"
#include "imshell/video.hpp"
#include "imgui.h"
#include "imgui_impl_metal.h"
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
        bool draw(int index, bool sequential, CVPixelBufferRef buffer) {
            ImGuiContext* saved = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(context_);
            const bool done = drawFrame(index, sequential, buffer);
            ImGui::SetCurrentContext(saved);
            return done;
        }

    private:
        bool drawFrame(int index, bool sequential, CVPixelBufferRef buffer) {
            CVMetalTextureRef cvTexture = nullptr;
            if (CVMetalTextureCacheCreateTextureFromImage(nullptr, cache_, buffer, nullptr, MTLPixelFormatBGRA8Unorm,
                                                          comp_.width, comp_.height, 0, &cvTexture) != kCVReturnSuccess) {
                return false;
            }
            id<MTLTexture> texture = CVMetalTextureGetTexture(cvTexture);
            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = texture;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;

            const Frame frame{.index = index, .frames = comp_.frames, .fps = comp_.fps,
                              .width = comp_.width, .height = comp_.height, .sequential = sequential};
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
} // namespace

bool RenderVideo(const Composition& comp, const std::string& path, const Options& options, std::string* error) {
    @autoreleasepool {
        if (!validSize(comp, error)) {
            return false;
        }
        if (comp.width % 2 || comp.height % 2) {
            return fail(error, "h.264 needs an even width and height");
        }
        const int from = std::clamp(options.from, 0, comp.frames - 1);
        const int to = options.to < 0 ? comp.frames : std::clamp(options.to, from + 1, comp.frames);

        Renderer renderer(comp);
        if (!renderer.ok()) {
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
        if (![writer startWriting]) {
            return fail(error, writer.error.localizedDescription.UTF8String);
        }
        [writer startSessionAtSourceTime:kCMTimeZero];

        bool ok = true;
        bool cancelled = false;
        for (int n = from; n < to && ok; ++n) {
            while (!input.readyForMoreMediaData) {
                usleep(1000);
            }
            CVPixelBufferRef buffer = nullptr;
            if (CVPixelBufferPoolCreatePixelBuffer(nullptr, adaptor.pixelBufferPool, &buffer) != kCVReturnSuccess) {
                ok = fail(error, "no pixel buffer from the encoder's pool");
                break;
            }
            ok = renderer.draw(n, true, buffer) && [adaptor appendPixelBuffer:buffer withPresentationTime:CMTimeMake(n - from, comp.fps)];
            CVPixelBufferRelease(buffer);
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
    @autoreleasepool {
        if (!validSize(comp, error)) {
            return {};
        }
        Renderer renderer(comp);
        if (!renderer.ok()) {
            fail(error, "no metal device");
            return {};
        }
        CVPixelBufferRef buffer = nullptr;
        if (CVPixelBufferCreate(nullptr, comp.width, comp.height, kCVPixelFormatType_32BGRA,
                                (__bridge CFDictionaryRef)bufferAttributes(comp.width, comp.height), &buffer) != kCVReturnSuccess) {
            fail(error, "no pixel buffer");
            return {};
        }
        Image image;
        if (renderer.draw(std::clamp(index, 0, comp.frames - 1), false, buffer)) {
            CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
            const auto* src = static_cast<const unsigned char*>(CVPixelBufferGetBaseAddress(buffer));
            const size_t stride = CVPixelBufferGetBytesPerRow(buffer);
            image.width = comp.width;
            image.height = comp.height;
            image.rgba.resize(static_cast<size_t>(comp.width) * comp.height * 4);
            for (int y = 0; y < comp.height; ++y) {
                const unsigned char* row = src + y * stride;
                unsigned char* out = &image.rgba[static_cast<size_t>(y) * comp.width * 4];
                for (int x = 0; x < comp.width; ++x) { // bgra to rgba
                    out[x * 4 + 0] = row[x * 4 + 2];
                    out[x * 4 + 1] = row[x * 4 + 1];
                    out[x * 4 + 2] = row[x * 4 + 0];
                    out[x * 4 + 3] = row[x * 4 + 3];
                }
            }
            CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
        } else {
            fail(error, "drawing the frame failed");
        }
        CVPixelBufferRelease(buffer);
        return image;
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
} // namespace imshell::video
