#pragma once

#include "imgui.h"
#include <cstdint>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11RenderTargetView;

namespace imshell {
    // direct3d 11 swap chain on the window, and the imgui dx11 renderer
    class DX11Backend {
    public:
        explicit DX11Backend(HWND hwnd);
        ~DX11Backend();

        bool ok() const {
            return device_ != nullptr;
        }
        bool initializeImGui();
        void beginFrame();
        // clears to clearColor, then draws: only once the frame is known to differ
        void renderDrawData(ImDrawData* drawData, ImVec4 clearColor);
        void present();
        void resize(); // the window's client area changed
        void shutdown();
        ImTextureID createTextureFromRGBA(const uint8_t* pixels, int width, int height);

        bool vsync = true;
        // resized buffers hold nothing: the next frame draws even when it matches the last
        bool mustDraw = true;

    private:
        void createRenderTarget();
        void cleanupRenderTarget();

        ID3D11Device* device_ = nullptr;
        ID3D11DeviceContext* context_ = nullptr;
        IDXGISwapChain* swapChain_ = nullptr;
        ID3D11RenderTargetView* target_ = nullptr;
    };
} // namespace imshell
