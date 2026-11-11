#pragma once

#include <functional>
#include <string>
#include <vector>

// the system's open, save and folder dialogs. done gets the chosen path and is not called when
// the user cancels. ui thread only
//
//   imshell::OpenFileDialog({{"CSV files", "csv,tsv"}, {"All files", "*"}},
//                          [](const std::string& path) { open(path); });
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

    void OpenFileDialog(const std::vector<FileFilter>& filters, FileDone done,
                        const std::string& copyInto = {});
    void SaveFileDialog(const std::string& defaultName, const std::vector<FileFilter>& filters,
                        FileDone done);
    void PickFolderDialog(FileDone done);

    // the user-visible documents folder on ios and android (created); empty on desktops
    std::string DocumentsDir();
} // namespace imshell
