#if __has_include(<QuickLookUI/QLPreviewProvider.h>)
#import <QuickLookUI/QuickLookUI.h>
#import <QuickLookUI/QLPreviewProvider.h>
#import <QuickLookUI/QLPreviewReply.h>
#elif __has_include(<QuickLook/QLPreviewProvider.h>)
#import <QuickLook/QuickLook.h>
#import <QuickLook/QLPreviewProvider.h>
#import <QuickLook/QLPreviewReply.h>
#else
#error "Required Quick Look headers not found"
#endif
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <motioncam/Decoder.hpp>
#include <nlohmann/json.hpp>

API_AVAILABLE(macos(12.0))
@interface MCPreviewProvider : QLPreviewProvider
@end

@implementation MCPreviewProvider

- (void)providePreviewForFileAtURL:(NSURL *)url completionHandler:(void (^)(QLPreviewReply *, NSError *))handler API_AVAILABLE(macos(12.0)) {
    @autoreleasepool {
        try {
            motioncam::Decoder decoder([[url path] UTF8String]);
            const auto& frames = decoder.getFrames();
            if(frames.empty()) {
                NSError *err = [NSError errorWithDomain:@"MCPreview" code:1 userInfo:@{NSLocalizedDescriptionKey:@"No frames"}];
                handler(nil, err);
                return;
            }
            std::vector<uint16_t> data;
            nlohmann::json metadata;
            decoder.loadFrame(frames[0], data, metadata);
            unsigned width = metadata["width"];
            unsigned height = metadata["height"];

            size_t bitsPerComponent = 16;
            size_t bytesPerRow = width * sizeof(uint16_t);
            CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceGenericGrayGamma2_2);
            CGContextRef context = CGBitmapContextCreate(data.data(), width, height, bitsPerComponent, bytesPerRow, colorSpace, kCGImageAlphaNone);
            CGColorSpaceRelease(colorSpace);
            CGImageRef cgImage = CGBitmapContextCreateImage(context);
            CGContextRelease(context);
            if (@available(macOS 12.0, *)) {
                QLPreviewReply *reply = [QLPreviewReply replyWithImage:cgImage properties:@{}];
                CGImageRelease(cgImage);
                handler(reply, nil);
            } else {
                CGImageRelease(cgImage);
                NSError *err = [NSError errorWithDomain:@"MCPreview" code:2 userInfo:@{NSLocalizedDescriptionKey:@"Requires macOS 12"}];
                handler(nil, err);
            }
        } catch (const std::exception& e) {
            NSString *desc = [NSString stringWithUTF8String:e.what()];
            NSError *error = [NSError errorWithDomain:@"MCPreview" code:1 userInfo:@{NSLocalizedDescriptionKey:desc}];
            handler(nil, error);
        }
    }
}

@end
