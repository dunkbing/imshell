// sparkle reads its feed (SUFeedURL) and key (SUPublicEDKey) from Info.plist, checks on its
// schedule and shows its own ui
#include "imshell/updater.hpp"

#import <Sparkle/Sparkle.h>

namespace imshell::updater {
    namespace {
        SPUStandardUpdaterController* gController = nil;
    }

    void Start(const Config&) {
        if (gController)
            return;
        NSString* key = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"SUPublicEDKey"];
        if (key.length == 0) { // sparkle refuses to start without it: say why once
            NSLog(@"imshell: SUPublicEDKey not set, no updates");
            return;
        }
        gController = [[SPUStandardUpdaterController alloc] initWithStartingUpdater:YES
                                                                    updaterDelegate:nil
                                                                 userDriverDelegate:nil];
    }

    void Check() {
        [gController checkForUpdates:nil];
    }

    bool Available() {
        return false;
    }

    std::string LatestVersion() {
        return {};
    }

    void Stop() {}
} // namespace imshell::updater
