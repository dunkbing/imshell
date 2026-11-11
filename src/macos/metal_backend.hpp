#pragma once

#include "imgui.h"
#include <cstdint>

struct GLFWwindow;

namespace imshell {
    // metal through a CAMetalLayer on the glfw window's content view
    class MetalBackend {
    public:
        explicit MetalBackend(GLFWwindow* window);

        bool initializeImGui();
        // acquires the drawable and starts the imgui frame (metal + glfw)
        void beginFrame(ImVec4 clearColor);
        void renderDrawData(ImDrawData* drawData);
        // skip the drawing and the present: the window keeps showing the last frame
        void dropFrame();
        void present();
        void shutdown();
        void getFramebufferSize(int& width, int& height);
        ImTextureID createTextureFromRGBA(const uint8_t* pixels, int width, int height);

        bool vsync = true;

#ifdef __OBJC__
        id metalDevice_ = nullptr;
        id metalCommandQueue_ = nullptr;
        id metalLayer_ = nullptr;
        // per-frame objects, valid between beginFrame() and present()
        id currentDrawable_ = nullptr;
        id currentCommandBuffer_ = nullptr;
        id currentRenderEncoder_ = nullptr;
#else
        void* metalDevice_ = nullptr;
        void* metalCommandQueue_ = nullptr;
        void* metalLayer_ = nullptr;
        void* currentDrawable_ = nullptr;
        void* currentCommandBuffer_ = nullptr;
        void* currentRenderEncoder_ = nullptr;
#endif

    private:
        GLFWwindow* window_ = nullptr;
    };
} // namespace imshell
