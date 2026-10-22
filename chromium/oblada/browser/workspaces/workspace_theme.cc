#include "oblada/browser/workspaces/workspace_theme.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>

#include "base/no_destructor.h"
#include "components/tab_groups/tab_group_color.h"
#include "ui/gfx/color_utils.h"

namespace oblada::workspaces {

namespace {

// How light and how saturated (at the picker's edge) the colors are, in a dark and
// in a light window: deep in one, pastel in the other, for the window's text to
// read on them either way.
constexpr double kDarkLightness = 0.34;
constexpr double kDarkSaturation = 0.62;
constexpr double kLightLightness = 0.80;
constexpr double kLightSaturation = 0.80;

// The first color, for a theme that had none.
constexpr Theme::Dot kFirstDot = {.hue = 215, .distance = 0.6};

double NormalizeHue(double hue) {
  hue = std::fmod(hue, 360);
  return hue < 0 ? hue + 360 : hue;
}

// Where the dots after the first go, from it, by harmony and number of dots.
std::vector<double> HueOffsets(Theme::Harmony harmony, size_t count) {
  switch (harmony) {
    case Theme::Harmony::kAnalogous:
      return count == 2 ? std::vector<double>{0, 40}
                        : std::vector<double>{0, 35, -35};
    case Theme::Harmony::kComplementary:
      return count == 2 ? std::vector<double>{0, 180}
                        : std::vector<double>{0, 150, 210};
    case Theme::Harmony::kTriadic:
      return count == 2 ? std::vector<double>{0, 120}
                        : std::vector<double>{0, 120, 240};
  }
}

constexpr std::array<std::pair<Theme::Harmony, std::string_view>, 3> kHarmonies = {{
    {Theme::Harmony::kAnalogous, "analogous"},
    {Theme::Harmony::kComplementary, "complementary"},
    {Theme::Harmony::kTriadic, "triadic"},
}};

constexpr std::array<std::pair<Theme::Scheme, std::string_view>, 3> kSchemes = {{
    {Theme::Scheme::kAuto, "auto"},
    {Theme::Scheme::kLight, "light"},
    {Theme::Scheme::kDark, "dark"},
}};

}  // namespace

SkColor GroupColor(int color) {
  switch (static_cast<tab_groups::TabGroupColorId>(color)) {
    case tab_groups::TabGroupColorId::kBlue:
      return SkColorSetRGB(0x8A, 0xB4, 0xF8);
    case tab_groups::TabGroupColorId::kRed:
      return SkColorSetRGB(0xF2, 0x8B, 0x82);
    case tab_groups::TabGroupColorId::kYellow:
      return SkColorSetRGB(0xFD, 0xD6, 0x63);
    case tab_groups::TabGroupColorId::kGreen:
      return SkColorSetRGB(0x81, 0xC9, 0x95);
    case tab_groups::TabGroupColorId::kPink:
      return SkColorSetRGB(0xFF, 0x8B, 0xCB);
    case tab_groups::TabGroupColorId::kPurple:
      return SkColorSetRGB(0xC5, 0x8A, 0xF9);
    case tab_groups::TabGroupColorId::kCyan:
      return SkColorSetRGB(0x78, 0xD9, 0xEC);
    case tab_groups::TabGroupColorId::kOrange:
      return SkColorSetRGB(0xFC, 0xAD, 0x70);
    default:
      return SkColorSetRGB(0x9A, 0xA0, 0xA6);
  }
}

Theme ThemeFromGroupColor(int color, bool tints_window) {
  Theme theme;
  if (tints_window &&
      static_cast<tab_groups::TabGroupColorId>(color) !=
          tab_groups::TabGroupColorId::kGrey) {
    color_utils::HSL hsl;
    color_utils::SkColorToHSL(GroupColor(color), &hsl);
    theme.dots.push_back({.hue = hsl.h * 360, .distance = kFirstDot.distance});
  }
  return theme;
}

SkColor ColorOf(const Theme::Dot& dot, bool dark) {
  const double distance = std::clamp(dot.distance, 0.0, 1.0);
  color_utils::HSL hsl;
  hsl.h = NormalizeHue(dot.hue) / 360;
  hsl.s = distance * (dark ? kDarkSaturation : kLightSaturation);
  hsl.l = dark ? kDarkLightness : kLightLightness;
  return color_utils::HSLToSkColor(hsl, SK_AlphaOPAQUE);
}

std::vector<SkColor> ColorsOf(const Theme& theme, bool dark) {
  std::vector<SkColor> colors;
  for (const Theme::Dot& dot : theme.dots) {
    colors.push_back(ColorOf(dot, dark));
  }
  return colors;
}

bool IsDark(const Theme& theme, bool system_dark) {
  switch (theme.scheme) {
    case Theme::Scheme::kAuto:
      return system_dark;
    case Theme::Scheme::kLight:
      return false;
    case Theme::Scheme::kDark:
      return true;
  }
}

void MoveDot(Theme& theme, size_t index, const Theme::Dot& dot) {
  if (index >= theme.dots.size()) {
    return;
  }
  const double hue = dot.hue - theme.dots[index].hue;
  const double distance = dot.distance - theme.dots[index].distance;
  for (Theme::Dot& each : theme.dots) {
    each.hue = NormalizeHue(each.hue + hue);
    each.distance = std::clamp(each.distance + distance, 0.0, 1.0);
  }
  theme.dots[index] = {.hue = NormalizeHue(dot.hue),
                       .distance = std::clamp(dot.distance, 0.0, 1.0)};
}

void Harmonize(Theme& theme) {
  if (theme.dots.size() < 2) {
    return;
  }
  const std::vector<double> offsets = HueOffsets(theme.harmony, theme.dots.size());
  for (size_t i = 1; i < theme.dots.size(); ++i) {
    theme.dots[i] = {.hue = NormalizeHue(theme.dots[0].hue + offsets[i]),
                     .distance = theme.dots[0].distance};
  }
}

void AddDot(Theme& theme) {
  if (theme.dots.size() >= kMaxThemeDots) {
    return;
  }
  if (theme.dots.empty()) {
    theme.dots.push_back(kFirstDot);
    return;
  }
  theme.dots.push_back(theme.dots[0]);
  Harmonize(theme);
}

base::span<const std::vector<Theme::Dot>> Presets() {
  using Dots = std::vector<Theme::Dot>;
  static const base::NoDestructor<std::vector<Dots>> presets({
      // Soft.
      Dots{{48, 0.35}},
      Dots{{340, 0.5}},
      Dots{{285, 0.45}},
      Dots{{355, 0.6}},
      Dots{{18, 0.7}},
      Dots{{46, 0.65}},
      Dots{{150, 0.55}},
      Dots{{228, 0.45}},
      // Vivid.
      Dots{{0, 0.95}},
      Dots{{28, 0.95}},
      Dots{{52, 0.9}},
      Dots{{125, 0.8}},
      Dots{{172, 0.85}},
      Dots{{205, 0.9}},
      Dots{{250, 0.85}},
      Dots{{305, 0.9}},
      // Two colors.
      Dots{{330, 0.6}, {25, 0.65}},
      Dots{{200, 0.7}, {265, 0.65}},
      Dots{{145, 0.6}, {195, 0.6}},
      Dots{{40, 0.7}, {350, 0.6}},
      Dots{{270, 0.55}, {320, 0.6}},
      Dots{{185, 0.5}, {95, 0.5}},
      Dots{{15, 0.7}, {55, 0.7}},
      Dots{{235, 0.6}, {175, 0.5}},
      // Three colors.
      Dots{{200, 0.7}, {255, 0.6}, {310, 0.6}},
      Dots{{15, 0.75}, {45, 0.7}, {340, 0.6}},
      Dots{{140, 0.6}, {180, 0.6}, {215, 0.6}},
      Dots{{275, 0.6}, {320, 0.6}, {10, 0.6}},
      Dots{{55, 0.65}, {95, 0.5}, {150, 0.5}},
      // Procida's houses and its sea.
      Dots{{20, 0.7}, {50, 0.75}, {195, 0.55}},
      Dots{{345, 0.55}, {300, 0.45}, {235, 0.5}},
      Dots{{170, 0.6}, {120, 0.5}, {60, 0.55}},
  });
  return *presets;
}

base::DictValue ThemeToDict(const Theme& theme) {
  base::ListValue dots;
  for (const Theme::Dot& dot : theme.dots) {
    dots.Append(base::ListValue().Append(dot.hue).Append(dot.distance));
  }
  base::DictValue dict;
  dict.Set("dots", std::move(dots));
  for (const auto& [harmony, name] : kHarmonies) {
    if (harmony == theme.harmony) {
      dict.Set("harmony", name);
    }
  }
  for (const auto& [scheme, name] : kSchemes) {
    if (scheme == theme.scheme) {
      dict.Set("scheme", name);
    }
  }
  if (theme.opacity) {
    dict.Set("opacity", *theme.opacity);
  }
  dict.Set("grain", theme.grain);
  return dict;
}

Theme ThemeFromDict(const base::DictValue& dict) {
  Theme theme;
  if (const base::ListValue* dots = dict.FindList("dots")) {
    for (const base::Value& value : *dots) {
      const base::ListValue* dot = value.GetIfList();
      if (!dot || dot->size() != 2 || theme.dots.size() >= kMaxThemeDots) {
        continue;
      }
      const std::optional<double> hue = (*dot)[0].GetIfDouble();
      const std::optional<double> distance = (*dot)[1].GetIfDouble();
      if (hue && distance) {
        theme.dots.push_back({.hue = NormalizeHue(*hue),
                              .distance = std::clamp(*distance, 0.0, 1.0)});
      }
    }
  }
  if (const std::string* harmony = dict.FindString("harmony")) {
    for (const auto& [value, name] : kHarmonies) {
      if (*harmony == name) {
        theme.harmony = value;
      }
    }
  }
  if (const std::string* scheme = dict.FindString("scheme")) {
    for (const auto& [value, name] : kSchemes) {
      if (*scheme == name) {
        theme.scheme = value;
      }
    }
  }
  if (std::optional<double> opacity = dict.FindDouble("opacity")) {
    theme.opacity = std::clamp(*opacity, 0.0, 1.0);
  }
  theme.grain = std::clamp(dict.FindDouble("grain").value_or(0), 0.0, 1.0);
  return theme;
}

}  // namespace oblada::workspaces
