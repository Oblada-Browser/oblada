#include "oblada/browser/debug/window_controls_mac.h"

#import <AppKit/AppKit.h>

namespace oblada::debug {

std::vector<gfx::Rect> WindowControlBounds(gfx::NativeWindow native_window) {
  std::vector<gfx::Rect> bounds;
  NSWindow* window = native_window.GetNativeNSWindow();
  NSView* frame = window.contentView.superview;
  if (!frame) {
    return bounds;
  }
  for (NSWindowButton type :
       {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
    NSButton* button = [window standardWindowButton:type];
    if (!button || button.hidden) {
      continue;
    }
    NSRect rect = [button convertRect:button.bounds toView:frame];
    if (!frame.isFlipped) {
      rect.origin.y = NSHeight(frame.bounds) - NSMaxY(rect);
    }
    bounds.emplace_back(NSMinX(rect), NSMinY(rect), NSWidth(rect), NSHeight(rect));
  }
  return bounds;
}

}  // namespace oblada::debug
