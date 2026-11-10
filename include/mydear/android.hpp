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
// subclass: it brings the soft keyboard, typed characters and the clipboard
#include <android_native_app_glue.h> // android_main, with c linkage

namespace mydear {
    void SetAndroidApp(android_app* app);
    android_app* AndroidApp();
    // the app's private files folder (Context.getFilesDir)
    const char* AndroidFilesDir();
} // namespace mydear
