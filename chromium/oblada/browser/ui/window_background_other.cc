#include "oblada/browser/ui/window_background.h"

namespace oblada::window_background {

// Only macOS windows have glass for now.
void Set(gfx::NativeWindow window, const Background& background) {}

Background Get(gfx::NativeWindow window) {
  return Background();
}

double DefaultOpacity(gfx::NativeWindow window) {
  return 1;
}

SkBitmap Render(gfx::NativeWindow window, const gfx::Size& size) {
  return SkBitmap();
}

}  // namespace oblada::window_background
