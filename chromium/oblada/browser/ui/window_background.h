#ifndef OBLADA_BROWSER_UI_WINDOW_BACKGROUND_H_
#define OBLADA_BROWSER_UI_WINDOW_BACKGROUND_H_

#include <optional>
#include <vector>

#include "build/build_config.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/native_ui_types.h"

#if BUILDFLAG(IS_MAC) && defined(__OBJC__)
@class NSView;
#endif

// What a browser window shows behind its sidebar and around its page, on its glass
// (the system's translucent material): the theme of its workspace
// (workspaces/workspace_theme.h), a gradient of its colors, as see-through as it
// says, with grain. The window's views are see-through there.
namespace oblada::window_background {

struct Background {
  // From the top left to the bottom right; none: the window's frame color.
  std::vector<SkColor> colors;
  // How much they cover the glass, from 0 to 1; unset: the window's default.
  std::optional<double> opacity;
  // From 0 (none) to 1.
  double grain = 0;
  // The glass's own look, dark or light; unset: the system's.
  std::optional<bool> dark;

  bool operator==(const Background&) const = default;
};

// Sets the background of a browser window, fading to it.
void Set(gfx::NativeWindow window, const Background& background);

// The background a window was last given.
Background Get(gfx::NativeWindow window);

// How much a window's colors cover its glass when its background doesn't say.
double DefaultOpacity(gfx::NativeWindow window);

// Grain at its strongest: how much its specks show.
inline constexpr float kMaxGrain = 0.5f;

// The specks of grain, a tile of light and dark ones, transparent between them:
// 256 pixels square, for 128 points on a Retina screen; the same every time.
const SkBitmap& GrainTile();

// A plain gray, dark or light, standing for the glass where it can't be: under
// snapshots of the window, or under its background shown over the page.
SkColor GlassStandIn(bool dark);

// What the background looks like in `window`, `size` pixels wide and high, on a
// gray standing for the glass: for snapshots of the window, whose compositor only
// has its views. Empty if the window has no background.
SkBitmap Render(gfx::NativeWindow window, const gfx::Size& size);

#if BUILDFLAG(IS_MAC) && defined(__OBJC__)
// For BrowserNativeWidgetMac: paints the view over the glass of its window with the
// window's background, its colors (or else `frame_color`) at its opacity (or else
// `default_opacity`).
void Paint(NSView* tint_view, SkColor frame_color, double default_opacity);
#endif

}  // namespace oblada::window_background

#endif  // OBLADA_BROWSER_UI_WINDOW_BACKGROUND_H_
