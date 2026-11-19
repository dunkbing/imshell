// windows: imgui drawn into an offscreen direct3d 11 texture and read back through a staging
// copy. no window and no swap chain; with no gpu (a vm, a remote session) warp, direct3d's
// software renderer, does the drawing
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <wrl/client.h>

#include "imgui_impl_dx11.h"
#include "rasterizer.hpp"

#include <cstdio>
#include <cstring>

namespace imshell::video {
    namespace {
        using Microsoft::WRL::ComPtr;

        class D3D11Rasterizer : public Rasterizer {
        public:
            explicit D3D11Rasterizer(const Composition& comp) : comp_(comp) {}

            ~D3D11Rasterizer() override {
                if (imguiContext_) {
                    ImGui::SetCurrentContext(imguiContext_);
                    ImGui_ImplDX11_Shutdown();
                    ImGui::DestroyContext(imguiContext_);
                    imshell::SetReduceMotion(std::nullopt);
                }
            }

            bool init(std::string* error) {
                constexpr D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
                HRESULT hr = E_FAIL;
                for (const D3D_DRIVER_TYPE type : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
                    hr = D3D11CreateDevice(nullptr, type, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &device_, nullptr, &context_);
                    if (SUCCEEDED(hr)) {
                        break;
                    }
                }
                if (FAILED(hr)) {
                    return fail(error, "Direct3D 11 is not available (no GPU and no WARP)");
                }

                D3D11_TEXTURE2D_DESC desc = {};
                desc.Width = static_cast<UINT>(comp_.width);
                desc.Height = static_cast<UINT>(comp_.height);
                desc.MipLevels = 1;
                desc.ArraySize = 1;
                desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = D3D11_BIND_RENDER_TARGET;
                if (FAILED(device_->CreateTexture2D(&desc, nullptr, &target_)) || FAILED(device_->CreateRenderTargetView(target_.Get(), nullptr, &view_))) {
                    return fail(error, "could not make the offscreen render target");
                }
                desc.Usage = D3D11_USAGE_STAGING;
                desc.BindFlags = 0;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                if (FAILED(device_->CreateTexture2D(&desc, nullptr, &staging_))) {
                    return fail(error, "could not make the readback texture");
                }

                imguiContext_ = ImGui::CreateContext();
                ImGui::SetCurrentContext(imguiContext_);
                PrepareContext(comp_);
                if (!ImGui_ImplDX11_Init(device_.Get(), context_.Get())) {
                    ImGui::DestroyContext(imguiContext_);
                    imguiContext_ = nullptr;
                    return fail(error, "imgui's Direct3D backend did not start");
                }
                imshell::ApplyTheme(); // the theme is process wide, the style is per context
                if (comp_.setup) {
                    comp_.setup();
                }
                return true;
            }

            bool render(int index, bool sequential, float offset, std::vector<unsigned char>& rgba) override {
                ImGui::SetCurrentContext(imguiContext_);
                const FLOAT black[4] = {0.f, 0.f, 0.f, comp_.transparent ? 0.f : 1.f};
                ID3D11RenderTargetView* view = view_.Get();
                context_->OMSetRenderTargets(1, &view, nullptr);
                context_->ClearRenderTargetView(view, black);
                D3D11_VIEWPORT viewport = {0.f, 0.f, static_cast<float>(comp_.width), static_cast<float>(comp_.height), 0.f, 1.f};
                context_->RSSetViewports(1, &viewport);
                DrawComposition(comp_, index, sequential, offset, ImGui_ImplDX11_NewFrame);
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

                context_->CopyResource(staging_.Get(), target_.Get());
                D3D11_MAPPED_SUBRESOURCE mapped = {};
                if (FAILED(context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
                    return false;
                }
                const size_t row = static_cast<size_t>(comp_.width) * 4;
                rgba.resize(row * comp_.height);
                for (int y = 0; y < comp_.height; ++y) {
                    std::memcpy(&rgba[static_cast<size_t>(y) * row], static_cast<const unsigned char*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch, row);
                }
                context_->Unmap(staging_.Get(), 0);
                return true;
            }

        private:
            static bool fail(std::string* error, const char* what) {
                if (error) {
                    *error = what;
                }
                return false;
            }

            const Composition& comp_;
            ComPtr<ID3D11Device> device_;
            ComPtr<ID3D11DeviceContext> context_;
            ComPtr<ID3D11Texture2D> target_, staging_;
            ComPtr<ID3D11RenderTargetView> view_;
            ImGuiContext* imguiContext_ = nullptr;
        };
    } // namespace

    std::unique_ptr<Rasterizer> MakeRasterizer(const Composition& comp, std::string* error) {
        auto rasterizer = std::make_unique<D3D11Rasterizer>(comp);
        if (!rasterizer->init(error)) {
            return nullptr;
        }
        return rasterizer;
    }
} // namespace imshell::video
