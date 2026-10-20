#ifndef OBLADA_BROWSER_UI_ICONS_H_
#define OBLADA_BROWSER_UI_ICONS_H_

#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr_exclusion.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/models/image_model.h"

namespace gfx {
struct VectorIcon;
}  // namespace gfx

namespace views {
class View;
}  // namespace views

// The flat icons workspaces and containers can have (besides their color), as
// Zen and Firefox's containers do: Chromium's own Material icons.
namespace oblada::icons {

struct Icon {
  std::string_view id;
  // Not a raw_ptr: the icons are in a constexpr table.
  RAW_PTR_EXCLUSION const gfx::VectorIcon* vector;
};

// Every one, in the order a picker shows them (by theme, eight a row).
base::span<const Icon> All();

// The icon `id` is, if any: one of All(), or a name for one of them (Firefox's
// container icons, e.g. "briefcase", and Oblada's older ones).
const gfx::VectorIcon* Find(std::string_view id);
// The id among All() that `id` is ("" for none, e.g. Firefox's "circle", which
// is a container's dot).
std::string_view Canonical(std::string_view id);
// Its name among Firefox's container icons, for add-ons ("circle" if none).
std::string_view FirefoxName(std::string_view id);

// `id`, `size` wide, in `color`; empty if there's no such icon (for a dot).
ui::ImageModel Image(std::string_view id, SkColor color, int size);
// `id` as a data: URL of a PNG, black on transparent and twice `size` wide (for
// pages, as a CSS mask in any color); empty if there's no such icon.
std::string MaskUrl(std::string_view id, int size);

// A color a picker offers.
struct PickerColor {
  std::string id;
  SkColor color;
};

// Opens a bubble next to `anchor` to pick an icon among All() (or none), and,
// with `colors`, a color: picks are passed on as they're made, the icons shown
// in the color picked. It goes once the user clicks elsewhere.
void ShowPicker(views::View* anchor,
                std::string icon,
                std::vector<PickerColor> colors,
                std::string color,
                base::RepeatingCallback<void(const std::string&)> on_icon,
                base::RepeatingCallback<void(const std::string&)> on_color);

}  // namespace oblada::icons

#endif  // OBLADA_BROWSER_UI_ICONS_H_
