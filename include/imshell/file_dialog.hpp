#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

// the system's open, save and folder dialogs. done gets the chosen paths and is not called when
// the user cancels. ui thread only
//
//   imshell::OpenDialog({.filters = {{"CSV files", "csv,tsv"}, {"All files", "*"}}, .multiple = true},
//                       [](const std::vector<std::string>& paths) { for (auto& p : paths) open(p); });
//
// desktops (native file dialog): modal, done runs before the call returns. ios and android:
// the system document picker, done runs a moment later; another app's file is copied into
// copyInto (DocumentsDir() when empty), since it is not reachable by path. there is no save
// or folder dialog there: save names a free file in DocumentsDir() (the files app shows it on
// ios), and the folder is DocumentsDir() itself
namespace imshell {
    struct FileFilter {
        std::string name;       // "CSV files"
        std::string extensions; // "csv,tsv" without dots; "*" = any file
    };
    using FileDone = std::function<void(const std::string& path)>;
    using FilesDone = std::function<void(const std::vector<std::string>& paths)>;

    struct OpenOptions {
        std::vector<FileFilter> filters; // none: any file
        bool multiple = false;           // several files; done gets them all
        bool directory = false;          // a folder instead of files (DocumentsDir() on phones)
        std::string defaultPath;         // the folder it opens in (desktops); empty: the system's pick
        std::string copyInto;            // phones: where picked files are copied
    };

    void OpenDialog(const OpenOptions& options, FilesDone done);
    void SaveFileDialog(const std::string& defaultName, const std::vector<FileFilter>& filters,
                        FileDone done);

    // one file or one folder
    inline void OpenFileDialog(const std::vector<FileFilter>& filters, FileDone done,
                               const std::string& copyInto = {}) {
        OpenDialog({.filters = filters, .copyInto = copyInto},
                   [done = std::move(done)](const std::vector<std::string>& paths) { done(paths.front()); });
    }
    inline void PickFolderDialog(FileDone done) {
        OpenDialog({.directory = true},
                   [done = std::move(done)](const std::vector<std::string>& paths) { done(paths.front()); });
    }

    // the user-visible documents folder on ios and android (created); empty on desktops
    std::string DocumentsDir();
} // namespace imshell
