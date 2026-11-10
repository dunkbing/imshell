#include "dx11_backend.hpp"
#include "imgui_impl_dx11.h"

#include <cstdio>
#include <d3d11.h>
#include <dxgi.h>

namespace mydear {
    DX11Backend::DX11Backend(HWND hwnd) {
        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 2;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        D3D_FEATURE_LEVEL level;
        constexpr D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
        const HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd,
            &swapChain_, &device_, &level, &context_);
        if (FAILED(hr)) {
            std::fprintf(stderr, "mydear: D3D11CreateDeviceAndSwapChain failed: 0x%lx\n",
                         static_cast<unsigned long>(hr));
            return;
        }
        createRenderTarget();
    }

    DX11Backend::~DX11Backend() {
        cleanupRenderTarget();
        if (swapChain_)
            swapChain_->Release();
        if (context_)
            context_->Release();
        if (device_)
            device_->Release();
    }

    void DX11Backend::createRenderTarget() {
        ID3D11Texture2D* backBuffer = nullptr;
        swapChain_->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
        if (backBuffer) {
            device_->CreateRenderTargetView(backBuffer, nullptr, &target_);
            backBuffer->Release();
        }
    }

    void DX11Backend::cleanupRenderTarget() {
        if (target_) {
            target_->Release();
            target_ = nullptr;
        }
    }

    bool DX11Backend::initializeImGui() {
        return device_ && ImGui_ImplDX11_Init(device_, context_);
    }

    void DX11Backend::beginFrame() {
        ImGui_ImplDX11_NewFrame();
    }

    void DX11Backend::renderDrawData(ImDrawData* drawData, const ImVec4 clearColor) {
        const float clear[4] = {clearColor.x, clearColor.y, clearColor.z, clearColor.w};
        context_->OMSetRenderTargets(1, &target_, nullptr);
        context_->ClearRenderTargetView(target_, clear);
        ImGui_ImplDX11_RenderDrawData(drawData);
    }

    void DX11Backend::present() {
        swapChain_->Present(vsync ? 1 : 0, 0);
    }

    void DX11Backend::resize() {
        if (!swapChain_)
            return;
        cleanupRenderTarget();
        swapChain_->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
        createRenderTarget();
    }

    void DX11Backend::shutdown() {
        ImGui_ImplDX11_Shutdown();
    }

    ImTextureID DX11Backend::createTextureFromRGBA(const uint8_t* pixels, const int width,
                                                   const int height) {
        if (!device_ || !pixels)
            return ImTextureID{};
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = pixels;
        data.SysMemPitch = width * 4;
        ID3D11Texture2D* texture = nullptr;
        if (FAILED(device_->CreateTexture2D(&desc, &data, &texture)))
            return ImTextureID{};
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        ID3D11ShaderResourceView* srv = nullptr;
        const HRESULT hr = device_->CreateShaderResourceView(texture, &srvDesc, &srv);
        texture->Release();
        return FAILED(hr) ? ImTextureID{} : (ImTextureID)(INT_PTR)srv;
    }
} // namespace mydear
