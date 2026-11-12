// winsparkle: the same appcast and ed25519 key as sparkle, its own dialogs; with stageArguments
// the installer only stages the update and tools\imshell-update.exe swaps it in (zed's scheme)
#include "imshell/updater.hpp"

#include "../staged_update.hpp"

#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>
#include <windows.h>
#include <winsparkle.h>

namespace imshell::updater {
    namespace {
        bool gStarted = false;

        std::wstring wide(const std::string& s) {
            const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
            std::wstring out(n > 0 ? n - 1 : 0, L'\0');
            if (n > 1)
                MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
            return out;
        }

        // the installer replaces the exe, so winsparkle asks the app to quit right after starting
        // it: close the main window as the user would (closeRequested can still veto), from
        // winsparkle's thread, so only PostMessage. glfw's class, not winsparkle's own dialogs
        void requestShutdown() {
            EnumWindows(
                [](HWND hwnd, LPARAM) -> BOOL {
                    DWORD pid = 0;
                    GetWindowThreadProcessId(hwnd, &pid);
                    wchar_t cls[16] = {};
                    GetClassNameW(hwnd, cls, 16);
                    if (pid != GetCurrentProcessId() || wcscmp(cls, L"GLFW30") != 0)
                        return TRUE;
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                    return FALSE;
                },
                0);
        }

        std::wstring gStageArguments;

        std::filesystem::path exePath() {
            std::wstring path(32768, L'\0');
            path.resize(GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size())));
            return path;
        }

        bool spawn(std::wstring command, const std::filesystem::path& cwd, DWORD flags, HANDLE* process) {
            STARTUPINFOW si{};
            si.cb = sizeof si;
            PROCESS_INFORMATION pi{};
            if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, flags, nullptr,
                                cwd.c_str(), &si, &pi))
                return false;
            CloseHandle(pi.hThread);
            if (process)
                *process = pi.hProcess;
            else
                CloseHandle(pi.hProcess);
            return true;
        }

        // tools\imshell-update.exe waits for this process to exit, swaps the stage in and, when
        // the user chose to install now, opens the app again
        bool armHelper(bool relaunch) {
            const std::filesystem::path exe = exePath(), dir = exe.parent_path();
            const std::filesystem::path helper = dir / L"tools" / L"imshell-update.exe";
            std::wstring command = L"\"" + helper.wstring() + L"\" " + std::to_wstring(GetCurrentProcessId()) +
                                   L" \"" + dir.wstring() + L"\"";
            if (relaunch)
                command += L" \"" + exe.wstring() + L"\"";
            return spawn(command, dir, DETACHED_PROCESS, nullptr);
        }

        // winsparkle downloaded and verified the installer: run it in update mode, which installs
        // into <app dir>\install\ while this version keeps running, then hand over to the helper.
        // winsparkle asks the app to quit after this returns (requestShutdown)
        int __cdecl stageUpdate(const wchar_t* installer) {
            const std::filesystem::path dir = exePath().parent_path();
            std::error_code ec;
            std::filesystem::remove_all(dir / L"install", ec); // an earlier, unfinished stage
            HANDLE process = nullptr;
            if (!spawn(L"\"" + std::wstring(installer) + L"\" " + gStageArguments, dir, 0, &process))
                return WINSPARKLE_RETURN_ERROR;
            WaitForSingleObject(process, INFINITE);
            DWORD code = 1;
            GetExitCodeProcess(process, &code);
            CloseHandle(process);
            if (code != 0 || !std::filesystem::is_directory(dir / L"install", ec))
                return WINSPARKLE_RETURN_ERROR;
            std::ofstream(dir / L"install" / detail::kStagedMarker) << "staged\n";
            return armHelper(true) ? 1 : WINSPARKLE_RETURN_ERROR;
        }
    } // namespace

    void Start(const Config& config) {
        if (gStarted || config.appcastUrl.empty())
            return;
        win_sparkle_set_app_details(wide(config.appName).c_str(), wide(config.appName).c_str(),
                                    wide(config.version).c_str());
        win_sparkle_set_appcast_url(config.appcastUrl.c_str());
        if (!config.publicKey.empty())
            win_sparkle_set_eddsa_public_key(config.publicKey.c_str());
        win_sparkle_set_automatic_check_for_updates(1);
        win_sparkle_set_shutdown_request_callback(requestShutdown);
        if (!config.stageArguments.empty()) {
            gStageArguments = wide(config.stageArguments);
            win_sparkle_set_user_run_installer_callback(stageUpdate);
            // staged earlier and not applied (a close was vetoed, the machine restarted): it
            // goes in when this run exits, without opening the app again
            if (detail::HasStagedUpdate(exePath().parent_path()))
                armHelper(false);
        }
        win_sparkle_init();
        gStarted = true;
    }

    void Check() {
        if (gStarted)
            win_sparkle_check_update_with_ui();
    }

    bool Available() {
        return false;
    }

    std::string LatestVersion() {
        return {};
    }

    void Stop() {
        if (gStarted)
            win_sparkle_cleanup();
        gStarted = false;
    }
} // namespace imshell::updater
