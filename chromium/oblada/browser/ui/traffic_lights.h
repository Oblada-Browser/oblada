#ifndef OBLADA_BROWSER_UI_TRAFFIC_LIGHTS_H_
#define OBLADA_BROWSER_UI_TRAFFIC_LIGHTS_H_

#include "build/build_config.h"
#include "ui/gfx/geometry/vector2d.h"

namespace views {
class Widget;
}  // namespace views

namespace oblada::sidebar {

// Shows or hides the window's close, minimize and zoom buttons (macOS's traffic
// lights), which the compact sidebar hides until it has expanded, and moves them
// by `offset` from where the system puts them: into the compact sidebar's panel
// (sidebar_card.h). In full screen they're shown, where the system puts them.
#if BUILDFLAG(IS_MAC)
void SetWindowControls(views::Widget* widget,
                       bool visible,
                       const gfx::Vector2d& offset);
#else
inline void SetWindowControls(views::Widget* widget,
                              bool visible,
                              const gfx::Vector2d& offset) {}
#endif

}  // namespace oblada::sidebar

#endif  // OBLADA_BROWSER_UI_TRAFFIC_LIGHTS_H_
