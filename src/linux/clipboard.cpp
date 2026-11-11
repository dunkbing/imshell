// the clipboard on linux: gtk4's GdkClipboard, every format offered with a mime type. gtk reads
// asynchronously, so Read runs the main context until each format has arrived (a second at
// most); ReadText and WriteText stay on imgui's hooks (app.cpp)
#include "imshell/clipboard.hpp"

#include <gtk/gtk.h>

#include <algorithm>
#include <string_view>
#include <vector>

namespace imshell::Clipboard {
    namespace {
        GdkClipboard* clipboard() {
            GdkDisplay* display = gdk_display_get_default();
            return display ? gdk_display_get_clipboard(display) : nullptr;
        }

        // one read in flight; freed by its last callback, which may come after Read gave up on it
        struct Pending {
            GMemoryOutputStream* sink = G_MEMORY_OUTPUT_STREAM(g_memory_output_stream_new_resizable());
            GCancellable* cancel = g_cancellable_new();
            bool done = false, ok = false, abandoned = false;

            void finish(bool success) {
                ok = success;
                done = true;
                if (abandoned)
                    delete this;
            }
            ~Pending() {
                g_object_unref(sink);
                g_object_unref(cancel);
            }
        };

        // one format's bytes; nothing when it does not arrive within a second
        bool readFormat(GdkClipboard* board, const char* mime, std::string& out) {
            auto* read = new Pending;
            const char* mimes[] = {mime, nullptr};
            gdk_clipboard_read_async(
                board, mimes, G_PRIORITY_DEFAULT, read->cancel,
                [](GObject* source, GAsyncResult* result, gpointer data) {
                    auto* r = static_cast<Pending*>(data);
                    GInputStream* in = gdk_clipboard_read_finish(GDK_CLIPBOARD(source), result, nullptr, nullptr);
                    if (!in)
                        return r->finish(false);
                    // spliced, not read in place: our own content is written into the pipe by
                    // this same main loop
                    g_output_stream_splice_async(
                        G_OUTPUT_STREAM(r->sink), in,
                        GOutputStreamSpliceFlags(G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE | G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET),
                        G_PRIORITY_DEFAULT, r->cancel,
                        [](GObject* sink, GAsyncResult* res, gpointer data) {
                            static_cast<Pending*>(data)->finish(
                                g_output_stream_splice_finish(G_OUTPUT_STREAM(sink), res, nullptr) >= 0);
                        },
                        r);
                    g_object_unref(in);
                },
                read);
            const gint64 until = g_get_monotonic_time() + G_USEC_PER_SEC;
            while (!read->done && g_get_monotonic_time() < until)
                g_main_context_iteration(nullptr, FALSE);
            if (!read->done) { // the owner is stuck: give up, the callback frees it
                read->abandoned = true;
                g_cancellable_cancel(read->cancel);
                return false;
            }
            const bool ok = read->ok;
            if (ok)
                out.assign(static_cast<const char*>(g_memory_output_stream_get_data(read->sink)),
                           g_memory_output_stream_get_data_size(read->sink));
            delete read;
            return ok;
        }

        // "text/plain;charset=utf-8" is text/plain; other parameters and gtk's own names are left out
        std::string mimeOf(std::string_view offered) {
            if (offered == "text/plain;charset=utf-8")
                return "text/plain";
            if (offered.find('/') == offered.npos || offered.find(';') != offered.npos ||
                offered.starts_with("application/x-gtk") || offered.starts_with("application/x-gdk"))
                return {};
            return std::string(offered);
        }

        GBytes* bytesOf(const std::string& s) {
            return g_bytes_new(s.data(), s.size());
        }
    } // namespace

    std::vector<Item> Read() {
        GdkClipboard* board = clipboard();
        if (!board)
            return {};
        Item item;
        gsize count = 0;
        const char* const* offered = gdk_content_formats_get_mime_types(gdk_clipboard_get_formats(board), &count);
        // gdk learns another app's formats asynchronously: just after start (or a copy) it may
        // know none yet
        const gint64 until = g_get_monotonic_time() + 200000;
        while (count == 0 && g_get_monotonic_time() < until) {
            g_main_context_iteration(nullptr, FALSE);
            offered = gdk_content_formats_get_mime_types(gdk_clipboard_get_formats(board), &count);
        }
        // utf-8 text first, so a plain "text/plain" in the locale's charset does not win
        std::vector<std::string_view> order(offered, offered + count);
        std::stable_partition(order.begin(), order.end(),
                              [](std::string_view m) { return m == "text/plain;charset=utf-8"; });
        for (std::string_view offeredMime : order) {
            const std::string mime = mimeOf(offeredMime);
            std::string bytes;
            if (!mime.empty() && !item.has(mime) && readFormat(board, std::string(offeredMime).c_str(), bytes))
                item.data[mime] = std::move(bytes);
        }
        if (item.data.empty())
            return {};
        // copied files: one item each, like the other platforms
        const std::string uris = item.get("text/uri-list");
        std::vector<Item> files;
        for (size_t start = 0; start < uris.size();) {
            const size_t end = std::min(uris.find_first_of("\r\n", start), uris.size());
            std::string uri = uris.substr(start, end - start);
            start = uris.find_first_not_of("\r\n", end);
            if (!uri.empty() && uri[0] != '#') // a comment line
                files.push_back(Item{{{"text/uri-list", uri}}});
        }
        if (files.size() > 1)
            return files;
        return {item};
    }

    void Write(const std::vector<Item>& items) {
        GdkClipboard* board = clipboard();
        if (!board)
            return;
        // the clipboard holds one thing: files from every item become one list, otherwise the
        // first item is written
        Item item = items.empty() ? Item{} : items[0];
        if (items.size() > 1) {
            std::string uris;
            for (const Item& i : items)
                if (i.has("text/uri-list"))
                    uris += i.get("text/uri-list") + "\r\n";
            if (!uris.empty())
                item = Item{{{"text/uri-list", uris}}};
        }
        std::vector<GdkContentProvider*> providers;
        auto add = [&](const char* mime, const std::string& bytes) {
            GBytes* b = bytesOf(bytes);
            providers.push_back(gdk_content_provider_new_for_bytes(mime, b));
            g_bytes_unref(b);
        };
        for (const auto& [mime, bytes] : item.data) {
            if (mime == "text/plain") // what x11 and wayland apps ask for (UTF8_STRING)
                add("text/plain;charset=utf-8", bytes);
            add(mime.c_str(), bytes);
            if (mime == "text/uri-list" && item.file().size()) // file managers paste these
                add("x-special/gnome-copied-files", "copy\n" + bytes);
        }
        GdkContentProvider* content = gdk_content_provider_new_union(providers.data(), providers.size());
        gdk_clipboard_set_content(board, content);
        g_object_unref(content);
    }
} // namespace imshell::Clipboard
