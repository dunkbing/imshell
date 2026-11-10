#pragma once

// android: NativeActivity runs the app through android_native_app_glue, which calls the
// host's android_main on its own thread. hand the glue's app over before App::create:
//
//   void android_main(android_app* state) {
//       mydear::SetAndroidApp(state);
//       mydear::App app;
//       if (app.create(config, delegate)) app.run();
//       app.destroy();
//   }
//
// the activity must be io.mydear.MydearActivity (android/io/mydear/MydearActivity.java) or a
// subclass: it brings the soft keyboard, typed characters, the clipboard and the file picker.
// the app's private folder is state->activity->internalDataPath
#include <android_native_app_glue.h> // android_main, with c linkage

#include <functional>
#include <string>

namespace mydear {
    void SetAndroidApp(android_app* app);
    android_app* AndroidApp();

    // the system document picker. the picked file is copied into `intoDir` (apps cannot open
    // another app's files by path) and `done` gets the copy's path on the ui thread, a frame
    // or two later; never when cancelled. mimeTypes: comma-separated, empty for any file
    void PickFile(const std::string& mimeTypes, const std::string& intoDir,
                  std::function<void(const std::string&)> done);
} // namespace mydear
