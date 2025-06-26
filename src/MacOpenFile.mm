#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#include "MacOpenFile.h"

static NSMutableArray<NSString*>* gPendingFiles = nil;

@interface MCOpenFileHandler : NSObject <NSApplicationDelegate>
@end

@implementation MCOpenFileHandler
- (void)application:(NSApplication*)sender openFiles:(NSArray<NSString*>*)filenames {
    if (!gPendingFiles) gPendingFiles = [[NSMutableArray alloc] init];
    [gPendingFiles addObjectsFromArray:filenames];
    [sender replyToOpenOrPrint:NSApplicationDelegateReplySuccess];
}
- (BOOL)application:(NSApplication*)app openFile:(NSString*)filename {
    if (!gPendingFiles) gPendingFiles = [[NSMutableArray alloc] init];
    [gPendingFiles addObject:filename];
    return YES;
}
@end

std::vector<std::string> GetStartupOpenFiles() {
    std::vector<std::string> result;
    @autoreleasepool {
        [NSApplication sharedApplication];
        static MCOpenFileHandler* handler = nil;
        if (!handler) {
            handler = [[MCOpenFileHandler alloc] init];
            [NSApp setDelegate:handler];
        }
        NSEvent* event;
        do {
            event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                         untilDate:[NSDate dateWithTimeIntervalSinceNow:0]
                                            inMode:NSDefaultRunLoopMode
                                           dequeue:YES];
            if (event) [NSApp sendEvent:event];
        } while (event);

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
