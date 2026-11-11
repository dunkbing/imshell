#include "imshell/clipboard.hpp"

#include <algorithm>
#include <cstdlib>

namespace imshell::Clipboard {
    std::string Item::file() const {
        // the first line of the list, when it is a file:// url; %xx decoded
        std::string uri = get("text/uri-list");
        uri = uri.substr(0, uri.find_first_of("\r\n"));
        if (!uri.starts_with("file://")) {
            return "";
        }
        const size_t slash = uri.find('/', 7); // past an optional host
        std::string path;
        for (size_t i = slash; i < uri.size(); ++i) {
            if (uri[i] == '%' && i + 2 < uri.size()) {
                path += static_cast<char>(std::strtol(uri.substr(i + 1, 2).c_str(), nullptr, 16));
                i += 2;
            } else {
                path += uri[i];
            }
        }
        if (path.size() > 1 && path.back() == '/') {
            path.pop_back(); // a folder's url ends in one
        }
#ifdef _WIN32
        if (path.size() > 2 && path[2] == ':') {
            path.erase(0, 1); // /C:/a -> C:\a
        }
        std::replace(path.begin(), path.end(), '/', '\\');
#endif
        return slash == std::string::npos ? "" : path;
    }
} // namespace imshell::Clipboard
