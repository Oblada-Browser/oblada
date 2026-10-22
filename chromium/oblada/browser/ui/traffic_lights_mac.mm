#include "oblada/browser/ui/traffic_lights.h"

#import <AppKit/AppKit.h>
#include <objc/runtime.h>

#include "ui/gfx/native_ui_types.h"
#include "ui/views/widget/widget.h"

namespace oblada::sidebar {

namespace {

// For each button, where the system put it, and where it was last moved to.
char kSystemOriginKey;
char kMovedOriginKey;

// Moves `button` by `offset` (y down) from where the system lays it out: where it
// is now, unless that's where it was last moved to.
void Move(NSButton* button, const gfx::Vector2d& offset) {
  const NSPoint origin = button.frame.origin;
  NSValue* system = objc_getAssociatedObject(button, &kSystemOriginKey);
  NSValue* moved = objc_getAssociatedObject(button, &kMovedOriginKey);
  if (!system || !moved || !NSEqualPoints(moved.pointValue, origin)) {
    system = [NSValue valueWithPoint:origin];
    objc_setAssociatedObject(button, &kSystemOriginKey, system,
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  }
  NSPoint target = system.pointValue;
  const bool right_to_left = button.window.windowTitlebarLayoutDirection ==
                             NSUserInterfaceLayoutDirectionRightToLeft;
  target.x += right_to_left ? -offset.x() : offset.x();
  target.y += button.superview.isFlipped ? offset.y() : -offset.y();
  if (!NSEqualPoints(origin, target)) {
    [button setFrameOrigin:target];
  }
  objc_setAssociatedObject(button, &kMovedOriginKey,
                           [NSValue valueWithPoint:target],
                           OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

}  // namespace

void SetWindowControls(views::Widget* widget,
                       bool visible,
                       const gfx::Vector2d& offset) {
  NSWindow* window = widget ? widget->GetNativeWindow().GetNativeNSWindow() : nil;
  if (!window) {
    return;
  }
  // In full screen they come with the menu bar, when the pointer reaches the top.
  const bool full_screen = window.styleMask & NSWindowStyleMaskFullScreen;
  if (full_screen) {
    visible = true;
  }
  for (NSWindowButton type :
       {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
    NSButton* button = [window standardWindowButton:type];
    if (!button) {
      continue;
    }
    // In place before they show, not where the system had them a moment.
    Move(button, full_screen ? gfx::Vector2d() : offset);
    if (button.hidden == visible) {
      button.hidden = !visible;
    }
  }
}

}  // namespace oblada::sidebar
