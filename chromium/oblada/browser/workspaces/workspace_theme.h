#ifndef OBLADA_BROWSER_WORKSPACES_WORKSPACE_THEME_H_
#define OBLADA_BROWSER_WORKSPACES_WORKSPACE_THEME_H_

#include <optional>
#include <vector>

#include "base/containers/span.h"
#include "base/values.h"
#include "third_party/skia/include/core/SkColor.h"

namespace oblada::workspaces {

// How a window looks while it shows a workspace, as with Zen's and Arc's themes: up
// to three colors, a gradient over the window's glass, how much of the glass shows
// through them, and grain. The theme picker (theme_picker.h) edits it.
struct Theme {
  // Light or dark: the window's colors, and how light the theme's colors are.
  enum class Scheme { kAuto, kLight, kDark };
  // Where a color added in the picker goes, from the first one.
  enum class Harmony { kAnalogous, kComplementary, kTriadic };

  // A color, as the picker places it: the hue its angle (in degrees), the
  // saturation its distance from the middle (0 to 1); how light it is depends on
  // the scheme.
  struct Dot {
    double hue = 0;
    double distance = 0;
    bool operator==(const Dot&) const = default;
  };

  // The first leads: the window's other colors come from it. None: gray.
  std::vector<Dot> dots;
  Harmony harmony = Harmony::kAnalogous;
  Scheme scheme = Scheme::kAuto;
  // How much the colors cover the glass, from 0 (see-through) to 1; unset, the
  // window's default for light or dark.
  std::optional<double> opacity;
  // From 0 (none) to 1.
  double grain = 0;

  bool operator==(const Theme&) const = default;
};

inline constexpr size_t kMaxThemeDots = 3;
// Grain goes in steps, as the picker's dial turns: 0, 1/15... 1.
inline constexpr int kGrainSteps = 16;

// A workspace's color (a tab_groups::TabGroupColorId), as its dot and groups show it.
SkColor GroupColor(int color);

// The theme of a workspace from before themes: its color, or gray.
Theme ThemeFromGroupColor(int color, bool tints_window);

// What `dot` looks like in a light or dark window.
SkColor ColorOf(const Theme::Dot& dot, bool dark);
// Every color of `theme`, in order.
std::vector<SkColor> ColorsOf(const Theme& theme, bool dark);
// Whether a window with `theme` is dark, given whether the system is.
bool IsDark(const Theme& theme, bool system_dark);

// Moves the dots so that dot `index` is at `dot`, the others keeping where they are
// from it.
void MoveDot(Theme& theme, size_t index, const Theme::Dot& dot);
// Places the dots after the first one as the harmony has them.
void Harmonize(Theme& theme);
// Adds a dot as the harmony places it (or, the first, somewhere pleasant).
void AddDot(Theme& theme);

// The themes the picker offers, a page of eight at a time.
inline constexpr size_t kPresetsPerPage = 8;
base::span<const std::vector<Theme::Dot>> Presets();

base::DictValue ThemeToDict(const Theme& theme);
Theme ThemeFromDict(const base::DictValue& dict);

}  // namespace oblada::workspaces

#endif  // OBLADA_BROWSER_WORKSPACES_WORKSPACE_THEME_H_
