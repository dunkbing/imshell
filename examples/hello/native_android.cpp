// the hello example on android: no native views yet, and android_main instead of main
#include "mydear/android.hpp"
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

<<<<<<< HEAD
// mydear::Run keeps the app across a recreated activity
=======
// shortcut: main() owns its App, so hello does not survive the activity being recreated (an
// overlay or theme change); hosts follow the pattern in mydear/android.hpp
>>>>>>> origin/main
void android_main(android_app* state) {
    mydear::SetAndroidApp(state);
    main();
}
<<<<<<< HEAD

void closeLikeUser() {}
=======
>>>>>>> origin/main
