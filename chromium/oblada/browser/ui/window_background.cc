#include "oblada/browser/ui/window_background.h"

#include <cstdint>
#include <cstdlib>

#include "base/no_destructor.h"
#include "third_party/skia/include/core/SkColor.h"

namespace oblada::window_background {

const SkBitmap& GrainTile() {
  static const base::NoDestructor<SkBitmap> tile([] {
    constexpr int kPixels = 256;
    SkBitmap bitmap;
    bitmap.allocN32Pixels(kPixels, kPixels);
    // The same specks every time (xorshift).
    uint32_t seed = 0x9E3779B9;
    for (int y = 0; y < kPixels; ++y) {
      for (int x = 0; x < kPixels; ++x) {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        const int value = seed & 0xFF;
        // Lighter or darker than the colors under it, more so further from the
        // middle.
        const U8CPU alpha = static_cast<U8CPU>(std::abs(value - 128) * 2 * 0.6);
        const U8CPU channel = value >= 128 ? 0xFF : 0;
        *bitmap.getAddr32(x, y) =
            SkPreMultiplyARGB(alpha, channel, channel, channel);
      }
    }
    bitmap.setImmutable();
    return bitmap;
  }());
  return *tile;
}

SkColor GlassStandIn(bool dark) {
  return dark ? SkColorSetRGB(0x2A, 0x2A, 0x2C) : SkColorSetRGB(0xE8, 0xE8, 0xEA);
}

}  // namespace oblada::window_background
