#include "oblada/browser/ui/traffic_lights.h"

#import <AppKit/AppKit.h>

#include "ui/gfx/native_ui_types.h"
#include "ui/views/widget/widget.h"

namespace oblada::sidebar {

void SetWindowControlsVisible(views::Widget* widget, bool visible) {
  NSWindow* window = widget ? widget->GetNativeWindow().GetNativeNSWindow() : nil;
  if (!window) {
    return;
  }
  // In full screen they come with the menu bar, when the pointer reaches the top.
  if (window.styleMask & NSWindowStyleMaskFullScreen) {
    visible = true;
  }
  for (NSWindowButton type :
       {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
    NSButton* button = [window standardWindowButton:type];
    if (button && button.hidden == visible) {
      button.hidden = !visible;
    }
  }
}

}  // namespace oblada::sidebar
