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
    using FilesDone = std::function<void(const std::vector<std::string>& paths)>;

    void OpenFileDialog(const std::vector<FileFilter>& filters, FileDone done,
                        const std::string& copyInto = {});
    // the same, for any number of files: done gets all the picked ones at once. where the system
    // picker takes one (ios, android), the list holds that one
    void OpenFilesDialog(const std::vector<FileFilter>& filters, FilesDone done,
                         const std::string& copyInto = {});
    void SaveFileDialog(const std::string& defaultName, const std::vector<FileFilter>& filters,
                        FileDone done);
    void PickFolderDialog(FileDone done);

    // the user-visible documents folder on ios and android (created); empty on desktops
    std::string DocumentsDir();
} // namespace imshell
