// imshell-update.exe <pid> <app dir>: the staged update's second half
// (src/windows/updater.cpp stages it and runs a copy of this from %TEMP%, so an installer can
// replace tools\imshell-update.exe meanwhile). waits for the app to exit, stops anything else
// still running from the app dir, swaps <app dir>\install\ in (staged_update.cpp: all or nothing),
// opens the app again when the app left the reopen marker (restart now). logs to <app dir>\update.log
#include "../staged_update.hpp"

#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <chrono>
#include <cwctype>
#include <filesystem>
#include <format>
#include <iterator>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using imshell::detail::Utf8;

namespace {
    fs::path gDir;

    void log(const std::string& line) {
        const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
        std::ofstream(gDir / L"update.log", std::ios::app) << std::format("{:%F %T} {}\n", now, line);
    }

    std::wstring lower(std::wstring s) {
        for (wchar_t& c : s)
            c = static_cast<wchar_t>(std::towlower(c));
        return s;
    }

    // a file stays locked for a moment after the app exits (explorer, an antivirus scan): retry
    // such a rename for up to 30 s, give up at once on anything else
    bool renameRetrying(const fs::path& from, const fs::path& to) {
        for (int attempt = 0; attempt < 300; ++attempt) {
            if (MoveFileExW(from.c_str(), to.c_str(), 0))
                return true;
            const DWORD error = GetLastError();
            if (error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED && error != ERROR_LOCK_VIOLATION) {
                log(std::format("rename {} -> {} failed: error {}", Utf8(from), Utf8(to), error));
                return false;
            }
            Sleep(100);
        }
        log(std::format("rename {} -> {} still locked", Utf8(from), Utf8(to)));
        return false;
    }

    // processes running an exe from the app dir: another window, the app's cli, a server an
    // editor started from it. they hold files the swap replaces
    std::vector<HANDLE> runningFromAppDir() {
        const std::wstring prefix = lower(gDir.wstring() + L"\\");
        std::vector<HANDLE> found;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return found;
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof entry;
        for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry)) {
            if (entry.th32ProcessID == GetCurrentProcessId())
                continue;
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE | PROCESS_TERMINATE, FALSE,
                                         entry.th32ProcessID);
            if (!process)
                continue;
            std::wstring image(32768, L'\0');
            DWORD size = static_cast<DWORD>(image.size());
            if (QueryFullProcessImageNameW(process, 0, image.data(), &size) &&
                lower(image.substr(0, size)).starts_with(prefix))
                found.push_back(process);
            else
                CloseHandle(process);
        }
        CloseHandle(snapshot);
        return found;
    }

    // asks each visible top-level window of pid to close, as a user would; false when it has none
    bool askToClose(DWORD pid) {
        struct Find {
            DWORD pid;
            bool asked;
        } find{pid, false};
        EnumWindows(
            [](HWND hwnd, LPARAM param) -> BOOL {
                auto* f = reinterpret_cast<Find*>(param);
                DWORD owner = 0;
                GetWindowThreadProcessId(hwnd, &owner);
                if (owner == f->pid && IsWindowVisible(hwnd) && !GetWindow(hwnd, GW_OWNER)) {
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                    f->asked = true;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&find));
        return find.asked;
    }

    void end(HANDLE process) {
        TerminateProcess(process, 1);
        WaitForSingleObject(process, 5000);
        log(std::format("ended process {}", GetProcessId(process)));
    }

    // windows are asked to close (their own unsaved-work prompt still shows) and get 30 s, then
    // are ended; windowless ones (a command line, a server an editor started) are ended at once
    void stopOthers() {
        std::vector<HANDLE> windows;
        for (HANDLE process : runningFromAppDir()) {
            if (askToClose(GetProcessId(process))) {
                log(std::format("asked process {} to close", GetProcessId(process)));
                windows.push_back(process);
            } else {
                end(process);
                CloseHandle(process);
            }
        }
        const ULONGLONG deadline = GetTickCount64() + 30000;
        for (HANDLE process : windows) {
            const ULONGLONG now = GetTickCount64();
            if (WaitForSingleObject(process, now < deadline ? static_cast<DWORD>(deadline - now) : 0) == WAIT_TIMEOUT)
                end(process);
            CloseHandle(process);
        }
    }
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc < 3)
        return 2;
    const DWORD pid = wcstoul(argv[1], nullptr, 10);
    gDir = argv[2];
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
    // written while the app ran, so read once it is gone; a stale one never outlives a run
    fs::path relaunch;
    {
        const fs::path reopen = gDir / imshell::detail::kReopenMarker;
        std::ifstream in(reopen, std::ios::binary);
        const std::string utf8{std::istreambuf_iterator<char>(in), {}};
        in.close();
        std::error_code ec;
        fs::remove(reopen, ec);
        if (!utf8.empty())
            relaunch = fs::path(std::u8string(utf8.begin(), utf8.end()));
    }
    const bool staged = imshell::detail::HasStagedUpdate(gDir);
    if (staged)
        stopOthers();
    const std::string error = imshell::detail::ApplyStagedUpdate(gDir, renameRetrying);
    if (staged)
        log(error.empty() ? "update applied" : "update failed, old version kept: " + error);
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }

    if (!relaunch.empty()) {
        std::wstring command = L"\"" + relaunch.wstring() + L"\"";
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
