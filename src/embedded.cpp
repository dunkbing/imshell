#include "imshell/embedded.hpp"
#include <cstring>

namespace {
    template <typename T> const T* find(const T* items, const size_t count, const char* name) {
        for (size_t i = 0; items && name && i < count; ++i)
            if (std::strcmp(items[i].name, name) == 0)
                return &items[i];
        return nullptr;
    }
} // namespace

const EmbeddedFont* findEmbeddedFont(const char* name) {
    return find(getEmbeddedFonts(), getEmbeddedFontCount(), name);
}

const EmbeddedImage* findEmbeddedImage(const char* name) {
    return find(getEmbeddedImages(), getEmbeddedImageCount(), name);
}
