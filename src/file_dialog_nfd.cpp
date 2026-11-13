// desktop file dialogs: native file dialog extended (appkit, the windows shell, the xdg portal)
#include "imshell/file_dialog.hpp"

#include <cstdio>
#include <nfd.h>

namespace imshell {
    namespace {
        // nfd's own setup around each dialog: it can fail (no dbus session on linux), and a
        // failed init must not be followed by NFD_Quit
        template <class Show>
        void withNfd(Show show) {
            if (NFD_Init() != NFD_OKAY) {
                std::fprintf(stderr, "imshell: file dialogs unavailable: %s\n", NFD_GetError());
                return;
            }
            show();
            NFD_Quit();
        }

        bool failed(nfdresult_t result) {
            if (result == NFD_ERROR)
                std::fprintf(stderr, "imshell: file dialog: %s\n", NFD_GetError());
            return result != NFD_OKAY;
        }

        // windows and linux list "All files" themselves, so a "*" entry is left out. macos has
        // no list: every type goes into one allowed set, where "*" would match nothing, so any
        // "*" means no filter at all there
        std::vector<nfdfilteritem_t> items(const std::vector<FileFilter>& filters) {
            std::vector<nfdfilteritem_t> out;
            for (const FileFilter& f : filters) {
                if (f.extensions == "*") {
#ifdef __APPLE__
                    return {};
#else
                    continue;
#endif
                }
                out.push_back({f.name.c_str(), f.extensions.c_str()});
            }
            return out;
        }
    } // namespace

    void OpenDialog(const OpenOptions& options, FilesDone done) {
        std::vector<std::string> paths;
        withNfd([&] {
            const auto list = items(options.filters);
            const nfdchar_t* from = options.defaultPath.empty() ? nullptr : options.defaultPath.c_str();
            if (options.multiple) {
                const nfdpathset_t* set = nullptr;
                const nfdresult_t result =
                    options.directory
                        ? NFD_PickFolderMultiple(&set, from)
                        : NFD_OpenDialogMultiple(&set, list.data(),
                                                 static_cast<nfdfiltersize_t>(list.size()), from);
                if (failed(result))
                    return;
                nfdpathsetsize_t count = 0;
                NFD_PathSet_GetCount(set, &count);
                for (nfdpathsetsize_t i = 0; i < count; ++i) {
                    nfdchar_t* path = nullptr;
                    if (NFD_PathSet_GetPath(set, i, &path) == NFD_OKAY) {
                        paths.emplace_back(path);
                        NFD_PathSet_FreePath(path);
                    }
                }
                NFD_PathSet_Free(set);
            } else {
                nfdchar_t* path = nullptr;
                const nfdresult_t result =
                    options.directory
                        ? NFD_PickFolder(&path, from)
                        : NFD_OpenDialog(&path, list.data(), static_cast<nfdfiltersize_t>(list.size()), from);
                if (failed(result))
                    return;
                paths.emplace_back(path);
                NFD_FreePath(path);
            }
        });
        if (!paths.empty())
            done(paths);
    }

    void SaveFileDialog(const std::string& defaultName, const std::vector<FileFilter>& filters,
                        FileDone done) {
        std::string out;
        withNfd([&] {
            const auto list = items(filters);
            nfdchar_t* path = nullptr;
            if (failed(NFD_SaveDialog(&path, list.data(), static_cast<nfdfiltersize_t>(list.size()),
                                      nullptr, defaultName.empty() ? nullptr : defaultName.c_str())))
                return;
            out = path;
            NFD_FreePath(path);
        });
        if (!out.empty())
            done(out);
    }

    std::string DocumentsDir() {
        return {};
    }
} // namespace imshell
