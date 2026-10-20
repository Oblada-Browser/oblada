#include "oblada/browser/ui/floating_location_bar.h"

#include <algorithm>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/scoped_multi_source_observation.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/chrome_typography.h"
#include "oblada/browser/ui/sidebar_layout.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/class_property.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_solid_color.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_observer.h"
#include "ui/views/view_targeter.h"
#include "ui/views/view_targeter_delegate.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget.h"

namespace oblada::sidebar {

class FloatingLocationBar;
class FloatingLocationBarTargeter;

}  // namespace oblada::sidebar

DEFINE_UI_CLASS_PROPERTY_TYPE(oblada::sidebar::FloatingLocationBar*)
DEFINE_UI_CLASS_PROPERTY_TYPE(oblada::sidebar::FloatingLocationBarTargeter*)

namespace oblada::sidebar {

namespace {

// Its width, as a share of the page's within these bounds, and its distance from
// the page's edges at least.
constexpr int kMinWidth = 440;
constexpr int kMaxWidth = 720;
constexpr int kMargin = 16;

// How much the veil over the page darkens it, in dark and light colors.
constexpr float kDarkDim = 0.25f;
constexpr float kLightDim = 0.15f;

// Its place in the sidebar: where its text starts, as the address bar's.
constexpr int kPlaceholderTextInset = 17;

// The bar's shape flies to the page (and back) this long; the bar shows, whole, as
// it lands, and the shape fades out over it this long. (Never both translucent at
// once, which would let the page show through. Nor is the bar itself scaled: its
// suggestions are placed by where it is, transform included.)
constexpr base::TimeDelta kFlightOut = base::Milliseconds(320);
constexpr base::TimeDelta kFlightBack = base::Milliseconds(240);
constexpr base::TimeDelta kLanding = base::Milliseconds(120);

// The floating address bars there are (one per window at most).
std::set<const views::View*>& FloatingLocationBars() {
  static base::NoDestructor<std::set<const views::View*>> bars;
  return *bars;
}

// The transform that shows a layer with bounds `to` at `from`.
gfx::Transform Mapping(const gfx::Rect& to, const gfx::Rect& from) {
  gfx::Transform transform;
  transform.Translate(from.x() - to.x(), from.y() - to.y());
  transform.Scale(static_cast<float>(from.width()) / to.width(),
                  static_cast<float>(from.height()) / to.height());
  return transform;
}

void DeleteLayerSoon(std::unique_ptr<ui::Layer> layer) {
  base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE,
                                                             std::move(layer));
}

// Where `view` is in its widget, leaving out its own transform (e.g. the bar's as
// it lands).
gfx::Rect BoundsInWidget(const views::View* view) {
  return view->parent() ? view->parent()->ConvertRectToWidget(view->bounds())
                        : view->bounds();
}

// Removes the view `tracker` tracks from its parent, if it's still there.
void RemoveSoon(std::unique_ptr<views::ViewTracker> tracker) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](std::unique_ptr<views::ViewTracker> tracker) {
                       if (views::View* view = tracker->view()) {
                         view->parent()->RemoveChildViewT(view);
                       }
                     },
                     std::move(tracker)));
}

// The floating bar's color.
SkColor BarColor(const views::View* location_bar) {
  return location_bar->GetColorProvider()->GetColor(
      kColorOmniboxResultsBackground);
}

// Flies the bar's shape, in `color`, from `from` to `to` (in `widget`'s
// coordinates), above everything in the window, then fades it out (over the bar);
// then runs `done`.
void Fly(views::Widget* widget,
         const gfx::Rect& from,
         const gfx::Rect& to,
         SkColor color,
         base::TimeDelta duration,
         base::OnceClosure done = base::DoNothing()) {
  ui::Layer* root = widget ? widget->GetLayer() : nullptr;
  if (!root || from.IsEmpty() || to.IsEmpty()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
                                                             std::move(done));
    return;
  }
  auto shape = std::make_unique<ui::LayerSolidColor>();
  shape->SetName("FloatingLocationBarFlight");
  shape->SetColor(SkColor4f::FromColor(color));
  shape->SetBounds(to);
  shape->SetRoundedCornerRadius(
      gfx::RoundedCornersF(kFloatingLocationBarCornerRadius));
  shape->SetTransform(Mapping(to, from));
  root->Add(shape.get());
  root->StackAtTop(shape.get());
  ui::Layer* layer = shape.get();
  auto [ended, aborted] = base::SplitOnceCallback(base::BindOnce(
      [](std::unique_ptr<ui::Layer> layer, base::OnceClosure done) {
        DeleteLayerSoon(std::move(layer));
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE, std::move(done));
      },
      std::unique_ptr<ui::Layer>(std::move(shape)), std::move(done)));
  views::AnimationBuilder()
      .OnEnded(std::move(ended))
      .OnAborted(std::move(aborted))
      .Once()
      .SetDuration(duration)
      .SetTransform(layer, gfx::Transform(), gfx::Tween::FAST_OUT_SLOW_IN)
      .Then()
      .SetDuration(kLanding)
      .SetOpacity(layer, 0.f);
}

// Fades `layer` out, then deletes it.
void FadeOut(std::unique_ptr<ui::Layer> layer, base::TimeDelta duration) {
  ui::Layer* target = layer.get();
  auto [ended, aborted] = base::SplitOnceCallback(
      base::BindOnce(&DeleteLayerSoon, std::move(layer)));
  views::AnimationBuilder()
      .OnEnded(std::move(ended))
      .OnAborted(std::move(aborted))
      .Once()
      .SetDuration(duration)
      .SetOpacity(target, 0.f, gfx::Tween::EASE_IN_OUT);
}

// The bar's place in the sidebar while it floats, grayed out: the site it shows.
class Placeholder : public views::View {
  METADATA_HEADER(Placeholder, views::View)

 public:
  explicit Placeholder(const std::u16string& text) {
    SetLayoutManager(std::make_unique<views::FillLayout>());
    SetProperty(views::kViewIgnoredByLayoutKey, true);
    SetCanProcessEventsWithinSubtree(false);
    label_ = AddChildView(
        std::make_unique<views::Label>(text, CONTEXT_OMNIBOX_PRIMARY));
    label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label_->SetElideBehavior(gfx::ELIDE_TAIL);
    label_->SetAutoColorReadabilityEnabled(false);
    label_->SetSubpixelRenderingEnabled(false);
    label_->SetBorder(
        views::CreateEmptyBorder(gfx::Insets::VH(0, kPlaceholderTextInset)));
  }

  // views::View:
  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    const SkColor text = GetColorProvider()->GetColor(kColorOmniboxText);
    SetBackground(views::CreateRoundedRectBackground(SkColorSetA(text, 0x0C),
                                                     kLocationBarCornerRadius));
    label_->SetEnabledColor(SkColorSetA(text, 0x50));
  }

 private:
  raw_ptr<views::Label> label_;
};

BEGIN_METADATA(Placeholder)
END_METADATA

}  // namespace

// A floating address bar's state: its place in the sidebar, the page
// it floats over (whose moves its toolbar has to follow) and the veil dimming it.
class FloatingLocationBar : public views::ViewObserver {
 public:
  FloatingLocationBar(views::View* location_bar,
                      views::View* page,
                      const std::u16string& placeholder)
      : location_bar_(location_bar), page_(page) {
    FloatingLocationBars().insert(location_bar);
    // The bar paints to a layer of its own already, which isn't clipped to its
    // toolbar. It shows once its shape has flown there (see LayOut()).
    location_bar->layer()->SetOpacity(0.f);
    if (views::View* toolbar = location_bar->parent()) {
      views::View* view =
          toolbar->AddChildView(std::make_unique<Placeholder>(placeholder));
      view->SetBoundsRect(location_bar->bounds());
      placeholder_.SetView(view);
    }
    observations_.AddObservation(location_bar);
    observations_.AddObservation(page);
  }
  FloatingLocationBar(const FloatingLocationBar&) = delete;
  FloatingLocationBar& operator=(const FloatingLocationBar&) = delete;

  ~FloatingLocationBar() override {
    Land();
    // Unless the bar is being deleted (with its toolbar, then).
    if (!location_bar_) {
      return;
    }
    FloatingLocationBars().erase(location_bar_);
    if (views::View* placeholder = placeholder_.view()) {
      placeholder->parent()->RemoveChildViewT(placeholder);
    }
    if (dim_) {
      location_bar_->RemoveLayerFromRegions(dim_.get());
    }
  }

  // Lays the bar out over the page, and its placeholder in its row.
  void LayOut(views::View* toolbar) {
    const gfx::Rect slot = LocationBarBounds(toolbar->size(), location_bar_);
    if (views::View* placeholder = placeholder_.view()) {
      placeholder->SetBoundsRect(slot);
    }
    if (!page_ || page_->bounds().IsEmpty()) {
      location_bar_->SetBoundsRect(slot);
      Land();
      return;
    }
    gfx::RectF page_bounds(page_->GetLocalBounds());
    views::View::ConvertRectToTarget(page_, toolbar, &page_bounds);
    const gfx::Rect area = gfx::ToEnclosingRect(page_bounds);
    const int width = std::max(
        0, std::min(area.width() - 2 * kMargin,
                    std::clamp(area.width() * 3 / 5, kMinWidth, kMaxWidth)));
    // In the upper part of the page, leaving its suggestions room below.
    location_bar_->SetBoundsRect(gfx::Rect(
        area.x() + (area.width() - width) / 2,
        area.y() + std::max(kMargin, area.height() / 5), width,
        slot.height() + kFloatingLocationBarExtraHeight));
    LayOutDim();
    if (!flown_) {
      flown_ = true;
      FlyOut();
    }
  }

  // Flies the bar's shape back from the page to its row in the sidebar, where the
  // bar shows again as it lands.
  void FlyBack(views::View* toolbar) {
    FadeDimOut();
    views::Widget* widget = location_bar_->GetWidget();
    if (!widget || !flown_) {
      location_bar_->layer()->SetOpacity(1.f);
      return;
    }
    const gfx::Rect from = BoundsInWidget(location_bar_);
    const gfx::Rect to = toolbar->ConvertRectToWidget(
        LocationBarBounds(toolbar->size(), location_bar_));
    Fly(widget, from, to, BarColor(location_bar_), kFlightBack);
    // The bar, back in its row, shows once its shape has landed there; its
    // placeholder keeps its place until then.
    ui::Layer* layer = location_bar_->layer();
    layer->GetAnimator()->StopAnimating();
    layer->SetTransform(gfx::Transform());
    layer->SetOpacity(0.f);
    auto [ended, aborted] = base::SplitOnceCallback(base::BindOnce(
        &RemoveSoon, std::make_unique<views::ViewTracker>(placeholder_.view())));
    placeholder_.SetView(nullptr);
    views::AnimationBuilder()
        .OnEnded(std::move(ended))
        .OnAborted(std::move(aborted))
        .Once()
        .At(kFlightBack)
        .SetDuration(base::TimeDelta())
        .SetOpacity(layer, 1.f);
  }

  // Whether the bar is still on its way over the page, and what to do once it
  // has landed there (see WhenFloatingLocationBarLands()).
  bool landing() const { return !landed_; }
  void WhenLanded(base::OnceClosure callback) {
    if (landed_) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, std::move(callback));
    } else {
      when_landed_.push_back(std::move(callback));
    }
  }

  // views::ViewObserver:
  void OnViewBoundsChanged(views::View* observed_view) override {
    if (observed_view == page_ && location_bar_ && location_bar_->parent()) {
      location_bar_->parent()->InvalidateLayout();
    }
  }

  void OnViewLayerBoundsSet(views::View* observed_view) override {
    // Views moves the layers under the bar's along with it: the veil goes back
    // over the page.
    if (observed_view == location_bar_ && dim_ && page_) {
      LayOutDim();
    }
  }

  void OnViewIsDeleting(views::View* observed_view) override {
    observations_.RemoveObservation(observed_view);
    if (observed_view == location_bar_) {
      FloatingLocationBars().erase(location_bar_);
      if (dim_) {
        location_bar_->RemoveLayerFromRegions(dim_.get());
        dim_.reset();
      }
      location_bar_ = nullptr;
    } else if (observed_view == page_) {
      page_ = nullptr;
    }
  }

 private:
  // Fades the veil out, from above everything in the window: the bar's container,
  // which it is in, goes back under the page.
  void FadeDimOut() {
    if (!dim_) {
      return;
    }
    location_bar_->RemoveLayerFromRegions(dim_.get());
    views::Widget* widget = location_bar_->GetWidget();
    ui::Layer* root = widget ? widget->GetLayer() : nullptr;
    if (!root || !page_) {
      dim_.reset();
      return;
    }
    dim_->SetBounds(BoundsInWidget(page_));
    root->Add(dim_.get());
    root->StackAtTop(dim_.get());
    FadeOut(std::move(dim_), kFlightBack);
  }

  // The veil dimming the page, under the bar, over the page.
  void LayOutDim() {
    ui::Layer* layer = location_bar_->layer();
    if (!layer->parent()) {
      return;
    }
    if (!dim_) {
      auto dim = std::make_unique<ui::LayerSolidColor>();
      dim->SetName("FloatingLocationBarDim");
      dim->SetColor(SkColors::kBlack);
      dim->SetRoundedCornerRadius(gfx::RoundedCornersF(ContentCornerRadius()));
      dim->SetOpacity(0.f);
      dim_ = std::move(dim);
      location_bar_->AddLayerToRegion(dim_.get(), views::LayerRegion::kBelow);
    }
    // In the coordinates of the layer the bar's is in.
    const gfx::Vector2d offset =
        layer->bounds().origin() - BoundsInWidget(location_bar_).origin();
    dim_->SetBounds(BoundsInWidget(page_) + offset);
  }

  void Land() {
    landed_ = true;
    for (base::OnceClosure& callback : when_landed_) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, std::move(callback));
    }
    when_landed_.clear();
  }

  // Flies the bar's shape from its place in the sidebar to the page, where the
  // bar appears as it lands, as the page dims.
  void FlyOut() {
    views::Widget* widget = location_bar_->GetWidget();
    views::View* placeholder = placeholder_.view();
    ui::Layer* layer = location_bar_->layer();
    Fly(widget, placeholder ? BoundsInWidget(placeholder) : gfx::Rect(),
        BoundsInWidget(location_bar_), BarColor(location_bar_), kFlightOut,
        base::BindOnce(&FloatingLocationBar::Land, weak_factory_.GetWeakPtr()));
    views::AnimationBuilder builder;
    if (dim_) {
      // Easing in: the first frames of an animation are the likeliest to come
      // late (as the bar's window gets busy), and least noticeable then.
      // Never quite opaque, black: the veil is drawn the same way all along.
      builder.Once().SetDuration(kFlightOut).SetOpacity(
          dim_.get(),
          color_utils::IsDark(BarColor(location_bar_)) ? kDarkDim : kLightDim,
          gfx::Tween::EASE_IN_OUT);
    }
    builder.Once()
        .At(kFlightOut)
        .SetDuration(base::TimeDelta())
        .SetOpacity(layer, 1.f);
  }

  raw_ptr<views::View> location_bar_;
  raw_ptr<views::View> page_;
  views::ViewTracker placeholder_;
  std::unique_ptr<ui::Layer> dim_;
  // Whether its shape has flown out to the page yet (see FlyOut()), and landed.
  bool flown_ = false;
  bool landed_ = false;
  std::vector<base::OnceClosure> when_landed_;
  base::ScopedMultiSourceObservation<views::View, views::ViewObserver>
      observations_{this};
  base::WeakPtrFactory<FloatingLocationBar> weak_factory_{this};
};

// Lets events reach a floating address bar through its ancestors (its toolbar and
// the toolbar's container), outside of whose bounds it is.
class FloatingLocationBarTargeter : public views::ViewTargeterDelegate {
 public:
  explicit FloatingLocationBarTargeter(views::View* location_bar)
      : location_bar_(location_bar) {}

  // views::ViewTargeterDelegate:
  bool DoesIntersectRect(const views::View* target,
                         const gfx::Rect& rect) const override {
    if (views::ViewTargeterDelegate::DoesIntersectRect(target, rect)) {
      return true;
    }
    const views::View* location_bar = location_bar_.view();
    if (!location_bar || !IsLocationBarFloating(location_bar) ||
        !location_bar->IsDrawn()) {
      return false;
    }
    gfx::RectF bounds(location_bar->GetLocalBounds());
    views::View::ConvertRectToTarget(location_bar, target, &bounds);
    return gfx::ToEnclosingRect(bounds).Intersects(rect);
  }

 private:
  views::ViewTracker location_bar_;
};

namespace {

DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(FloatingLocationBar, kFloatingKey)
DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(FloatingLocationBarTargeter,
                                   kFloatingTargeterKey)

}  // namespace

void SetLocationBarFloating(views::View* location_bar,
                            views::View* page,
                            bool floating,
                            const std::u16string& placeholder) {
  if (IsLocationBarFloating(location_bar) == floating) {
    return;
  }
  views::View* toolbar = location_bar->parent();
  if (floating) {
    if (!toolbar || !location_bar->layer()) {
      return;
    }
    location_bar->SetProperty(kFloatingKey, std::make_unique<FloatingLocationBar>(
                                                location_bar, page, placeholder));
    // The bar is outside of its toolbar and of the toolbar's container.
    for (views::View* ancestor : {toolbar, toolbar->parent()}) {
      if (ancestor && !ancestor->GetProperty(kFloatingTargeterKey) &&
          !ancestor->GetEventTargeter()) {
        auto* targeter = ancestor->SetProperty(
            kFloatingTargeterKey,
            std::make_unique<FloatingLocationBarTargeter>(location_bar));
        ancestor->SetEventTargeter(std::make_unique<views::ViewTargeter>(targeter));
      }
    }
  } else {
    if (toolbar) {
      location_bar->GetProperty(kFloatingKey)->FlyBack(toolbar);
    }
    location_bar->ClearProperty(kFloatingKey);
  }
  // Its toolbar lays it out; the toolbar's container goes above the page.
  location_bar->InvalidateLayout();
  if (toolbar) {
    toolbar->InvalidateLayout();
  }
  location_bar->SchedulePaint();
}

bool IsLocationBarFloating(const views::View* location_bar) {
  return location_bar && location_bar->GetProperty(kFloatingKey);
}

bool HasFloatingLocationBar(const views::View* toolbar) {
  if (!toolbar) {
    return false;
  }
  for (const views::View* child : toolbar->children()) {
    if (IsLocationBarFloating(child)) {
      return true;
    }
  }
  return false;
}

bool IsFloatingLocationBarAt(const gfx::Rect& bounds_in_screen) {
  for (const views::View* location_bar : FloatingLocationBars()) {
    if (location_bar->GetBoundsInScreen() == bounds_in_screen) {
      return true;
    }
  }
  return false;
}

bool IsFloatingLocationBarLanding(const gfx::Rect& bounds_in_screen) {
  for (const views::View* location_bar : FloatingLocationBars()) {
    if (location_bar->GetBoundsInScreen() == bounds_in_screen) {
      return location_bar->GetProperty(kFloatingKey)->landing();
    }
  }
  return false;
}

void WhenFloatingLocationBarLands(const gfx::Rect& bounds_in_screen,
                                  base::OnceClosure callback) {
  for (const views::View* location_bar : FloatingLocationBars()) {
    if (location_bar->GetBoundsInScreen() == bounds_in_screen) {
      location_bar->GetProperty(kFloatingKey)->WhenLanded(std::move(callback));
      return;
    }
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
                                                           std::move(callback));
}

void LayOutLocationBar(views::View* toolbar, views::View* location_bar) {
  if (FloatingLocationBar* floating = location_bar->GetProperty(kFloatingKey)) {
    floating->LayOut(toolbar);
  } else {
    location_bar->SetBoundsRect(LocationBarBounds(toolbar->size(), location_bar));
  }
}

}  // namespace oblada::sidebar
