#ifndef OBLADA_BROWSER_DEBUG_WINDOW_CONTROLS_MAC_H_
#define OBLADA_BROWSER_DEBUG_WINDOW_CONTROLS_MAC_H_

#include <vector>

#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_ui_types.h"

namespace oblada::debug {

// Where the window's close, minimize and zoom buttons are, in window coordinates:
// AppKit draws them, so they aren't in snapshots of the window's compositor.
std::vector<gfx::Rect> WindowControlBounds(gfx::NativeWindow window);

}  // namespace oblada::debug

#endif  // OBLADA_BROWSER_DEBUG_WINDOW_CONTROLS_MAC_H_
