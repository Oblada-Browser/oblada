#include "oblada/browser/ui/sidebar_card.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_image.h"
#include "cc/paint/paint_shader.h"
#include "oblada/browser/ui/sidebar_layout.h"
#include "oblada/browser/ui/window_background.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/base/class_property.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

DEFINE_UI_CLASS_PROPERTY_TYPE(oblada::sidebar::Card)
DEFINE_UI_CLASS_PROPERTY_TYPE(oblada::window_background::Background*)

namespace oblada::sidebar {

namespace {

DEFINE_UI_CLASS_PROPERTY_KEY(Card, kCardKey, Card::kNone)
DEFINE_UI_CLASS_PROPERTY_KEY(bool, kGlassKey, false)
// The window's background the panel was last drawn with.
DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(window_background::Background,
                                   kBackgroundKey)


// The window's colors, as its glass shows them, over the card's gray: a
// gradient from the window's top left to its bottom right (as Core Animation
// draws it, see window_background_mac.mm), and the grain.
void PaintTheme(gfx::Canvas* canvas,
                const views::View* view,
                const window_background::Background& background,
                SkColor frame_color) {
  const views::Widget* widget = view->GetWidget();
  const gfx::Size window = widget->GetRootView()->size();
  gfx::Point origin = view->origin();
  views::View::ConvertPointToWidget(view->parent(), &origin);
  if (window.IsEmpty()) {
    return;
  }

  std::vector<SkColor> colors = background.colors;
  if (colors.empty()) {
    colors.push_back(frame_color);
  }
  if (colors.size() == 1) {
    colors.push_back(colors.front());
  }
  std::vector<SkColor4f> colors4f;
  for (SkColor color : colors) {
    colors4f.push_back(SkColor4f::FromColor(color));
  }
  // From (0, 0) to (1, 1) in a square the window's size.
  const SkPoint points[] = {SkPoint::Make(0, 0), SkPoint::Make(1, 1)};
  SkMatrix matrix = SkMatrix::Translate(-origin.x(), -origin.y());
  matrix.preScale(window.width(), window.height());
  cc::PaintFlags gradient;
  gradient.setAntiAlias(true);
  gradient.setShader(cc::PaintShader::MakeLinearGradient(
      points, colors4f.data(), nullptr, colors4f.size(), SkTileMode::kClamp,
      SkGradient::Interpolation(), 0, &matrix));
  gradient.setAlphaf(static_cast<float>(background.opacity.value_or(
      window_background::DefaultOpacity(widget->GetNativeWindow()))));
  canvas->DrawRect(view->GetLocalBounds(), gradient);

  if (background.grain > 0) {
    // A pixel of the tile to a pixel of the screen.
    const SkMatrix scale = SkMatrix::Scale(1 / canvas->image_scale(),
                                           1 / canvas->image_scale());
    cc::PaintFlags grain;
    grain.setShader(cc::PaintShader::MakeImage(
        cc::PaintImage::CreateFromBitmap(window_background::GrainTile()),
        SkTileMode::kRepeat, SkTileMode::kRepeat, &scale));
    grain.setAlphaf(static_cast<float>(background.grain) *
                    window_background::kMaxGrain);
    canvas->DrawRect(view->GetLocalBounds(), grain);
  }
}

}  // namespace

int CardMargin() {
  return ContentInsets().top();
}

void SetCard(views::View* view, Card card, bool glass) {
  const window_background::Background background =
      view->GetWidget()
          ? window_background::Get(view->GetWidget()->GetNativeWindow())
          : window_background::Background();
  const window_background::Background* painted =
      view->GetProperty(kBackgroundKey);
  if (view->GetProperty(kCardKey) != card ||
      view->GetProperty(kGlassKey) != glass || !painted ||
      *painted != background) {
    view->SetProperty(kCardKey, card);
    view->SetProperty(kGlassKey, glass);
    view->SetProperty(kBackgroundKey,
                      std::make_unique<window_background::Background>(
                          background));
    view->SchedulePaint();
  }
}

bool PaintCard(gfx::Canvas* canvas, const views::View* view) {
  const Card card = view->GetProperty(kCardKey);
  if (card == Card::kNone || !view->GetWidget() || !view->GetColorProvider()) {
    return card != Card::kNone;
  }
  const bool glass = view->GetProperty(kGlassKey);
  const window_background::Background* background =
      view->GetProperty(kBackgroundKey);
  const SkColor frame_color = view->GetColorProvider()->GetColor(
      view->GetWidget()->ShouldPaintAsActive() ? ui::kColorFrameActive
                                               : ui::kColorFrameInactive);
  const bool dark =
      background && background->dark ? *background->dark
                                     : color_utils::IsDark(frame_color);
  const SkRRect rrect = SkRRect::MakeRectXY(
      gfx::RectToSkRect(view->GetLocalBounds()),
      // The page's corners, as far from the window's edges.
      ContentCornerRadius(), ContentCornerRadius());

  gfx::ScopedCanvas scoped(canvas);
  canvas->sk_canvas()->clipRRect(rrect, SkClipOp::kIntersect, true);

  // Opaque, so that nothing under it shows as it widens over the page.
  if (glass && background) {
    canvas->DrawColor(window_background::GlassStandIn(dark));
    PaintTheme(canvas, view, *background, frame_color);
  } else {
    // A window without glass is all its frame color.
    canvas->DrawColor(SkColorSetA(frame_color, 0xFF));
  }

  // An edge, as glass has.
  cc::PaintFlags edge;
  edge.setAntiAlias(true);
  edge.setStyle(cc::PaintFlags::kStroke_Style);
  edge.setStrokeWidth(1);
  edge.setColor(dark ? SkColorSetA(SK_ColorWHITE, 0x1C)
                     : SkColorSetA(SK_ColorBLACK, 0x14));
  SkRRect inner;
  rrect.inset(0.5f, 0.5f, &inner);
  canvas->sk_canvas()->drawRRect(inner, edge);
  return true;
}

void ClipToCard(views::View* view, const views::View* card_view) {
  ui::Layer* layer = view->layer();
  if (!layer) {
    return;
  }
  const Card card = card_view ? card_view->GetProperty(kCardKey) : Card::kNone;
  if (card != Card::kPanel) {
    layer->SetClipRect(gfx::Rect());
    layer->SetRoundedCornerRadius(gfx::RoundedCornersF());
    return;
  }
  const gfx::Rect card_bounds = card_view->bounds();
  gfx::Rect clip = card_bounds;
  clip.Intersect(view->bounds());
  if (clip.IsEmpty()) {
    // Nothing of it in the card (an empty clip would clip nothing).
    layer->SetClipRect(gfx::Rect(-1, -1, 1, 1));
    return;
  }
  // Rounded where the card's corners are.
  const float radius = ContentCornerRadius();
  const gfx::RoundedCornersF corners(
      clip.origin() == card_bounds.origin() ? radius : 0,
      clip.top_right() == card_bounds.top_right() ? radius : 0,
      clip.bottom_right() == card_bounds.bottom_right() ? radius : 0,
      clip.bottom_left() == card_bounds.bottom_left() ? radius : 0);
  clip.Offset(-view->x(), -view->y());
  layer->SetClipRect(clip);
  layer->SetRoundedCornerRadius(corners);
  layer->SetIsFastRoundedCorner(true);
}

gfx::Vector2d WindowControlsOffset() {
  return gfx::Vector2d(CardMargin(), CardMargin());
}

}  // namespace oblada::sidebar
