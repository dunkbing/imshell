// android file dialogs: the system document picker (PickFile copies the picks in); saves and
// folders are the documents folder in the app's internal storage
#include "../activity.hpp"
#include "imshell/android.hpp"
#include "imshell/file_dialog.hpp"

#include <algorithm>
#include <filesystem>
#include <string_view>

namespace imshell {
    namespace {
        // shortcut: a fixed table, any file for the rest; ask java's MimeTypeMap when an app
        // needs more types
        std::string mimeTypes(const std::vector<FileFilter>& filters) {
            static const std::pair<std::string_view, std::string_view> known[] = {
                {"csv", "text/csv,text/comma-separated-values,text/plain"},
                {"tsv", "text/tab-separated-values,text/plain"},
                {"txt", "text/plain"},
                {"sql", "text/plain,application/sql"},
                {"json", "application/json,text/plain"},
                {"png", "image/png"},
                {"jpg", "image/jpeg"},
                {"jpeg", "image/jpeg"},
                {"pdf", "application/pdf"},
            };
            std::string out;
            for (const FileFilter& f : filters) {
                std::string_view exts = f.extensions;
                while (!exts.empty()) {
                    const size_t comma = exts.find(',');
                    const std::string_view ext = exts.substr(0, comma);
                    exts = comma == exts.npos ? std::string_view() : exts.substr(comma + 1);
                    const auto* hit = std::find_if(std::begin(known), std::end(known),
                                                   [&](const auto& k) { return k.first == ext; });
                    if (hit == std::end(known))
                        return ""; // "*" or an unknown type: any file
                    out += (out.empty() ? "" : ",") + std::string(hit->second);
                }
            }
            return out;
        }

        // "name.ext", or "name 2.ext" when taken
        std::string freePath(const std::string& name) {
            namespace fs = std::filesystem;
            const fs::path base = fs::path(DocumentsDir()) / name;
            fs::path p = base;
            std::error_code ec;
            for (int i = 2; fs::exists(p, ec); ++i)
                p = base.parent_path() /
                    (base.stem().string() + " " + std::to_string(i) + base.extension().string());
            return p.string();
        }
    } // namespace

    std::string DocumentsDir() {
        const std::string files = detail::androidFilesDir();
        if (files.empty())
            return {};
        const auto dir = std::filesystem::path(files) / "Documents";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir.string();
    }

    // shortcut: the copy is what the app opens; edits do not reach the picked original
    void OpenDialog(const OpenOptions& options, FilesDone done) {
        if (options.directory) // no folder picker: the documents folder
            return done({DocumentsDir()});
        PickFile(mimeTypes(options.filters),
                 options.copyInto.empty() ? DocumentsDir() : options.copyInto, options.multiple,
                 std::move(done));
    }

    void SaveFileDialog(const std::string& defaultName, const std::vector<FileFilter>&,
                        FileDone done) {
        done(freePath(defaultName.empty() ? "Untitled" : defaultName));
    }
} // namespace imshell
