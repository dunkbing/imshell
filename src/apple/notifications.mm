// the notification center (macos: a signed .app; a bare exe has no bundle id and gets
// Unavailable), and the badge: the dock tile on macos, the home screen icon on ios. the center
// answers on its own threads; answers go to the main queue
#include "imshell/notifications.hpp"
#include "imshell/frames.hpp"

#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#endif
#import <UserNotifications/UserNotifications.h>

#include <algorithm>
#include <utility>
#include <vector>

using namespace imshell::notifications;

namespace {
    std::function<void(const std::string&)> gOnPress;
    std::vector<std::string> gPending; // presses before OnPress

    void pressed(const std::string& id) {
        if (gOnPress)
            gOnPress(id);
        else
            gPending.push_back(id);
        imshell::requestFrames();
    }

    Permission permissionOf(UNAuthorizationStatus status) {
        switch (status) {
        case UNAuthorizationStatusNotDetermined:
            return Permission::Default;
        case UNAuthorizationStatusDenied:
            return Permission::Denied;
        default:
            return Permission::Granted; // authorized, provisional, ephemeral
        }
    }
} // namespace

// shows notifications while the app is in front too, and reports clicks
@interface ImShellNotificationDelegate : NSObject <UNUserNotificationCenterDelegate>
@end

@implementation ImShellNotificationDelegate
- (void)userNotificationCenter:(UNUserNotificationCenter*)center
       willPresentNotification:(UNNotification*)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler {
    UNNotificationPresentationOptions options = UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionList;
    if (notification.request.content.sound)
        options |= UNNotificationPresentationOptionSound;
    completionHandler(options);
}

- (void)userNotificationCenter:(UNUserNotificationCenter*)center
    didReceiveNotificationResponse:(UNNotificationResponse*)response
             withCompletionHandler:(void (^)(void))completionHandler {
    if ([response.actionIdentifier isEqualToString:UNNotificationDefaultActionIdentifier]) {
        std::string id = response.notification.request.identifier.UTF8String;
        dispatch_async(dispatch_get_main_queue(), ^{
          pressed(id);
        });
    }
    completionHandler();
}
@end

namespace {
    // null without a bundle id: the center would throw
    UNUserNotificationCenter* center() {
        static ImShellNotificationDelegate* delegate;
        if (!NSBundle.mainBundle.bundleIdentifier)
            return nil;
        UNUserNotificationCenter* c = UNUserNotificationCenter.currentNotificationCenter;
        static dispatch_once_t once;
        dispatch_once(&once, ^{
          delegate = [ImShellNotificationDelegate new];
          c.delegate = delegate; // weak in the center: the static keeps it
        });
        return c;
    }

    void onMain(dispatch_block_t block) {
        dispatch_async(dispatch_get_main_queue(), ^{
          block();
          imshell::requestFrames();
        });
    }
} // namespace

namespace imshell::notifications {
    void GetPermission(std::function<void(Permission)> done) {
        UNUserNotificationCenter* c = center();
        if (!c) {
            onMain(^{
              if (done)
                  done(Permission::Unavailable);
            });
            return;
        }
        [c getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings* settings) {
          const Permission p = permissionOf(settings.authorizationStatus);
          onMain(^{
            if (done)
                done(p);
          });
        }];
    }

    void RequestPermission(std::function<void(Permission)> done) {
        UNUserNotificationCenter* c = center();
        if (!c) {
            GetPermission(std::move(done));
            return;
        }
        const UNAuthorizationOptions options = UNAuthorizationOptionAlert | UNAuthorizationOptionSound | UNAuthorizationOptionBadge;
        [c requestAuthorizationWithOptions:options
                         completionHandler:^(BOOL, NSError*) {
                           GetPermission(done);
                         }];
    }

    void Show(const Notification& n, std::function<void(bool)> done) {
        UNUserNotificationCenter* c = center();
        if (!c) {
            onMain(^{
              if (done)
                  done(false);
            });
            return;
        }
        UNMutableNotificationContent* content = [UNMutableNotificationContent new];
        content.title = @(n.title.c_str());
        content.body = @(n.body.c_str());
        content.sound = n.silent ? nil : UNNotificationSound.defaultSound;
        UNNotificationRequest* request = [UNNotificationRequest requestWithIdentifier:@(n.id.c_str())
                                                                              content:content
                                                                              trigger:nil];
        [c addNotificationRequest:request
            withCompletionHandler:^(NSError* error) {
              const bool shown = error == nil;
              onMain(^{
                if (done)
                    done(shown);
              });
            }];
    }

    void Close(const std::string& id) {
        if (UNUserNotificationCenter* c = center()) {
            NSArray* ids = @[ @(id.c_str()) ];
            [c removeDeliveredNotificationsWithIdentifiers:ids];
            [c removePendingNotificationRequestsWithIdentifiers:ids];
        }
    }

    void SetBadge(int count) {
#if TARGET_OS_IPHONE
        // needs the badge permission, which RequestPermission asks for
        const NSInteger n = std::max(count, 0);
        if (@available(iOS 16, *)) {
            [center() setBadgeCount:n withCompletionHandler:nil];
        } else {
            dispatch_async(dispatch_get_main_queue(), ^{
              UIApplication.sharedApplication.applicationIconBadgeNumber = n;
            });
        }
#else
        dispatch_async(dispatch_get_main_queue(), ^{ // appkit: the main thread
          NSApp.dockTile.badgeLabel = count > 0 ? [NSString stringWithFormat:@"%d", count] : nil;
        });
#endif
    }

    void OnPress(std::function<void(const std::string&)> handler) {
        center(); // the delegate in place, for a click that launched the app
        gOnPress = std::move(handler);
        for (const std::string& id : std::exchange(gPending, {}))
            if (gOnPress)
                gOnPress(id);
    }
} // namespace imshell::notifications
