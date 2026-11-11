#pragma once

// android: NativeActivity runs the app through android_native_app_glue, which calls the
// host's android_main on its own thread. hand the glue's app over, then run as on the desktop:
//
//   void android_main(android_app* state) {
//       imshell::SetAndroidApp(state);
//       static MyApp delegate; // outlives a recreated activity
//       imshell::Run(config, delegate);
//   }
//
// the activity must be io.imshell.ImShellActivity (android/io/imshell/ImShellActivity.java) or a
// subclass: it brings the soft keyboard, typed characters, the clipboard and the file picker.
// the app's private folder is state->activity->internalDataPath (DataDir is inside it)
#include <android_native_app_glue.h> // android_main, with c linkage

#include <functional>
#include <string>

namespace imshell {
    void SetAndroidApp(android_app* app);
    android_app* AndroidApp();

    // after App::run() returns: true when the activity was destroyed only to be recreated (a
    // configuration change the manifest's configChanges cannot absorb, such as a system theme
    // or overlay change). return from android_main without tearing anything down: the next
    // activity calls android_main again, and SetAndroidApp re-attaches the running shell, so
    // skip the host's setup and go straight to run()
    bool AndroidActivityRecreating();

    // the system document picker. the picked file is copied into `intoDir` (apps cannot open
    // another app's files by path) and `done` gets the copy's path on the ui thread, a frame
    // or two later; never when cancelled. mimeTypes: comma-separated, empty for any file
    void PickFile(const std::string& mimeTypes, const std::string& intoDir,
                  std::function<void(const std::string&)> done);
} // namespace imshell
