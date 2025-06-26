#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#include "MacOpenFile.h"

static NSMutableArray<NSString*>* gPendingFiles = nil;
static id<NSApplicationDelegate> gPrevDelegate = nil;

@interface MCOpenFileDelegate : NSObject <NSApplicationDelegate>
@end

@implementation MCOpenFileDelegate
- (void)application:(NSApplication*)sender openFiles:(NSArray<NSString*>*)filenames {
    if (!gPendingFiles) gPendingFiles = [[NSMutableArray alloc] init];
    [gPendingFiles addObjectsFromArray:filenames];
    [sender replyToOpenOrPrint:NSApplicationDelegateReplySuccess];
}
- (id)forwardingTargetForSelector:(SEL)aSelector {
    if (gPrevDelegate && [gPrevDelegate respondsToSelector:aSelector]) {
        return gPrevDelegate;
    }
    return [super forwardingTargetForSelector:aSelector];
}
@end

static void InstallDelegate() {
    NSApplication* app = [NSApplication sharedApplication];
    id<NSApplicationDelegate> current = [app delegate];
    if (![current isKindOfClass:[MCOpenFileDelegate class]]) {
        gPrevDelegate = current;
        MCOpenFileDelegate* delegate = [[MCOpenFileDelegate alloc] init];
        [app setDelegate:delegate];
    }
}

std::vector<std::string> GetStartupOpenFiles() {
    std::vector<std::string> result;
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        InstallDelegate();
        [app finishLaunching];
        NSDate* end = [NSDate dateWithTimeIntervalSinceNow:2.0];
        while ([end timeIntervalSinceNow] > 0 && (!gPendingFiles || [gPendingFiles count] == 0)) {
            NSEvent* event = [app nextEventMatchingMask:NSEventMaskAny
                                          untilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]
                                             inMode:NSDefaultRunLoopMode
                                            dequeue:YES];
            if (event) [app sendEvent:event];
        }

        if (gPendingFiles) {
            for (NSString* name in gPendingFiles) {
                result.emplace_back([name UTF8String]);
            }
            [gPendingFiles removeAllObjects];
        }
    }
    return result;
}

std::vector<std::string> GetPendingOpenFiles() {
    std::vector<std::string> result;
    @autoreleasepool {
        InstallDelegate();
        if (gPendingFiles) {
            for (NSString* name in gPendingFiles) {
                result.emplace_back([name UTF8String]);
            }
            [gPendingFiles removeAllObjects];
        }
    }
    return result;
}
#endif
