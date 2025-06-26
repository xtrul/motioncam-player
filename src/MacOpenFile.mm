#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#include "MacOpenFile.h"

static NSMutableArray<NSString*>* gPendingFiles = nil;

@interface MCOpenFileHandler : NSObject
- (void)handleOpenFiles:(NSAppleEventDescriptor*)event withReply:(NSAppleEventDescriptor*)reply;
@end

-@implementation MCOpenFileHandler
+@implementation MCOpenFileHandler
- (void)handleOpenFiles:(NSAppleEventDescriptor*)event withReply:(NSAppleEventDescriptor*)reply {
    NSAppleEventDescriptor* list = [event paramDescriptorForKeyword:keyDirectObject];
    for (NSInteger i = 1; i <= [list numberOfItems]; ++i) {
        NSString* path = [[list descriptorAtIndex:i] stringValue];
        if (!path) {
            NSURL* url = [[list descriptorAtIndex:i] fileURLValue];
            if (url) path = [url path];
        }
        if (path) {
            if (!gPendingFiles) gPendingFiles = [[NSMutableArray alloc] init];
            [gPendingFiles addObject:path];
        }
    }
}
@end

static void InstallHandlerOnce() {
    static bool installed = false;
    if (!installed) {
        installed = true;
        MCOpenFileHandler* handler = [[MCOpenFileHandler alloc] init];
        NSAppleEventManager* em = [NSAppleEventManager sharedAppleEventManager];
        [em setEventHandler:handler
                andSelector:@selector(handleOpenFiles:withReply:)
                forEventClass:kCoreEventClass
                   eventID:kAEOpenDocuments];
    }
}

std::vector<std::string> GetStartupOpenFiles() {
    std::vector<std::string> result;
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        InstallHandlerOnce();
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
#endif
