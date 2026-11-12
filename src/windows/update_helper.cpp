// tools\imshell-update.exe <pid> <app dir> [<exe to open after>]: the staged update's second half
// (src/windows/updater.cpp stages it). waits for the app to exit, swaps <app dir>\install\ in
// (staged_update.cpp: all or nothing), opens the app again when asked. logs to <app dir>\update.log
#include "../staged_update.hpp"

#include <windows.h>
#include <shellapi.h>

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {
    fs::path gDir;

    void log(const std::string& line) {
        const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
        std::ofstream(gDir / L"update.log", std::ios::app) << std::format("{:%F %T} {}\n", now, line);
    }

    // a file stays locked for a moment after the app exits (explorer, an antivirus scan): retry
    // such a rename for up to 10 s, give up at once on anything else
    bool rename(const fs::path& from, const fs::path& to) {
        for (int attempt = 0; attempt < 100; ++attempt) {
            if (MoveFileExW(from.c_str(), to.c_str(), 0))
                return true;
            const DWORD error = GetLastError();
            if (error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED && error != ERROR_LOCK_VIOLATION) {
                log(std::format("rename {} -> {} failed: error {}", imshell::detail::Utf8(from), imshell::detail::Utf8(to), error));
                return false;
            }
            Sleep(100);
        }
        log(std::format("rename {} -> {} still locked", imshell::detail::Utf8(from), imshell::detail::Utf8(to)));
        return false;
    }
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc < 3)
        return 2;
    const DWORD pid = wcstoul(argv[1], nullptr, 10);
    gDir = argv[2];
    const std::wstring relaunch = argc > 3 ? argv[3] : L"";
    LocalFree(argv);

    // one helper at a time per app: a stage re-armed at launch and a new one both wait here
    std::wstring name = L"imshell-update-" + gDir.wstring();
    for (wchar_t& c : name)
        if (c == L'\\' || c == L':')
            c = L'_';
    HANDLE mutex = CreateMutexW(nullptr, FALSE, name.c_str());
    if (mutex)
        WaitForSingleObject(mutex, INFINITE);

    if (HANDLE app = OpenProcess(SYNCHRONIZE, FALSE, pid)) {
        WaitForSingleObject(app, INFINITE);
        CloseHandle(app);
    }
    const bool staged = imshell::detail::HasStagedUpdate(gDir);
    const std::string error = imshell::detail::ApplyStagedUpdate(gDir, rename);
    if (staged)
        log(error.empty() ? "update applied" : "update failed, old version kept: " + error);
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }

    if (!relaunch.empty()) {
        std::wstring command = L"\"" + relaunch + L"\"";
        STARTUPINFOW si{};
        si.cb = sizeof si;
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, gDir.c_str(), &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        } else {
            log(std::format("cannot open the app again: error {}", GetLastError()));
        }
    }
    return error.empty() ? 0 : 1;
}
