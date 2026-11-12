// windows: an input method composes inline in the field (ime.cpp), as on the other platforms,
// instead of in the system's own small window. glfw takes only WM_CHAR, so the composition
// messages are handled here, in the shell's window procedure, before glfw sees them
#include "../ime.hpp"
#include "imgui_internal.h"

#include <windows.h>
#include <imm.h>

#include <string>

namespace ime = imshell::detail::ime;

namespace {
    HWND gHwnd = nullptr;

    std::string utf8(const std::wstring& w) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        std::string out(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
        return out;
    }

    // the composition (GCS_COMPSTR) or the committed text (GCS_RESULTSTR)
    std::string read(HWND hwnd, DWORD what) {
        HIMC context = ImmGetContext(hwnd);
        if (!context)
            return {};
        const LONG bytes = ImmGetCompositionStringW(context, what, nullptr, 0);
        std::wstring text(bytes > 0 ? bytes / sizeof(wchar_t) : 0, L'\0');
        if (bytes > 0)
            ImmGetCompositionStringW(context, what, text.data(), bytes);
        ImmReleaseContext(hwnd, context);
        return utf8(text);
    }

    // the composition stays as typed and the input method drops it; what it sends back for the
    // cancel finds nothing composing, so it changes nothing
    void endComposition(HWND hwnd) {
        ime::forget();
        if (HIMC context = ImmGetContext(hwnd)) {
            ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
            ImmReleaseContext(hwnd, context);
        }
    }
} // namespace

namespace imshell::detail {
    // true when the message is handled, with its result
    bool imeMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result) {
        gHwnd = hwnd;
        switch (msg) {
        case WM_IME_SETCONTEXT: // no system composition window: the field shows it
            result = DefWindowProcW(hwnd, msg, wParam, lParam & ~ISC_SHOWUICOMPOSITIONWINDOW);
            return true;
        case WM_IME_STARTCOMPOSITION:
            result = 0;
            return true;
        case WM_IME_COMPOSITION:
            // not passed on: DefWindowProc would send the result again as WM_IME_CHAR / WM_CHAR
            if (lParam & GCS_RESULTSTR)
                ime::commit(read(hwnd, GCS_RESULTSTR));
            if (lParam & GCS_COMPSTR)
                ime::compose(read(hwnd, GCS_COMPSTR));
            else if (!(lParam & GCS_RESULTSTR))
                ime::compose(""); // cancelled
            result = 0;
            return true;
        case WM_IME_ENDCOMPOSITION:
            if (ime::composing())
                ime::compose(""); // ended without a result: cancelled
            result = 0;
            return true;
        case WM_KEYDOWN:
        case WM_KEYUP:
            if (wParam != VK_PROCESSKEY)
                return false;
            result = DefWindowProcW(hwnd, msg, wParam, lParam); // the input method's key, not the field's
            return true;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            if (ime::composing())
                endComposition(hwnd); // the click may move the caret: the word stays as it is
            return false;
        }
        return false;
    }

    // imgui's own windows handler, but the candidate window keeps off the line being typed
    void installTextInput() {
        ImGui::GetPlatformIO().Platform_SetImeDataFn = [](ImGuiContext*, ImGuiViewport* viewport, ImGuiPlatformImeData* data) {
            HWND hwnd = static_cast<HWND>(viewport->PlatformHandleRaw);
            HIMC context = hwnd ? ImmGetContext(hwnd) : nullptr;
            if (!context)
                return;
            const LONG x = static_cast<LONG>(data->InputPos.x - viewport->Pos.x);
            const LONG y = static_cast<LONG>(data->InputPos.y - viewport->Pos.y);
            COMPOSITIONFORM composition = {CFS_FORCE_POSITION, {x, y}, {}};
            ImmSetCompositionWindow(context, &composition);
            CANDIDATEFORM candidate = {0, CFS_EXCLUDE, {x, y}, {x, y, x + 1, y + static_cast<LONG>(data->InputLineHeight)}};
            ImmSetCandidateWindow(context, &candidate);
            ImmReleaseContext(hwnd, context);
        };
    }

    // after the app's frame: a composition whose field went away ends where it is
    void textInputFrame() {
        if (gHwnd && ime::composing() && !GImGui->PlatformImeData.WantTextInput)
            endComposition(gHwnd);
        drawComposingUnderline(ime::text().c_str());
    }
} // namespace imshell::detail
