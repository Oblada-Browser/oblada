#include "oblada/browser/ui/horizontal_tabs.h"

#include <algorithm>
#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/tabs/tab_style.h"
#include "chrome/browser/ui/views/tabs/tab.h"
#include "chrome/browser/ui/views/tabs/tab_strip.h"
#include "oblada/browser/ui/floating_location_bar.h"
#include "ui/base/class_property.h"
#include "ui/views/layout/layout_manager_base.h"
#include "ui/views/view.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/views/view_observer.h"
#include "ui/views/view_targeter.h"
#include "ui/views/view_targeter_delegate.h"
#include "ui/views/view_tracker.h"
#include "ui/views/view_utils.h"

namespace oblada::horizontal {

// Lays a toolbar out again as the active tab its address bar is over moves
// (see LayOutCompactLocationBar()): the tab, its tab strip or the strip's
// region.
class ActiveTabWatcher : public views::ViewObserver {
 public:
  explicit ActiveTabWatcher(views::View* toolbar) : toolbar_(toolbar) {}
  ActiveTabWatcher(const ActiveTabWatcher&) = delete;
  ActiveTabWatcher& operator=(const ActiveTabWatcher&) = delete;
  ~ActiveTabWatcher() override = default;

  void Watch(views::View* tab) {
    if (tab == tab_) {
      return;
    }
    observations_.RemoveAllObservations();
    tab_ = tab;
    for (views::View* view = tab; view; view = view->parent()) {
      observations_.AddObservation(view);
      if (views::IsViewClass<TabStrip>(view)) {
        if (view->parent()) {
          observations_.AddObservation(view->parent());
        }
        break;
      }
    }
  }

  // views::ViewObserver:
  void OnViewBoundsChanged(views::View* view) override {
    toolbar_->InvalidateLayout();
  }
  void OnViewIsDeleting(views::View* view) override {
    observations_.RemoveObservation(view);
    if (view == tab_) {
      tab_ = nullptr;
    }
  }

 private:
  raw_ptr<views::View> toolbar_;
  raw_ptr<views::View> tab_ = nullptr;
  base::ScopedMultiSourceObservation<views::View, views::ViewObserver>
      observations_{this};
};

// In horizon compact mode, leaves the events over the address bar to it, out of
// the tab strip's region, which comes before the toolbar for them (and would
// give them to the tab under the bar).
class CompactTabsTargeter : public views::ViewTargeterDelegate {
 public:
  explicit CompactTabsTargeter(views::View* location_bar)
      : location_bar_(location_bar) {}

  void SetLocationBar(views::View* location_bar) {
    location_bar_.SetView(location_bar);
  }

  // views::ViewTargeterDelegate:
  bool DoesIntersectRect(const views::View* target,
                         const gfx::Rect& rect) const override {
    if (!views::ViewTargeterDelegate::DoesIntersectRect(target, rect)) {
      return false;
    }
    const views::View* location_bar = location_bar_.view();
    if (!location_bar || !location_bar->IsDrawn() ||
        sidebar::IsLocationBarFloating(location_bar) ||
        LayoutOf(target) != sidebar::Mode::kHorizonCompact) {
      return true;
    }
    gfx::RectF bounds(location_bar->GetLocalBounds());
    views::View::ConvertRectToTarget(location_bar, target, &bounds);
    return !gfx::ToEnclosingRect(bounds).Contains(rect.CenterPoint());
  }

 private:
  views::ViewTracker location_bar_;
};

}  // namespace oblada::horizontal

DEFINE_UI_CLASS_PROPERTY_TYPE(oblada::horizontal::ActiveTabWatcher*)
DEFINE_UI_CLASS_PROPERTY_TYPE(oblada::horizontal::CompactTabsTargeter*)

namespace oblada::horizontal {

namespace {

// The layout (a sidebar::Mode), or -1 for none.
DEFINE_UI_CLASS_PROPERTY_KEY(int, kLayoutKey, -1)

DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(ActiveTabWatcher, kActiveTabWatcherKey)
DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(CompactTabsTargeter, kCompactTargeterKey)

// The horizontal tab strip under `view`, if any.
TabStrip* FindTabStrip(views::View* view) {
  if (TabStrip* tab_strip = views::AsViewClass<TabStrip>(view)) {
    return tab_strip;
  }
  for (views::View* child : view->children()) {
    if (TabStrip* tab_strip = FindTabStrip(child)) {
      return tab_strip;
    }
  }
  return nullptr;
}

// The centered address bar's width: this much of the window's, within these
// bounds.
constexpr float kLocationBarWindowFraction = 0.46f;
constexpr int kLocationBarMinWidth = 320;
constexpr int kLocationBarMaxWidth = 760;

}  // namespace

void SetLayout(views::View* region, std::optional<sidebar::Mode> mode) {
  const int value = mode ? static_cast<int>(*mode) : -1;
  if (region->GetProperty(kLayoutKey) != value) {
    region->SetProperty(kLayoutKey, value);
    region->InvalidateLayout();
  }
}

bool SharesRow(const views::View* view) {
  const std::optional<sidebar::Mode> layout = LayoutOf(view);
  return layout == sidebar::Mode::kHorizon ||
         layout == sidebar::Mode::kHorizonCompact;
}

std::optional<sidebar::Mode> LayoutOf(const views::View* view) {
  for (; view; view = view->parent()) {
    if (const int value = view->GetProperty(kLayoutKey); value >= 0) {
      return static_cast<sidebar::Mode>(value);
    }
  }
  return std::nullopt;
}

void CenterLocationBar(views::View* toolbar, views::View* location_bar) {
  const gfx::Rect room = location_bar->bounds();
  const views::View* window = toolbar->parent();
  if (room.IsEmpty() || !window) {
    return;
  }
  const int width = std::min(
      room.width(),
      std::clamp(static_cast<int>(window->width() * kLocationBarWindowFraction),
                 kLocationBarMinWidth, kLocationBarMaxWidth));
  const int window_center = window->width() / 2 - toolbar->x();
  const int x =
      std::clamp(window_center - width / 2, room.x(), room.right() - width);
  location_bar->SetBounds(x, room.y(), width, room.height());
}

int CompactActiveTabMinWidth(const views::View* tab,
                             BrowserWindowInterface* browser,
                             int narrowest,
                             int overlap,
                             int fallback) {
  const views::View* tabs = tab->parent();
  TabStripModel* model = browser ? browser->GetTabStripModel() : nullptr;
  if (!tabs || !model || tabs->width() == 0) {
    return kCompactActiveTabMinWidth;
  }
  // The tabs shown: the workspace's, and the pinned ones.
  const std::optional<tab_groups::TabGroupId> focused = model->GetFocusedGroup();
  int shown = 0;
  for (int i = 0; i < model->count(); ++i) {
    shown += !focused || model->IsTabPinned(i) ||
             model->GetTabGroupForTab(i) == focused;
  }
  const int room =
      tabs->width() - std::max(0, shown - 1) * (narrowest - overlap) - overlap;
  return std::clamp(room, fallback, kCompactActiveTabMinWidth);
}

gfx::Rect CompactTabsRoom(const views::View* toolbar,
                          const gfx::Size& toolbar_size) {
  const auto* layout =
      static_cast<const views::LayoutManagerBase*>(toolbar->GetLayoutManager());
  if (!layout) {
    return gfx::Rect();
  }
  for (const views::ChildLayout& child :
       layout->GetProposedLayout(toolbar_size).child_layouts) {
    if (child.child_view &&
        child.child_view->GetClassName() == "LocationBarView") {
      return child.bounds;
    }
  }
  return gfx::Rect();
}

void LayOutCompactLocationBar(views::View* toolbar,
                              views::View* location_bar,
                              int active_index) {
  views::View* window = toolbar->parent() ? toolbar->parent()->parent() : nullptr;
  TabStrip* tab_strip = window ? FindTabStrip(window) : nullptr;
  Tab* tab = tab_strip && active_index >= 0 &&
                     active_index < tab_strip->GetTabCount()
                 ? tab_strip->tab_at(active_index)
                 : nullptr;
  if (!toolbar->GetProperty(kActiveTabWatcherKey)) {
    toolbar->SetProperty(kActiveTabWatcherKey,
                         std::make_unique<ActiveTabWatcher>(toolbar));
  }
  toolbar->GetProperty(kActiveTabWatcherKey)->Watch(tab);
  // Until the tab is laid out, where the toolbar's layout put it.
  if (!tab || !tab->GetVisible() || tab->bounds().IsEmpty()) {
    return;
  }
  // The events over the bar are the bar's, not the tab's under it.
  if (views::View* region = tab_strip->parent()) {
    if (CompactTabsTargeter* targeter = region->GetProperty(kCompactTargeterKey)) {
      targeter->SetLocationBar(location_bar);
    } else {
      region->SetEventTargeter(std::make_unique<views::ViewTargeter>(
          region->SetProperty(kCompactTargeterKey,
                              std::make_unique<CompactTabsTargeter>(location_bar))));
    }
  }
  // The tab's veil (see horizontal_tab_style_views.cc), short of its close
  // button.
  const int corner = TabStyle::Get()->GetBottomCornerRadius();
  const int top = GetLayoutConstant(LayoutConstant::kTabStripPadding);
  const int bottom = GetLayoutConstant(LayoutConstant::kTabHeight) -
                     GetLayoutConstant(LayoutConstant::kTabstripToolbarOverlap);
  int right = tab->width() - corner;
  for (const views::View* child : tab->children()) {
    if (child->GetClassName() == "TabCloseButton" && child->GetVisible() &&
        !child->bounds().IsEmpty()) {
      right = std::min(right, child->x());
    }
  }
  const gfx::Rect veil(corner, top, std::max(0, right - corner), bottom - top);
  sidebar::LayOutLocationBar(
      toolbar, location_bar,
      views::View::ConvertRectToTarget(tab, toolbar, veil));
}

}  // namespace oblada::horizontal
