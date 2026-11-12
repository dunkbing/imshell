// notifications through ImShellActivity (NotificationManager, the api 33+ prompt). jni only on
// the glue thread, which is the ui thread: calls queue work and wake the loop, and the loop runs
// it with its pending answers and clicks (runNotifications, from pump). no badge on android
#include "imshell/android.hpp"
#include "imshell/frames.hpp"
#include "imshell/notifications.hpp"

#include <android_native_app_glue.h>
#include <jni.h>
#include <mutex>
#include <utility>
#include <vector>

using namespace imshell::notifications;

namespace {
    using Job = std::function<void(JNIEnv*, jobject)>;
    std::mutex gMutex;
    std::vector<Job> gJobs;                               // from any thread
    std::vector<std::function<void(Permission)>> gAsking; // waiting for the prompt; ui thread
    std::function<void(const std::string&)> gOnPress;
    std::vector<std::string> gPending; // clicks before OnPress

    void later(Job job) {
        {
            std::lock_guard lock(gMutex);
            gJobs.push_back(std::move(job));
        }
        imshell::requestFrames(); // wakes the loop
    }

    jmethodID method(JNIEnv* env, jobject activity, const char* name, const char* sig) {
        jclass cls = env->GetObjectClass(activity);
        jmethodID m = env->GetMethodID(cls, name, sig);
        env->DeleteLocalRef(cls);
        if (!m)
            env->ExceptionClear(); // not an ImShellActivity
        return m;
    }

    Permission permission(JNIEnv* env, jobject activity) {
        jmethodID m = method(env, activity, "notificationPermission", "()I");
        return m ? static_cast<Permission>(env->CallIntMethod(activity, m)) : Permission::Unavailable;
    }

    std::string text(JNIEnv* env, jstring s) {
        const char* utf = env->GetStringUTFChars(s, nullptr);
        std::string out = utf ? utf : "";
        env->ReleaseStringUTFChars(s, utf);
        return out;
    }

    void pressed(const std::string& id) {
        if (gOnPress)
            gOnPress(id);
        else
            gPending.push_back(id);
    }
} // namespace

namespace imshell::detail {
    // the loop, after its events
    void runNotifications() {
        android_app* app = AndroidApp();
        JNIEnv* env = nullptr;
        if (!app || app->activity->vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK)
            return;
        jobject activity = app->activity->clazz;
        std::vector<Job> jobs;
        {
            std::lock_guard lock(gMutex);
            jobs.swap(gJobs);
        }
        for (Job& job : jobs)
            job(env, activity);
        if (!gAsking.empty()) {
            static jmethodID poll = method(env, activity, "pollPermission", "()I");
            if (const int answer = poll ? env->CallIntMethod(activity, poll) : 3; answer >= 0)
                for (auto& done : std::exchange(gAsking, {}))
                    if (done)
                        done(static_cast<Permission>(answer));
        }
        static jmethodID pollPressed = method(env, activity, "pollPressed", "()Ljava/lang/String;");
        while (pollPressed) {
            auto s = static_cast<jstring>(env->CallObjectMethod(activity, pollPressed));
            if (!s)
                break;
            pressed(text(env, s));
            env->DeleteLocalRef(s);
            requestFrames();
        }
    }
} // namespace imshell::detail

namespace imshell::notifications {
    void GetPermission(std::function<void(Permission)> done) {
        later([done = std::move(done)](JNIEnv* env, jobject activity) {
            if (done)
                done(permission(env, activity));
        });
    }

    void RequestPermission(std::function<void(Permission)> done) {
        later([done = std::move(done)](JNIEnv* env, jobject activity) {
            jmethodID m = method(env, activity, "requestNotificationPermission", "()V");
            if (!m) {
                if (done)
                    done(Permission::Unavailable);
                return;
            }
            gAsking.push_back(done);
            env->CallVoidMethod(activity, m);
        });
    }

    void Show(const Notification& notification, std::function<void(bool)> done) {
        later([n = notification, done = std::move(done)](JNIEnv* env, jobject activity) {
            jmethodID m = method(env, activity, "showNotification",
                                 "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)Z");
            bool shown = false;
            if (m) {
                jstring id = env->NewStringUTF(n.id.c_str());
                jstring title = env->NewStringUTF(n.title.c_str());
                jstring body = env->NewStringUTF(n.body.c_str());
                shown = env->CallBooleanMethod(activity, m, id, title, body, static_cast<jboolean>(n.silent));
                if (env->ExceptionCheck()) { // a bad icon or channel: not shown, not a crash
                    env->ExceptionClear();
                    shown = false;
                }
                env->DeleteLocalRef(id);
                env->DeleteLocalRef(title);
                env->DeleteLocalRef(body);
            }
            if (done)
                done(shown);
        });
    }

    void Close(const std::string& id) {
        later([id](JNIEnv* env, jobject activity) {
            if (jmethodID m = method(env, activity, "cancelNotification", "(Ljava/lang/String;)V")) {
                jstring s = env->NewStringUTF(id.c_str());
                env->CallVoidMethod(activity, m, s);
                env->DeleteLocalRef(s);
            }
        });
    }

    void SetBadge(int) {} // launchers only show that notifications exist

    void OnPress(std::function<void(const std::string&)> handler) {
        gOnPress = std::move(handler);
        for (const std::string& id : std::exchange(gPending, {}))
            if (gOnPress)
                gOnPress(id);
    }
} // namespace imshell::notifications
