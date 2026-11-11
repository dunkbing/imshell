// what one process writes to the clipboard another reads back, per format, through the display
// server (x11 or wayland), not gtk's shortcut for its own content. needs a display: skipped
// (passes) without one. it replaces what is on the clipboard
#include "imshell/clipboard.hpp"

#include <gtk/gtk.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <functional>

namespace Clipboard = imshell::Clipboard;

namespace {
    void pump(int ms) {
        const gint64 until = g_get_monotonic_time() + ms * 1000;
        while (g_get_monotonic_time() < until)
            g_main_context_iteration(nullptr, FALSE);
    }

    // a child process writes `items` and serves them while another checks what it reads
    bool roundTrip(const char* what, const std::vector<Clipboard::Item>& items,
                   const std::function<bool(const std::vector<Clipboard::Item>&)>& check) {
        const pid_t child = fork();
        if (child == 0) {
            gtk_init();
            Clipboard::Write(items);
            pump(2500);
            std::_Exit(0);
        }
        // the reader is a process of its own too: each opens its own display connection
        const pid_t reader = fork();
        if (reader == 0) {
            usleep(500000); // the writer owns the clipboard by now
            gtk_init();
            std::_Exit(check(Clipboard::Read()) ? 0 : 1);
        }
        int status = 1;
        waitpid(reader, &status, 0);
        waitpid(child, nullptr, 0);
        const bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (!ok)
            std::printf("FAILED: %s\n", what);
        return ok;
    }

    const std::string kPng = std::string("\x89PNG\r\n\x1a\n", 8) + std::string("\0\0\0\rIHDR", 8) + "not a real picture";
} // namespace

int main() {
    if (!std::getenv("DISPLAY") && !std::getenv("WAYLAND_DISPLAY")) {
        std::printf("clipboard: skipped (no display)\n");
        return 0;
    }
    int failed = 0;
    auto run = [&](const char* what, const std::vector<Clipboard::Item>& items, auto check) {
        failed += roundTrip(what, items, check) ? 0 : 1;
    };
    run("text reads back", {{{{"text/plain", "héllo"}}}},
        [](const auto& got) { return got.size() == 1 && got[0].get("text/plain") == "héllo"; });
    run("an item keeps each of its formats", {{{{"text/plain", "hi"}, {"text/html", "<b>hi</b>"}}}},
        [](const auto& got) {
            return got.size() == 1 && got[0].get("text/plain") == "hi" && got[0].get("text/html") == "<b>hi</b>";
        });
    run("a png reads back the same", {{{{"image/png", kPng}}}},
        [](const auto& got) { return got.size() == 1 && got[0].get("image/png") == kPng; });
    run("copied files come back as paths",
        {{{{"text/uri-list", "file:///tmp"}}}, {{{"text/uri-list", "file:///var/a%20b.txt"}}}},
        [](const auto& got) {
            return got.size() == 2 && got[0].file() == "/tmp" && got[1].file() == "/var/a b.txt";
        });
    std::printf(failed ? "%d failed\n" : "ok\n", failed);
    return failed ? 1 : 0;
}
