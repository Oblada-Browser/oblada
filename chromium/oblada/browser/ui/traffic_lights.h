#ifndef OBLADA_BROWSER_UI_TRAFFIC_LIGHTS_H_
#define OBLADA_BROWSER_UI_TRAFFIC_LIGHTS_H_

#include "build/build_config.h"

namespace views {
class Widget;
}  // namespace views

namespace oblada::sidebar {

// Shows or hides the window's close, minimize and zoom buttons (macOS's traffic
// lights), which the compact sidebar hides until it expands.
#if BUILDFLAG(IS_MAC)
void SetWindowControlsVisible(views::Widget* widget, bool visible);
#else
inline void SetWindowControlsVisible(views::Widget* widget, bool visible) {}
#endif

}  // namespace oblada::sidebar

#endif  // OBLADA_BROWSER_UI_TRAFFIC_LIGHTS_H_
