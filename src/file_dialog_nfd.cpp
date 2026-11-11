// desktop file dialogs: native file dialog extended (appkit, the windows shell, the xdg portal)
#include "imshell/file_dialog.hpp"

#include <cstdio>
#include <nfd.h>

namespace imshell {
    namespace {
        // nfd's own setup around each dialog: it can fail (no dbus session on linux), and a
        // failed init must not be followed by NFD_Quit
        template <class Show>
        void withNfd(const FileDone& done, Show show) {
            if (NFD_Init() != NFD_OKAY) {
                std::fprintf(stderr, "imshell: file dialogs unavailable: %s\n", NFD_GetError());
                return;
            }
            nfdchar_t* out = nullptr;
            const nfdresult_t result = show(&out);
            std::string path;
            if (result == NFD_OKAY) {
                path = out;
                NFD_FreePath(out);
            } else if (result != NFD_CANCEL) {
                std::fprintf(stderr, "imshell: file dialog: %s\n", NFD_GetError());
            }
            NFD_Quit();
            if (!path.empty())
                done(path);
        }

        std::vector<nfdfilteritem_t> items(const std::vector<FileFilter>& filters) {
            std::vector<nfdfilteritem_t> out;
            for (const FileFilter& f : filters)
                out.push_back({f.name.c_str(), f.extensions.c_str()});
            return out;
        }
    } // namespace

    void OpenFileDialog(const std::vector<FileFilter>& filters, FileDone done, const std::string&) {
        const auto list = items(filters);
        withNfd(done, [&](nfdchar_t** out) {
            return NFD_OpenDialog(out, list.data(), static_cast<nfdfiltersize_t>(list.size()),
                                  nullptr);
        });
    }

    void SaveFileDialog(const std::string& defaultName, const std::vector<FileFilter>& filters,
                        FileDone done) {
        const auto list = items(filters);
        withNfd(done, [&](nfdchar_t** out) {
            return NFD_SaveDialog(out, list.data(), static_cast<nfdfiltersize_t>(list.size()),
                                  nullptr, defaultName.empty() ? nullptr : defaultName.c_str());
        });
    }

    void PickFolderDialog(FileDone done) {
        withNfd(done, [](nfdchar_t** out) { return NFD_PickFolder(out, nullptr); });
    }

    std::string DocumentsDir() {
        return {};
    }
} // namespace imshell
