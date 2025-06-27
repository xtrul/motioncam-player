#import <AppKit/AppKit.h>
#import <QuickLookUI/QLPreviewProvider.h>
#import <QuickLookUI/QLPreviewReply.h>
#import <QuickLook/QuickLook.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

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
            size_t startIndex = frames.size() > 24 ? 23 : 0;
            const size_t maxFrames = std::min<size_t>(30, frames.size() - startIndex);
            std::vector<std::vector<uint8_t>> framePixels;
            framePixels.resize(maxFrames);
            size_t width = 0;
            size_t height = 0;
            for(size_t i = 0; i < maxFrames; ++i) {
                std::vector<uint16_t> frame16;
                nlohmann::json meta;
                decoder.loadFrame(frames[startIndex + i], frame16, meta);
                if(i == 0) {
                    width = meta["width"];
                    height = meta["height"];
                }
                framePixels[i].resize(width * height * 4);
                for(size_t p = 0; p < width * height; ++p) {
                    uint8_t v = static_cast<uint8_t>(frame16[p] >> 8);
                    framePixels[i][4*p + 0] = v;
                    framePixels[i][4*p + 1] = v;
                    framePixels[i][4*p + 2] = v;
                    framePixels[i][4*p + 3] = 0xFF;
                }
            }

            NSString *tmpPath = [NSTemporaryDirectory() stringByAppendingPathComponent:[[NSUUID UUID] UUIDString]];
            tmpPath = [tmpPath stringByAppendingPathExtension:@"gif"];
            NSURL *tmpURL = [NSURL fileURLWithPath:tmpPath];
            CGImageDestinationRef dest = CGImageDestinationCreateWithURL((__bridge CFURLRef)tmpURL, kUTTypeGIF, maxFrames, NULL);

            NSDictionary *frameProps = @{(__bridge NSString*)kCGImagePropertyGIFDictionary: @{(__bridge NSString*)kCGImagePropertyGIFDelayTime: @(0.033)}};
            size_t bitsPerComponent = 8;
            size_t bytesPerRow = width * 4;
            CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
            for(size_t i = 0; i < maxFrames; ++i) {
                CGContextRef context = CGBitmapContextCreate(framePixels[i].data(), width, height, bitsPerComponent, bytesPerRow, colorSpace, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Little);
                CGImageRef image = CGBitmapContextCreateImage(context);
                CGContextRelease(context);
                CGImageDestinationAddImage(dest, image, (__bridge CFDictionaryRef)frameProps);
                CGImageRelease(image);
            }
            CGImageDestinationSetProperties(dest, (__bridge CFDictionaryRef)@{(__bridge NSString*)kCGImagePropertyGIFDictionary: @{(__bridge NSString*)kCGImagePropertyGIFLoopCount: @0}});
            CGImageDestinationFinalize(dest);
            CFRelease(dest);
            CGColorSpaceRelease(colorSpace);

            if (@available(macOS 12.0, *)) {
                QLPreviewReply *reply = [QLPreviewReply replyWithFileURL:tmpURL contentType:UTTypeGIF.identifier properties:@{}];
                handler(reply, nil);
            } else {
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
