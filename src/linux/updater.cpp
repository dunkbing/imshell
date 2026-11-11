// linux: an AppImage replaces itself. the manifest names this build's asset and its sha-256; the
// download lands next to the AppImage (same filesystem, so the swap is one rename), is checked,
// then renamed over it, and a restart execs the new one. outside an AppImage there is nothing to
// replace: checks still run (Available()), installs don't
#include "imshell/updater.hpp"
#include "../native_view.hpp"

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <gtk/gtk.h>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace imshell::updater {
    namespace {
        namespace fs = std::filesystem;

        Config gConfig;
        std::string gAppImage; // $APPIMAGE: the file to replace
        std::mutex gMutex;     // guards the release fields
        std::string gLatest, gUrl, gSha256, gNotes;
        std::jthread gWorker;
        std::atomic<unsigned> gGeneration{0}; // bumped by Stop(): idle callbacks from before it drop
        std::atomic<bool> gBusy{false};
        std::atomic<size_t> gDone{0}, gTotal{0};
        GtkWidget* gProgress = nullptr;
        guint gProgressTimer = 0;

        // dotted numbers; anything that isn't a number counts as 0 ("1.2.0-beta" = 1.2.0)
        std::vector<int> parseVersion(const std::string& v) {
            std::vector<int> parts;
            for (size_t i = 0; i <= v.size();) {
                const size_t dot = std::min(v.find('.', i), v.size());
                int n = 0;
                std::from_chars(v.data() + i, v.data() + dot, n);
                parts.push_back(n);
                i = dot + 1;
            }
            return parts;
        }

        bool newer(const std::string& a, const std::string& b) {
            auto pa = parseVersion(a), pb = parseVersion(b);
            const size_t n = std::max(pa.size(), pb.size());
            pa.resize(n, 0);
            pb.resize(n, 0);
            return pa > pb;
        }

        GtkWindow* parentWindow() {
            auto* host = static_cast<GtkWidget*>(detail::nativeHost());
            return host ? GTK_WINDOW(gtk_widget_get_root(host)) : nullptr;
        }

        // a message with one optional action button; gtk 4.6 has no GtkAlertDialog
        void tell(const std::string& title, const std::string& detail,
                  const char* action = nullptr, std::function<void()> onAction = {}) {
            G_GNUC_BEGIN_IGNORE_DEPRECATIONS
            GtkWidget* dialog =
                gtk_message_dialog_new(parentWindow(), GTK_DIALOG_MODAL, GTK_MESSAGE_OTHER,
                                       GTK_BUTTONS_NONE, "%s", title.c_str());
            if (!detail.empty())
                gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s",
                                                         detail.c_str());
            gtk_dialog_add_button(GTK_DIALOG(dialog), action ? "Later" : "OK", GTK_RESPONSE_CANCEL);
            if (action) {
                gtk_dialog_add_button(GTK_DIALOG(dialog), action, GTK_RESPONSE_ACCEPT);
                gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
            }
            auto* callback = new std::function<void()>(std::move(onAction));
            g_signal_connect(dialog, "response",
                             G_CALLBACK(+[](GtkDialog* d, int response, gpointer data) {
                                 auto* fn = static_cast<std::function<void()>*>(data);
                                 gtk_window_destroy(GTK_WINDOW(d));
                                 if (response == GTK_RESPONSE_ACCEPT && *fn)
                                     (*fn)();
                                 delete fn;
                             }),
                             callback);
            gtk_window_present(GTK_WINDOW(dialog));
            G_GNUC_END_IGNORE_DEPRECATIONS
        }

        // runs fn on the gtk main loop, unless Stop() came first
        void onMain(std::function<void()> fn) {
            struct Job {
                unsigned generation;
                std::function<void()> fn;
            };
            g_idle_add(
                [](gpointer data) -> gboolean {
                    auto* job = static_cast<Job*>(data);
                    if (job->generation == gGeneration.load())
                        job->fn();
                    delete job;
                    return G_SOURCE_REMOVE;
                },
                new Job{gGeneration.load(), std::move(fn)});
        }

        std::string assetKey() {
            if (!gConfig.assetKey.empty())
                return gConfig.assetKey;
#if defined(__aarch64__)
            return "appimage-aarch64"; // no fallback: another arch's binary won't run
#else
            return "appimage-x86_64";
#endif
        }

        // splits "https://host/path" for httplib
        bool splitUrl(const std::string& url, std::string& origin, std::string& path) {
            const size_t scheme = url.find("://");
            const size_t slash = url.find('/', scheme == std::string::npos ? 0 : scheme + 3);
            if (scheme == std::string::npos || slash == std::string::npos)
                return false;
            origin = url.substr(0, slash);
            path = url.substr(slash);
            return true;
        }

        std::string lower(std::string s) {
            std::erase_if(s, [](unsigned char c) { return std::isspace(c); });
            std::ranges::transform(s, s.begin(), [](unsigned char c) { return std::tolower(c); });
            return s;
        }

        // the manifest: {"version", "release_notes", "downloads": {"<key>": {"url", "sha256"}}}
        bool fetchManifest(std::stop_token stop) {
            std::string origin, path;
            if (!splitUrl(gConfig.manifestUrl, origin, path))
                return false;
            httplib::Client client(origin);
            client.set_connection_timeout(10);
            client.set_read_timeout(15);
            client.set_follow_location(true);
            const auto res = client.Get(path.c_str());
            if (stop.stop_requested() || !res || res->status != 200) {
                std::fprintf(stderr, "imshell: update check failed (%d)\n", res ? res->status : 0);
                return false;
            }
            try {
                const auto data = nlohmann::json::parse(res->body);
                const auto& asset = data.at("downloads").at(assetKey());
                std::string version = data.value("version", "");
                std::string url = asset.value("url", "");
                std::string sha = lower(asset.value("sha256", ""));
                if (version.empty() || url.empty() || sha.size() != 64)
                    return false;
                std::lock_guard lock(gMutex);
                gLatest = std::move(version);
                gUrl = std::move(url);
                gSha256 = std::move(sha);
                gNotes = data.value("release_notes", "");
                return true;
            } catch (const std::exception& e) {
                std::fprintf(stderr, "imshell: bad update manifest: %s\n", e.what());
                return false;
            }
        }

        std::string sha256Of(const fs::path& file, std::stop_token stop) {
            std::ifstream in(file, std::ios::binary);
            GChecksum* sum = g_checksum_new(G_CHECKSUM_SHA256);
            std::vector<char> buffer(64 * 1024);
            while (in && !stop.stop_requested()) {
                in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                g_checksum_update(sum, reinterpret_cast<const guchar*>(buffer.data()), in.gcount());
            }
            std::string hex = in.eof() && !stop.stop_requested() ? g_checksum_get_string(sum) : "";
            g_checksum_free(sum);
            return hex;
        }

        // download next to the AppImage, verify, rename over it
        bool install(std::stop_token stop) {
            std::string url, sha;
            {
                std::lock_guard lock(gMutex);
                url = gUrl;
                sha = gSha256;
            }
            std::string origin, path;
            if (!splitUrl(url, origin, path))
                return false;
            const fs::path temp = gAppImage + ".update";
            std::ofstream out(temp, std::ios::binary);
            if (!out)
                return false;
            httplib::Client client(origin);
            client.set_connection_timeout(10);
            client.set_read_timeout(300);
            client.set_follow_location(true);
            gDone = 0;
            gTotal = 0;
            const auto res = client.Get(
                path.c_str(),
                [&](const char* data, size_t n) {
                    out.write(data, static_cast<std::streamsize>(n));
                    return !stop.stop_requested() && out.good();
                },
                [&](size_t done, size_t total) {
                    gDone = done;
                    gTotal = total;
                    return !stop.stop_requested();
                });
            out.close();
            std::error_code ec;
            if (stop.stop_requested() || !res || res->status != 200 || !out ||
                sha256Of(temp, stop) != sha) {
                std::fprintf(stderr, "imshell: update download failed or did not verify\n");
                fs::remove(temp, ec);
                return false;
            }
            fs::permissions(temp, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                            fs::perm_options::add, ec);
            fs::rename(temp, gAppImage, ec);
            if (ec) {
                fs::remove(temp, ec);
                return false;
            }
            return true;
        }

        void closeProgress() {
            if (gProgressTimer)
                g_source_remove(gProgressTimer);
            gProgressTimer = 0;
            if (gProgress)
                gtk_window_destroy(GTK_WINDOW(gtk_widget_get_root(gProgress)));
            gProgress = nullptr;
        }

        void showProgress() {
            GtkWidget* window = gtk_window_new();
            gtk_window_set_title(GTK_WINDOW(window), "Downloading Update");
            gtk_window_set_default_size(GTK_WINDOW(window), 350, -1);
            gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
            gtk_window_set_modal(GTK_WINDOW(window), TRUE);
            gtk_window_set_transient_for(GTK_WINDOW(window), parentWindow());
            g_signal_connect(window, "close-request",
                             G_CALLBACK(+[](GtkWindow*, gpointer) -> gboolean { return TRUE; }),
                             nullptr); // the download finishes or fails, it is not abandoned
            GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
            for (auto* set : {gtk_widget_set_margin_start, gtk_widget_set_margin_end,
                              gtk_widget_set_margin_top, gtk_widget_set_margin_bottom})
                set(box, 20);
            gtk_box_append(GTK_BOX(box), gtk_label_new("Downloading update, please wait..."));
            gProgress = gtk_progress_bar_new();
            gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(gProgress), TRUE);
            gtk_box_append(GTK_BOX(box), gProgress);
            gtk_window_set_child(GTK_WINDOW(window), box);
            gtk_window_present(GTK_WINDOW(window));
            gProgressTimer = g_timeout_add(
                100,
                [](gpointer) -> gboolean {
                    if (!gProgress)
                        return G_SOURCE_REMOVE;
                    const double done = static_cast<double>(gDone), total = static_cast<double>(gTotal);
                    if (total > 0) {
                        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(gProgress), done / total);
                        char text[64];
                        std::snprintf(text, sizeof text, "%.1f / %.1f MB", done / 1048576, total / 1048576);
                        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(gProgress), text);
                    } else {
                        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(gProgress));
                    }
                    return G_SOURCE_CONTINUE;
                },
                nullptr);
        }

        void restart() {
            execl(gAppImage.c_str(), gAppImage.c_str(), nullptr);
            tell("Restart Failed", "Please start the app again to use the new version.");
        }

        void startDownload() {
            if (gBusy.exchange(true))
                return;
            showProgress();
            gWorker = std::jthread([](std::stop_token stop) {
                const bool ok = install(stop);
                gBusy = false;
                onMain([ok] {
                    closeProgress();
                    if (ok)
                        tell("Update downloaded", "Restart to use the new version.", "Restart Now",
                             restart);
                    else
                        tell("Download Failed", "The update could not be downloaded. Please try again later.");
                });
            });
        }

        void startCheck(bool manual) {
            if (gConfig.manifestUrl.empty() || gBusy.exchange(true))
                return;
            gWorker = std::jthread([manual](std::stop_token stop) {
                const bool ok = fetchManifest(stop);
                gBusy = false;
                if (!manual)
                    return;
                onMain([ok] {
                    if (!ok)
                        return tell("Update Check Failed",
                                    "Could not check for updates. Please check your connection.");
                    if (!Available())
                        return tell("Up to Date", "You're running the latest version (" +
                                                      gConfig.version + ").");
                    std::string detail = "Current: " + gConfig.version + "\nLatest:  " + LatestVersion();
                    {
                        std::lock_guard lock(gMutex);
                        if (!gNotes.empty())
                            detail += "\n\n" + gNotes;
                    }
                    tell("A new version is available!", detail, "Download Update", startDownload);
                });
            });
        }
    } // namespace

    void Start(const Config& config) {
        gConfig = config;
        if (const char* appImage = std::getenv("APPIMAGE"))
            gAppImage = appImage;
        startCheck(false);
    }

    void Check() {
        if (gAppImage.empty())
            return tell("Update Not Available",
                        "Updates install by themselves only in the AppImage. Use your package "
                        "manager, or download the new version.");
        if (gProgress)
            return tell("Update In Progress", "Please wait for the current update to finish.");
        if (gBusy) // the silent check: its result shows in Available()
            return;
        startCheck(true);
    }

    bool Available() {
        std::lock_guard lock(gMutex);
        return !gLatest.empty() && newer(gLatest, gConfig.version);
    }

    std::string LatestVersion() {
        std::lock_guard lock(gMutex);
        return gLatest;
    }

    void Stop() {
        gGeneration++;
        gWorker = {}; // requests stop and joins
        closeProgress();
    }
} // namespace imshell::updater
