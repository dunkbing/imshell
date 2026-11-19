// linux (and anything with EGL and desktop GL): imgui drawn into a framebuffer object on a context with
// no window, a pbuffer standing in for the surface. mesa's llvmpipe makes that work on a machine
// with no gpu or display (libegl1 and libgl1-mesa-dri)
#include <epoxy/egl.h>
#include <epoxy/gl.h> // before any other gl header

#include "imgui_impl_opengl3.h"
#include "rasterizer.hpp"

#include <cstdio>
#include <cstring>

namespace imshell::video {
    namespace {
        class EglRasterizer : public Rasterizer {
        public:
            explicit EglRasterizer(const Composition& comp) : comp_(comp) {}

            ~EglRasterizer() override {
                if (context_ != EGL_NO_CONTEXT && display_ != EGL_NO_DISPLAY) {
                    eglMakeCurrent(display_, surface_, surface_, context_);
                    if (imguiContext_) {
                        ImGui::SetCurrentContext(imguiContext_);
                        ImGui_ImplOpenGL3_Shutdown();
                        ImGui::DestroyContext(imguiContext_);
                        imshell::SetReduceMotion(std::nullopt);
                    }
                    if (framebuffer_) {
                        glDeleteFramebuffers(1, &framebuffer_);
                        glDeleteTextures(1, &texture_);
                    }
                    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                    eglDestroyContext(display_, context_);
                    if (surface_ != EGL_NO_SURFACE) {
                        eglDestroySurface(display_, surface_);
                    }
                    eglTerminate(display_);
                }
            }

            bool init(std::string* error) {
                // the mesa surfaceless platform needs no display server; the default display is the fallback
                if (epoxy_has_egl_extension(EGL_NO_DISPLAY, "EGL_MESA_platform_surfaceless")) {
                    display_ = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
                }
                if (display_ == EGL_NO_DISPLAY) {
                    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
                }
                EGLint major = 0, minor = 0;
                if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, &major, &minor)) {
                    return fail(error, "EGL has no display (is libegl1 installed?)");
                }
                if (!eglBindAPI(EGL_OPENGL_API)) {
                    return fail(error, "EGL has no desktop OpenGL");
                }
                const EGLint configAttributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                                                   EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
                EGLConfig config = nullptr;
                EGLint count = 0;
                if (!eglChooseConfig(display_, configAttributes, &config, 1, &count) || count == 0) {
                    return fail(error, "EGL has no pbuffer config");
                }
                const EGLint contextAttributes[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
                                                    EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
                context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, contextAttributes);
                const EGLint surfaceAttributes[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
                surface_ = eglCreatePbufferSurface(display_, config, surfaceAttributes);
                if (context_ == EGL_NO_CONTEXT || surface_ == EGL_NO_SURFACE || !eglMakeCurrent(display_, surface_, surface_, context_)) {
                    return fail(error, "could not make an OpenGL 3.3 context (mesa's llvmpipe or a driver is needed)");
                }

                glGenTextures(1, &texture_);
                glBindTexture(GL_TEXTURE_2D, texture_);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, comp_.width, comp_.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                glGenFramebuffers(1, &framebuffer_);
                glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_, 0);
                if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                    return fail(error, "the offscreen framebuffer is not complete");
                }

                imguiContext_ = ImGui::CreateContext();
                ImGui::SetCurrentContext(imguiContext_);
                PrepareContext(comp_);
                if (!ImGui_ImplOpenGL3_Init("#version 330")) {
                    return fail(error, "imgui's OpenGL backend did not start");
                }
                imshell::ApplyTheme(); // the theme is process wide, the style is per context
                if (comp_.setup) {
                    comp_.setup();
                }
                return true;
            }

            bool render(int index, bool sequential, float offset, std::vector<unsigned char>& rgba) override {
                eglMakeCurrent(display_, surface_, surface_, context_);
                ImGui::SetCurrentContext(imguiContext_);
                glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
                glViewport(0, 0, comp_.width, comp_.height);
                glClearColor(0.f, 0.f, 0.f, 1.f);
                glClear(GL_COLOR_BUFFER_BIT);
                DrawComposition(comp_, index, sequential, offset, ImGui_ImplOpenGL3_NewFrame);
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

                // gl rows run bottom to top: read them into place the other way up
                const size_t row = static_cast<size_t>(comp_.width) * 4;
                std::vector<unsigned char> upsideDown(row * comp_.height);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(0, 0, comp_.width, comp_.height, GL_RGBA, GL_UNSIGNED_BYTE, upsideDown.data());
                if (glGetError() != GL_NO_ERROR) {
                    return false;
                }
                rgba.resize(upsideDown.size());
                for (int y = 0; y < comp_.height; ++y) {
                    std::memcpy(&rgba[static_cast<size_t>(y) * row], &upsideDown[static_cast<size_t>(comp_.height - 1 - y) * row], row);
                }
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
            EGLDisplay display_ = EGL_NO_DISPLAY;
            EGLContext context_ = EGL_NO_CONTEXT;
            EGLSurface surface_ = EGL_NO_SURFACE;
            GLuint framebuffer_ = 0, texture_ = 0;
            ImGuiContext* imguiContext_ = nullptr;
        };
    } // namespace

    std::unique_ptr<Rasterizer> MakeRasterizer(const Composition& comp, std::string* error) {
        auto rasterizer = std::make_unique<EglRasterizer>(comp);
        if (!rasterizer->init(error)) {
            return nullptr;
        }
        return rasterizer;
    }
} // namespace imshell::video
