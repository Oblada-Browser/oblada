#include "oblada/browser/import/app_icon.h"

#import <AppKit/AppKit.h>

#include "base/apple/foundation_util.h"
#include "base/base64.h"
#include "base/strings/sys_string_conversions.h"

namespace oblada::import {

namespace {

// Points: drawn at twice that, for Retina screens.
constexpr CGFloat kSize = 64;

}  // namespace

std::string AppIconDataUrl(const std::string& app_id) {
  NSURL* app = [NSWorkspace.sharedWorkspace
      URLForApplicationWithBundleIdentifier:base::SysUTF8ToNSString(app_id)];
  if (!app) {
    return std::string();
  }
  NSImage* icon = [NSWorkspace.sharedWorkspace iconForFile:app.path];
  NSBitmapImageRep* bitmap = [[NSBitmapImageRep alloc]
      initWithBitmapDataPlanes:nullptr
                    pixelsWide:kSize * 2
                    pixelsHigh:kSize * 2
                 bitsPerSample:8
               samplesPerPixel:4
                      hasAlpha:YES
                      isPlanar:NO
                colorSpaceName:NSDeviceRGBColorSpace
                   bytesPerRow:0
                  bitsPerPixel:0];
  bitmap.size = NSMakeSize(kSize, kSize);
  [NSGraphicsContext saveGraphicsState];
  NSGraphicsContext.currentContext =
      [NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];
  [icon drawInRect:NSMakeRect(0, 0, kSize, kSize)];
  [NSGraphicsContext restoreGraphicsState];
  NSData* png = [bitmap representationUsingType:NSBitmapImageFileTypePNG
                                     properties:@{}];
  if (!png) {
    return std::string();
  }
  return "data:image/png;base64," +
         base::Base64Encode(base::apple::NSDataToSpan(png));
}

}  // namespace oblada::import
