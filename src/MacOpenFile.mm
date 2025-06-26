#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#include "MacOpenFile.h"

static NSMutableArray<NSString*>* gPendingFiles = nil;

@interface MCOpenFileHandler : NSObject
- (void)handleOpenFiles:(NSAppleEventDescriptor*)event withReply:(NSAppleEventDescriptor*)reply;
@end

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
