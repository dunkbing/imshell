// the hello example in a browser: no native views, and the page cannot be closed like a window
#include <cstdio>

void* makeTextField() {
    return nullptr;
}

void* makeDatePicker() {
    return nullptr;
}

void describeNativeView(void*, char* out, int size) {
    std::snprintf(out, size, "none");
}

bool closeLikeUser() {
    return false;
}
