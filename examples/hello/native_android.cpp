// the hello example on android: no native views yet, and android_main instead of main
#include "imshell/android.hpp"
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

int main();

// imshell::Run keeps the app across a recreated activity
void android_main(android_app* state) {
    imshell::SetAndroidApp(state);
    main();
}

bool closeLikeUser() {
    return false; // shortcut: no user-like close here yet
}
