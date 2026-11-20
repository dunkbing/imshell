// file dialogs on a desktop build made with IMSHELL_FILE_DIALOGS=OFF (no nativefiledialog-extended):
// nothing opens and done is never called, the same as the user cancelling
#include "imshell/file_dialog.hpp"

namespace imshell {
    void OpenDialog(const OpenOptions&, FilesDone) {}
    void SaveFileDialog(const std::string&, const std::vector<FileFilter>&, FileDone) {}
    std::string DocumentsDir() { return {}; }
} // namespace imshell
