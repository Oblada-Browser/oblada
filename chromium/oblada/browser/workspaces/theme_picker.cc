#include "oblada/browser/workspaces/theme_picker.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "oblada/browser/l10n.h"
#include "oblada/browser/ui/window_background.h"
#include "oblada/browser/workspaces/workspace_switcher.h"
#include "oblada/browser/workspaces/workspace_theme.h"
#include "oblada/browser/workspaces/workspaces.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/vector2d_f.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace oblada::workspaces {

namespace {

// The picker's width, inside its margins.
constexpr int kWidth = 320;
// The field the colors are placed on.
constexpr int kFieldHeight = 300;
constexpr float kFieldCornerRadius = 14;
// Between the dots of the field's grid.
constexpr int kGridSpacing = 16;
// The circle of hues comes this close to the field's sides.
constexpr int kFieldMargin = 26;
constexpr float kFirstDotRadius = 11;
constexpr float kDotRadius = 8;
// The buttons at the field's top and bottom.
constexpr int kToolSize = 30;
constexpr int kToolGap = 20;
constexpr int kToolInset = 10;
// The colors ready to pick.
constexpr int kSwatchSize = 26;
constexpr int kArrowWidth = 22;
// The wave (opacity) and the dial (grain).
constexpr int kControlsHeight = 64;
constexpr int kDialSize = 64;
constexpr float kWavePadding = 12;
constexpr float kWaveAmplitude = 7;
constexpr float kWavePeriod = 22;
// How much a key moves the wave.
constexpr double kOpacityStep = 0.05;

constexpr double kPi = std::numbers::pi;

// The first view of `root` (it included) of class `class_name`.
views::View* FindView(views::View* root, std::string_view class_name) {
  if (root->GetClassName() == class_name) {
    return root;
  }
  for (views::View* child : root->children()) {
    if (views::View* found = FindView(child, class_name)) {
      return found;
    }
  }
  return nullptr;
}

SkColor TextColor(const views::View* view) {
  const ui::ColorProvider* provider = view->GetColorProvider();
  return provider ? provider->GetColor(ui::kColorLabelForeground) : SK_ColorBLACK;
}

// The theme being picked, a workspace's or the default one (`id` unset), saved as it
// changes: the windows that show it take it at once.
class Editor {
 public:
  Editor(BrowserWindowInterface* browser,
         std::optional<std::string> id,
         Theme theme,
         gfx::NativeWindow window)
      : browser_(browser),
        id_(std::move(id)),
        window_(window),
        theme_(std::move(theme)) {}
  Editor(const Editor&) = delete;
  Editor& operator=(const Editor&) = delete;

  const Theme& theme() const { return theme_; }

  // Whether the window is dark with the theme, and so are its colors.
  bool dark() const { return IsThemeDark(browser_, theme_); }

  // How much the colors cover the window's glass, as the theme says or by default.
  double opacity() const {
    return theme_.opacity.value_or(window_background::DefaultOpacity(window_));
  }

  void Set(Theme theme) {
    theme_ = std::move(theme);
    Profile* profile = browser_->GetProfile();
    if (!id_) {
      SetDefaultTheme(profile, theme_);
    } else if (std::optional<Workspace> workspace = FindWorkspace(profile, *id_)) {
      workspace->theme = theme_;
      UpdateWorkspace(profile, *workspace);
    }
    changed_.Notify();
  }

  base::CallbackListSubscription OnChanged(base::RepeatingClosure callback) {
    return changed_.Add(std::move(callback));
  }

 private:
  raw_ptr<BrowserWindowInterface> browser_;
  const std::optional<std::string> id_;
  const gfx::NativeWindow window_;
  Theme theme_;
  base::RepeatingClosureList changed_;
};

enum class Glyph { kAuto, kLight, kDark, kAdd, kRemove, kHarmony, kPrevious, kNext };

// The picker's small drawings, centered on `center`.
void PaintGlyph(gfx::Canvas* canvas,
                Glyph glyph,
                const gfx::PointF& center,
                SkColor color) {
  cc::PaintFlags fill;
  fill.setAntiAlias(true);
  fill.setColor(color);
  cc::PaintFlags stroke = fill;
  stroke.setStyle(cc::PaintFlags::kStroke_Style);
  stroke.setStrokeWidth(1.8f);
  stroke.setStrokeCap(cc::PaintFlags::kRound_Cap);
  stroke.setStrokeJoin(cc::PaintFlags::kRound_Join);
  switch (glyph) {
    case Glyph::kAuto: {
      // Sparkles: four-pointed stars, a big one and a small one.
      auto star = [&](const gfx::PointF& at, float radius) {
        SkPathBuilder path;
        path.moveTo(at.x(), at.y() - radius);
        path.quadTo(at.x(), at.y(), at.x() + radius, at.y());
        path.quadTo(at.x(), at.y(), at.x(), at.y() + radius);
        path.quadTo(at.x(), at.y(), at.x() - radius, at.y());
        path.quadTo(at.x(), at.y(), at.x(), at.y() - radius);
        canvas->DrawPath(path.detach(), fill);
      };
      star(center + gfx::Vector2dF(1.5f, 1.5f), 7.5f);
      star(center + gfx::Vector2dF(-5.5f, -5.5f), 3.5f);
      break;
    }
    case Glyph::kLight:
      // The sun.
      canvas->DrawCircle(center, 3.6f, fill);
      for (int i = 0; i < 8; ++i) {
        const double angle = i * kPi / 4;
        const gfx::Vector2dF ray(std::cos(angle), std::sin(angle));
        canvas->DrawLine(center + gfx::ScaleVector2d(ray, 6.2f),
                         center + gfx::ScaleVector2d(ray, 8.4f), stroke);
      }
      break;
    case Glyph::kDark: {
      // The moon: a disc with a bite out of it.
      canvas->Save();
      SkPathBuilder bite;
      bite.addCircle(center.x() + 4, center.y() - 3.5f, 6);
      canvas->sk_canvas()->clipPath(bite.detach(), SkClipOp::kDifference,
                                    /*antialias=*/true);
      canvas->DrawCircle(center, 7.5f, fill);
      canvas->Restore();
      break;
    }
    case Glyph::kAdd:
      canvas->DrawLine(center + gfx::Vector2dF(-6, 0),
                       center + gfx::Vector2dF(6, 0), stroke);
      canvas->DrawLine(center + gfx::Vector2dF(0, -6),
                       center + gfx::Vector2dF(0, 6), stroke);
      break;
    case Glyph::kRemove:
      canvas->DrawLine(center + gfx::Vector2dF(-6, 0),
                       center + gfx::Vector2dF(6, 0), stroke);
      break;
    case Glyph::kHarmony:
      // Colors that go together.
      stroke.setStrokeWidth(1.5f);
      canvas->DrawCircle(center + gfx::Vector2dF(0, -4), 3, stroke);
      canvas->DrawCircle(center + gfx::Vector2dF(-4.5f, 3.5f), 3, stroke);
      canvas->DrawCircle(center + gfx::Vector2dF(4.5f, 3.5f), 3, stroke);
      break;
    case Glyph::kPrevious:
    case Glyph::kNext: {
      const float side = glyph == Glyph::kNext ? 1 : -1;
      SkPathBuilder chevron;
      chevron.moveTo(center.x() - 2.5f * side, center.y() - 6);
      chevron.lineTo(center.x() + 2.5f * side, center.y());
      chevron.lineTo(center.x() - 2.5f * side, center.y() + 6);
      canvas->DrawPath(chevron.detach(), stroke);
      break;
    }
  }
}

// A button drawn as a glyph, on a highlight while hovered or selected.
class ThemeToolButton : public views::Button {
  METADATA_HEADER(ThemeToolButton, views::Button)

 public:
  ThemeToolButton(Glyph glyph, std::u16string name, PressedCallback callback)
      : views::Button(std::move(callback)), glyph_(glyph) {
    SetTooltipText(name);
    GetViewAccessibility().SetName(name);
    SetPreferredSize(gfx::Size(kToolSize, kToolSize));
  }

  void SetSelected(bool selected) {
    if (selected_ != selected) {
      selected_ = selected;
      SchedulePaint();
    }
  }

  // views::Button:
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const SkColor text = TextColor(this);
    const gfx::RectF bounds(GetLocalBounds());
    if (selected_ ||
        (GetEnabled() && (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED))) {
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(SkColorSetA(text, selected_ ? 0x30 : 0x18));
      canvas->DrawRoundRect(bounds, 8, flags);
    }
    PaintGlyph(canvas, glyph_, bounds.CenterPoint(),
               SkColorSetA(text, GetEnabled() ? 0xFF : 0x4D));
  }

 private:
  const Glyph glyph_;
  bool selected_ = false;
};

BEGIN_METADATA(ThemeToolButton)
END_METADATA

// The field the colors are placed on, a circle of hues around its middle, paler
// towards it: a click places the first color, or moves it there, and a drag moves
// a color, the others along with it. The scheme's buttons are at its top, the
// colors' at its bottom.
class ThemeField : public views::View {
  METADATA_HEADER(ThemeField, views::View)

 public:
  explicit ThemeField(Editor* editor) : editor_(editor) {
    SetPreferredSize(gfx::Size(kWidth, kFieldHeight));
    GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    GetViewAccessibility().SetName(L10n("theme.colors"));
    const struct {
      Glyph glyph;
      const char* name;
      Theme::Scheme scheme;
    } kSchemes[] = {
        {Glyph::kAuto, "theme.auto", Theme::Scheme::kAuto},
        {Glyph::kLight, "theme.light", Theme::Scheme::kLight},
        {Glyph::kDark, "theme.dark", Theme::Scheme::kDark},
    };
    for (const auto& scheme : kSchemes) {
      schemes_.push_back(
          {AddChildView(std::make_unique<ThemeToolButton>(
               scheme.glyph, L10n(scheme.name),
               base::BindRepeating(&ThemeField::SetScheme, base::Unretained(this),
                                   scheme.scheme))),
           scheme.scheme});
    }
    add_ = AddChildView(std::make_unique<ThemeToolButton>(
        Glyph::kAdd, L10n("theme.add"),
        base::BindRepeating(&ThemeField::Add, base::Unretained(this))));
    remove_ = AddChildView(std::make_unique<ThemeToolButton>(
        Glyph::kRemove, L10n("theme.remove"),
        base::BindRepeating(&ThemeField::Remove, base::Unretained(this))));
    harmony_ = AddChildView(std::make_unique<ThemeToolButton>(
        Glyph::kHarmony, L10n("theme.harmony"),
        base::BindRepeating(&ThemeField::NextHarmony, base::Unretained(this))));
    subscription_ = editor_->OnChanged(
        base::BindRepeating(&ThemeField::Refresh, base::Unretained(this)));
    Refresh();
  }

  // views::View:
  void Layout(PassKey) override {
    // Each row of buttons in the middle.
    auto row = [this](const std::vector<views::View*>& buttons, int y) {
      const int count = static_cast<int>(buttons.size());
      int x = (width() - count * kToolSize - (count - 1) * kToolGap) / 2;
      for (views::View* button : buttons) {
        button->SetBounds(x, y, kToolSize, kToolSize);
        x += kToolSize + kToolGap;
      }
    };
    std::vector<views::View*> schemes;
    for (const auto& [button, scheme] : schemes_) {
      schemes.push_back(button);
    }
    row(schemes, kToolInset);
    row({add_, remove_, harmony_}, height() - kToolInset - kToolSize);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const SkColor text = TextColor(this);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(SkColorSetA(text, 0x0D));
    canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), kFieldCornerRadius, flags);
    // A grid of dots, in the middle.
    flags.setColor(SkColorSetA(text, 0x2B));
    const float left = (width() % kGridSpacing + kGridSpacing) / 2.f;
    const float top = (height() % kGridSpacing + kGridSpacing) / 2.f;
    for (float y = top; y < height(); y += kGridSpacing) {
      for (float x = left; x < width(); x += kGridSpacing) {
        canvas->DrawCircle(gfx::PointF(x, y), 1, flags);
      }
    }
    const Theme& theme = editor_->theme();
    if (theme.dots.empty()) {
      canvas->DrawStringRectWithFlags(
          L10n("theme.add_hint"),
          gfx::FontList().DeriveWithSizeDelta(2).DeriveWithWeight(
              gfx::Font::Weight::SEMIBOLD),
          text, GetLocalBounds(), gfx::Canvas::TEXT_ALIGN_CENTER);
      return;
    }
    // The first color on top.
    const bool dark = editor_->dark();
    for (size_t i = theme.dots.size(); i-- > 0;) {
      const gfx::PointF at = PointOf(theme.dots[i]);
      const float radius = i == 0 ? kFirstDotRadius : kDotRadius;
      flags.setColor(SkColorSetA(SK_ColorBLACK, 0x40));
      canvas->DrawCircle(at + gfx::Vector2dF(0, 1), radius + 3.5f, flags);
      flags.setColor(SK_ColorWHITE);
      canvas->DrawCircle(at, radius + 2.5f, flags);
      flags.setColor(ColorOf(theme.dots[i], dark));
      canvas->DrawCircle(at, radius, flags);
    }
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (!event.IsOnlyLeftMouseButton()) {
      return false;
    }
    const Theme& theme = editor_->theme();
    dragging_.reset();
    for (size_t i = 0; i < theme.dots.size(); ++i) {
      if ((PointOf(theme.dots[i]) - event.location_f()).Length() <=
          kFirstDotRadius + 4) {
        dragging_ = i;
        break;
      }
    }
    if (!dragging_) {
      Theme changed = theme;
      if (changed.dots.empty()) {
        changed.dots.push_back(DotAt(event.location_f()));
      } else {
        MoveDot(changed, 0, DotAt(event.location_f()));
      }
      dragging_ = 0;
      editor_->Set(std::move(changed));
    }
    return true;
  }

  bool OnMouseDragged(const ui::MouseEvent& event) override {
    if (dragging_) {
      Theme changed = editor_->theme();
      MoveDot(changed, *dragging_, DotAt(event.location_f()));
      editor_->Set(std::move(changed));
    }
    return true;
  }

  void OnMouseReleased(const ui::MouseEvent& event) override {
    dragging_.reset();
  }

  void OnMouseCaptureLost() override { dragging_.reset(); }

 private:
  gfx::PointF Center() const {
    return gfx::RectF(GetLocalBounds()).CenterPoint();
  }

  float Radius() const {
    return std::min(width(), height()) / 2.f - kFieldMargin;
  }

  // Hues go clockwise from the top: red, yellow, green, cyan, blue, magenta.
  gfx::PointF PointOf(const Theme::Dot& dot) const {
    const double angle = dot.hue * kPi / 180;
    const double distance = dot.distance * Radius();
    return Center() + gfx::Vector2dF(std::sin(angle) * distance,
                                     -std::cos(angle) * distance);
  }

  Theme::Dot DotAt(const gfx::PointF& point) const {
    const gfx::Vector2dF offset = point - Center();
    const double hue = std::atan2(offset.x(), -offset.y()) * 180 / kPi;
    return {.hue = hue < 0 ? hue + 360 : hue,
            .distance = std::min(1.0, static_cast<double>(offset.Length() / Radius()))};
  }

  void Refresh() {
    const Theme& theme = editor_->theme();
    for (auto& [button, scheme] : schemes_) {
      button->SetSelected(theme.scheme == scheme);
    }
    add_->SetEnabled(theme.dots.size() < kMaxThemeDots);
    remove_->SetEnabled(!theme.dots.empty());
    harmony_->SetEnabled(theme.dots.size() > 1);
    SchedulePaint();
  }

  void SetScheme(Theme::Scheme scheme) {
    Theme changed = editor_->theme();
    changed.scheme = scheme;
    editor_->Set(std::move(changed));
  }

  void Add() {
    Theme changed = editor_->theme();
    AddDot(changed);
    editor_->Set(std::move(changed));
  }

  void Remove() {
    Theme changed = editor_->theme();
    if (!changed.dots.empty()) {
      changed.dots.pop_back();
    }
    editor_->Set(std::move(changed));
  }

  // Analogous, complementary, triadic, and round again.
  void NextHarmony() {
    Theme changed = editor_->theme();
    switch (changed.harmony) {
      case Theme::Harmony::kAnalogous:
        changed.harmony = Theme::Harmony::kComplementary;
        break;
      case Theme::Harmony::kComplementary:
        changed.harmony = Theme::Harmony::kTriadic;
        break;
      case Theme::Harmony::kTriadic:
        changed.harmony = Theme::Harmony::kAnalogous;
        break;
    }
    Harmonize(changed);
    editor_->Set(std::move(changed));
  }

  raw_ptr<Editor> editor_;
  std::vector<std::pair<raw_ptr<ThemeToolButton>, Theme::Scheme>> schemes_;
  raw_ptr<ThemeToolButton> add_ = nullptr;
  raw_ptr<ThemeToolButton> remove_ = nullptr;
  raw_ptr<ThemeToolButton> harmony_ = nullptr;
  // The dot being dragged.
  std::optional<size_t> dragging_;
  base::CallbackListSubscription subscription_;
};

BEGIN_METADATA(ThemeField)
END_METADATA

// A theme ready to pick, its colors in a circle.
class ThemeSwatch : public views::Button {
  METADATA_HEADER(ThemeSwatch, views::Button)

 public:
  ThemeSwatch(Editor* editor, PressedCallback callback)
      : views::Button(std::move(callback)), editor_(editor) {
    SetPreferredSize(gfx::Size(kSwatchSize + 8, kSwatchSize + 8));
  }

  void SetDots(std::vector<Theme::Dot> dots, std::u16string name) {
    dots_ = std::move(dots);
    GetViewAccessibility().SetName(name);
    SchedulePaint();
  }

  const std::vector<Theme::Dot>& dots() const { return dots_; }

  // views::Button:
  void PaintButtonContents(gfx::Canvas* canvas) override {
    if (dots_.empty()) {
      return;
    }
    const gfx::PointF center = gfx::RectF(GetLocalBounds()).CenterPoint();
    const float radius = kSwatchSize / 2.f;
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    // A ring around the one picked, and the one under the pointer.
    const bool picked = editor_->theme().dots == dots_;
    if (picked || GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) {
      flags.setStyle(cc::PaintFlags::kStroke_Style);
      flags.setStrokeWidth(2);
      flags.setColor(SkColorSetA(TextColor(this), picked ? 0xFF : 0x4D));
      canvas->DrawCircle(center, radius + 3, flags);
      flags.setStyle(cc::PaintFlags::kFill_Style);
    }
    const bool dark = editor_->dark();
    if (dots_.size() == 1) {
      flags.setColor(ColorOf(dots_.front(), dark));
    } else {
      // From the top left, as on the window.
      std::vector<SkColor4f> colors;
      for (const Theme::Dot& dot : dots_) {
        colors.push_back(SkColor4f::FromColor(ColorOf(dot, dark)));
      }
      const SkPoint points[] = {{center.x() - radius, center.y() - radius},
                                {center.x() + radius, center.y() + radius}};
      flags.setShader(cc::PaintShader::MakeLinearGradient(
          points, colors.data(), nullptr, static_cast<int>(colors.size()),
          SkTileMode::kClamp));
    }
    canvas->DrawCircle(center, radius, flags);
  }

 private:
  raw_ptr<Editor> editor_;
  std::vector<Theme::Dot> dots_;
};

BEGIN_METADATA(ThemeSwatch)
END_METADATA

// The themes ready to pick (workspace_theme.h's Presets()), a page at a time, with
// arrows to turn the pages.
class ThemePresetRow : public views::View {
  METADATA_HEADER(ThemePresetRow, views::View)

 public:
  explicit ThemePresetRow(Editor* editor) : editor_(editor) {
    previous_ = AddChildView(std::make_unique<ThemeToolButton>(
        Glyph::kPrevious, L10n("theme.previous"),
        base::BindRepeating(&ThemePresetRow::Turn, base::Unretained(this), -1)));
    for (size_t i = 0; i < kPresetsPerPage; ++i) {
      swatches_.push_back(AddChildView(std::make_unique<ThemeSwatch>(
          editor_, base::BindRepeating(&ThemePresetRow::Pick, base::Unretained(this), i))));
    }
    next_ = AddChildView(std::make_unique<ThemeToolButton>(
        Glyph::kNext, L10n("theme.next"),
        base::BindRepeating(&ThemePresetRow::Turn, base::Unretained(this), 1)));
    subscription_ = editor_->OnChanged(base::BindRepeating(
        [](ThemePresetRow* row) {
          for (views::View* swatch : row->swatches_) {
            swatch->SchedulePaint();
          }
        },
        base::Unretained(this)));
    // The page with the theme's colors, if it's one of them.
    const base::span<const std::vector<Theme::Dot>> presets = Presets();
    for (size_t i = 0; i < presets.size(); ++i) {
      if (presets[i] == editor_->theme().dots) {
        page_ = i / kPresetsPerPage;
      }
    }
    ShowPage();
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kWidth, kSwatchSize + 8);
  }

  void Layout(PassKey) override {
    previous_->SetBounds(0, 0, kArrowWidth, height());
    next_->SetBounds(width() - kArrowWidth, 0, kArrowWidth, height());
    // The swatches spread out between the arrows.
    const int count = static_cast<int>(swatches_.size());
    const int size = swatches_.front()->GetPreferredSize().width();
    const int start = kArrowWidth;
    const int room = width() - 2 * kArrowWidth - count * size;
    for (int i = 0; i < count; ++i) {
      swatches_[i]->SetBounds(start + room * (2 * i + 1) / (2 * count) + i * size,
                              (height() - size) / 2, size, size);
    }
  }

 private:
  size_t Pages() const {
    return (Presets().size() + kPresetsPerPage - 1) / kPresetsPerPage;
  }

  // Pages go round.
  void Turn(int by) {
    page_ = (page_ + Pages() + by) % Pages();
    ShowPage();
  }

  void ShowPage() {
    const base::span<const std::vector<Theme::Dot>> presets = Presets();
    for (size_t i = 0; i < swatches_.size(); ++i) {
      const size_t index = page_ * kPresetsPerPage + i;
      swatches_[i]->SetDots(
          index < presets.size() ? presets[index] : std::vector<Theme::Dot>(),
          base::ReplaceStringPlaceholders(
              L10n("theme.preset"),
              std::vector<std::u16string>{base::NumberToString16(index + 1)},
              nullptr));
      swatches_[i]->SetVisible(index < presets.size());
    }
  }

  void Pick(size_t i) {
    Theme changed = editor_->theme();
    changed.dots = swatches_[i]->dots();
    editor_->Set(std::move(changed));
  }

  raw_ptr<Editor> editor_;
  raw_ptr<ThemeToolButton> previous_ = nullptr;
  raw_ptr<ThemeToolButton> next_ = nullptr;
  std::vector<raw_ptr<ThemeSwatch>> swatches_;
  size_t page_ = 0;
  base::CallbackListSubscription subscription_;
};

BEGIN_METADATA(ThemePresetRow)
END_METADATA

// How much the colors cover the window's glass, a slider drawn as a wave: wavy as
// far as the value, flat past it. A double click goes back to the default.
class ThemeWave : public views::View {
  METADATA_HEADER(ThemeWave, views::View)

 public:
  explicit ThemeWave(Editor* editor) : editor_(editor) {
    SetFocusBehavior(FocusBehavior::ALWAYS);
    views::FocusRing::Install(this);
    GetViewAccessibility().SetRole(ax::mojom::Role::kSlider);
    GetViewAccessibility().SetName(L10n("theme.opacity"));
    SetTooltipText(L10n("theme.opacity"));
    subscription_ = editor_->OnChanged(
        base::BindRepeating(&ThemeWave::Refresh, base::Unretained(this)));
    Refresh();
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kWidth - kDialSize - 16, kControlsHeight);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const SkColor text = TextColor(this);
    const float middle = height() / 2.f;
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(SkColorSetA(text, 0x14));
    canvas->DrawRoundRect(gfx::RectF(0, middle - 9, width(), 18), 9, flags);

    const float at = Start() + editor_->opacity() * (End() - Start());
    auto wave_y = [middle](float x) {
      return middle -
             kWaveAmplitude * std::sin(2 * kPi * (x - kWavePadding) / kWavePeriod);
    };
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeCap(cc::PaintFlags::kRound_Cap);
    flags.setStrokeJoin(cc::PaintFlags::kRound_Join);
    if (at > Start()) {
      SkPathBuilder wave;
      wave.moveTo(Start(), wave_y(Start()));
      for (float x = Start() + 1; x < at; x += 1) {
        wave.lineTo(x, wave_y(x));
      }
      wave.lineTo(at, wave_y(at));
      flags.setStrokeWidth(3.5f);
      flags.setColor(SkColorSetA(text, 0xCC));
      canvas->DrawPath(wave.detach(), flags);
    }
    if (at < End()) {
      flags.setStrokeWidth(2);
      flags.setColor(SkColorSetA(text, 0x40));
      canvas->DrawLine(gfx::PointF(at, middle), gfx::PointF(End(), middle), flags);
    }
    // The knob rides the wave.
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setColor(text);
    canvas->DrawCircle(gfx::PointF(at, wave_y(at)),
                       dragging_ || IsMouseHovered() ? 7 : 6, flags);
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (!event.IsOnlyLeftMouseButton()) {
      return false;
    }
    RequestFocus();
    if (event.GetClickCount() == 2) {
      Theme changed = editor_->theme();
      changed.opacity.reset();
      editor_->Set(std::move(changed));
      return true;
    }
    dragging_ = true;
    SetValueAt(event.location_f().x());
    return true;
  }

  bool OnMouseDragged(const ui::MouseEvent& event) override {
    SetValueAt(event.location_f().x());
    return true;
  }

  void OnMouseReleased(const ui::MouseEvent& event) override {
    dragging_ = false;
    SchedulePaint();
  }

  void OnMouseCaptureLost() override { dragging_ = false; }
  void OnMouseEntered(const ui::MouseEvent& event) override { SchedulePaint(); }
  void OnMouseExited(const ui::MouseEvent& event) override { SchedulePaint(); }

  bool OnKeyPressed(const ui::KeyEvent& event) override {
    double by = 0;
    switch (event.key_code()) {
      case ui::VKEY_LEFT:
      case ui::VKEY_DOWN:
        by = -kOpacityStep;
        break;
      case ui::VKEY_RIGHT:
      case ui::VKEY_UP:
        by = kOpacityStep;
        break;
      default:
        return false;
    }
    Theme changed = editor_->theme();
    changed.opacity = std::clamp(editor_->opacity() + by, 0.0, 1.0);
    editor_->Set(std::move(changed));
    return true;
  }

 private:
  float Start() const { return kWavePadding; }
  float End() const { return width() - kWavePadding; }

  void SetValueAt(float x) {
    Theme changed = editor_->theme();
    changed.opacity =
        std::clamp(static_cast<double>((x - Start()) / (End() - Start())), 0.0, 1.0);
    editor_->Set(std::move(changed));
  }

  void Refresh() {
    GetViewAccessibility().SetValue(
        base::NumberToString16(std::lround(editor_->opacity() * 100)) + u"%");
    SchedulePaint();
  }

  raw_ptr<Editor> editor_;
  bool dragging_ = false;
  base::CallbackListSubscription subscription_;
};

BEGIN_METADATA(ThemeWave)
END_METADATA

// Grain, a dial turned clockwise from the top through kGrainSteps steps, marked
// around it. A double click turns it back to none.
class ThemeDial : public views::View {
  METADATA_HEADER(ThemeDial, views::View)

 public:
  explicit ThemeDial(Editor* editor) : editor_(editor) {
    SetFocusBehavior(FocusBehavior::ALWAYS);
    views::FocusRing::Install(this);
    GetViewAccessibility().SetRole(ax::mojom::Role::kSlider);
    GetViewAccessibility().SetName(L10n("theme.grain"));
    SetTooltipText(L10n("theme.grain"));
    subscription_ = editor_->OnChanged(
        base::BindRepeating(&ThemeDial::Refresh, base::Unretained(this)));
    Refresh();
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kDialSize, kDialSize);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const SkColor text = TextColor(this);
    const gfx::PointF center = gfx::RectF(GetLocalBounds()).CenterPoint();
    const float ring = std::min(width(), height()) / 2.f - 4;
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    // The knob.
    flags.setColor(SkColorSetA(text, 0x1C));
    canvas->DrawCircle(center, ring - 10, flags);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1);
    flags.setColor(SkColorSetA(text, 0x26));
    canvas->DrawCircle(center, ring - 10, flags);
    // The steps around it, lit as far as the value, which has a mark.
    const int step = Step();
    for (int i = 0; i < kGrainSteps; ++i) {
      const double angle = i * 2 * kPi / kGrainSteps;
      const gfx::Vector2dF direction(std::sin(angle), -std::cos(angle));
      if (i == step) {
        flags.setStyle(cc::PaintFlags::kStroke_Style);
        flags.setStrokeWidth(3.5f);
        flags.setStrokeCap(cc::PaintFlags::kRound_Cap);
        flags.setColor(text);
        canvas->DrawLine(center + gfx::ScaleVector2d(direction, ring - 4),
                         center + gfx::ScaleVector2d(direction, ring + 2), flags);
      } else {
        flags.setStyle(cc::PaintFlags::kFill_Style);
        flags.setColor(SkColorSetA(text, i < step ? 0x99 : 0x40));
        canvas->DrawCircle(center + gfx::ScaleVector2d(direction, ring), 1.6f,
                           flags);
      }
    }
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (!event.IsOnlyLeftMouseButton()) {
      return false;
    }
    RequestFocus();
    if (event.GetClickCount() == 2) {
      SetStep(0);
      return true;
    }
    SetStepAt(event.location_f());
    return true;
  }

  bool OnMouseDragged(const ui::MouseEvent& event) override {
    SetStepAt(event.location_f());
    return true;
  }

  bool OnKeyPressed(const ui::KeyEvent& event) override {
    switch (event.key_code()) {
      case ui::VKEY_LEFT:
      case ui::VKEY_DOWN:
        SetStep(Step() - 1);
        return true;
      case ui::VKEY_RIGHT:
      case ui::VKEY_UP:
        SetStep(Step() + 1);
        return true;
      default:
        return false;
    }
  }

 private:
  int Step() const {
    return static_cast<int>(
        std::lround(editor_->theme().grain * (kGrainSteps - 1)));
  }

  void SetStep(int step) {
    step = std::clamp(step, 0, kGrainSteps - 1);
    Theme changed = editor_->theme();
    changed.grain = static_cast<double>(step) / (kGrainSteps - 1);
    editor_->Set(std::move(changed));
  }

  // The step towards `point`, from the dial's middle. Turning past the top
  // stops at the last or the first step rather than going round.
  void SetStepAt(const gfx::PointF& point) {
    const gfx::Vector2dF offset =
        point - gfx::RectF(GetLocalBounds()).CenterPoint();
    double angle = std::atan2(offset.x(), -offset.y());
    if (angle < 0) {
      angle += 2 * kPi;
    }
    int step = static_cast<int>(std::lround(angle / (2 * kPi / kGrainSteps))) %
               kGrainSteps;
    const int current = Step();
    if (std::abs(step - current) > kGrainSteps / 2) {
      step = current > kGrainSteps / 2 ? kGrainSteps - 1 : 0;
    }
    SetStep(step);
  }

  void Refresh() {
    GetViewAccessibility().SetValue(
        base::NumberToString16(std::lround(editor_->theme().grain * 100)) + u"%");
    SchedulePaint();
  }

  raw_ptr<Editor> editor_;
  base::CallbackListSubscription subscription_;
};

BEGIN_METADATA(ThemeDial)
END_METADATA

// The bubble, while it's open.
class Picker {
 public:
  Picker(BrowserWindowInterface* browser,
         std::optional<std::string> id,
         Theme theme,
         views::View* anchor,
         views::BubbleBorder::Arrow arrow)
      : editor_(browser,
                std::move(id),
                std::move(theme),
                anchor->GetWidget()->GetTopLevelWidget()->GetNativeWindow()) {
    delegate_ = std::make_unique<views::BubbleDialogDelegate>(
        anchor, arrow, views::BubbleBorder::DIALOG_SHADOW, /*autosize=*/true);
    delegate_->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
    delegate_->SetShowCloseButton(false);
    delegate_->SetAccessibleTitle(L10n("theme.title"));
    delegate_->set_margins(gfx::Insets(12));

    auto contents = std::make_unique<views::View>();
    contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 12));
    contents->AddChildView(std::make_unique<ThemeField>(&editor_));
    contents->AddChildView(std::make_unique<ThemePresetRow>(&editor_));
    auto* controls = contents->AddChildView(std::make_unique<views::View>());
    auto* layout = controls->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 16));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    layout->SetFlexForView(controls->AddChildView(std::make_unique<ThemeWave>(&editor_)),
                           1);
    controls->AddChildView(std::make_unique<ThemeDial>(&editor_));
    delegate_->SetContentsView(std::move(contents));

    widget_ = views::BubbleDialogDelegate::CreateBubble(
        delegate_.get(),
        base::BindOnce(
            [](base::WeakPtr<Picker> picker, views::Widget::ClosedReason) {
              // Not from within its own closing.
              base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
                  FROM_HERE, base::BindOnce(
                                 [](base::WeakPtr<Picker> picker) {
                                   if (picker && Current().get() == picker.get()) {
                                     Current().reset();
                                   }
                                 },
                                 picker));
            },
            weak_factory_.GetWeakPtr()));
    widget_->Show();
  }
  Picker(const Picker&) = delete;
  Picker& operator=(const Picker&) = delete;

  // The one open, if any.
  static std::unique_ptr<Picker>& Current() {
    static base::NoDestructor<std::unique_ptr<Picker>> current;
    return *current;
  }

 private:
  // Before the views, which go first.
  Editor editor_;
  std::unique_ptr<views::BubbleDialogDelegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
  base::WeakPtrFactory<Picker> weak_factory_{this};
};

}  // namespace

void ShowThemePicker(BrowserWindowInterface* browser,
                     const std::string& id,
                     views::View* anchor) {
  const std::optional<Workspace> workspace =
      FindWorkspace(browser->GetProfile(), id);
  if (!workspace || !anchor || !anchor->GetWidget()) {
    return;
  }
  // Above what's in the lower half of its window (e.g. the workspaces at the
  // sidebar's bottom), else below.
  const bool above = anchor->GetBoundsInScreen().CenterPoint().y() >
                     anchor->GetWidget()
                         ->GetTopLevelWidget()
                         ->GetWindowBoundsInScreen()
                         .CenterPoint()
                         .y();
  Picker::Current().reset();
  Picker::Current() = std::make_unique<Picker>(
      browser, id, ThemeOf(browser->GetProfile(), *workspace), anchor,
      above ? views::BubbleBorder::BOTTOM_LEFT : views::BubbleBorder::TOP_LEFT);
}

void ShowDefaultThemePicker(BrowserWindowInterface* browser) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view || !browser_view->GetWidget()) {
    return;
  }
  // Over the page, by the sidebar's top.
  views::View* anchor = FindView(browser_view, "VerticalTabStripRegionView");
  Picker::Current().reset();
  Picker::Current() = std::make_unique<Picker>(
      browser, std::nullopt, DefaultTheme(browser->GetProfile()),
      anchor ? anchor : browser_view,
      anchor ? views::BubbleBorder::LEFT_TOP : views::BubbleBorder::TOP_LEFT);
}

void AddDefaultThemeItems(ui::SimpleMenuModel* model) {
  model->AddSeparator(ui::NORMAL_SEPARATOR);
  model->AddItem(kEditDefaultThemeCommandId, L10n("theme.edit_default"));
  model->AddItem(kResetDefaultThemeCommandId, L10n("theme.reset_default"));
}

bool IsDefaultThemeCommand(int command_id) {
  return command_id == kEditDefaultThemeCommandId ||
         command_id == kResetDefaultThemeCommandId;
}

bool IsDefaultThemeCommandEnabled(BrowserWindowInterface* browser,
                                  int command_id) {
  // Incognito windows keep their own look.
  if (browser->GetProfile()->IsOffTheRecord()) {
    return false;
  }
  return command_id != kResetDefaultThemeCommandId ||
         !IsDefaultThemeReset(browser->GetProfile());
}

void ExecuteDefaultThemeCommand(BrowserWindowInterface* browser,
                                int command_id) {
  if (command_id == kResetDefaultThemeCommandId) {
    ResetDefaultTheme(browser->GetProfile());
  } else if (command_id == kEditDefaultThemeCommandId) {
    // Once the menu is gone.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::WeakPtr<BrowserWindowInterface> browser) {
                         if (browser) {
                           ShowDefaultThemePicker(browser.get());
                         }
                       },
                       browser->GetWeakPtr()));
  }
}

}  // namespace oblada::workspaces
