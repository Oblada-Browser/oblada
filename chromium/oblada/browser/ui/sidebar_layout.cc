#include "oblada/browser/ui/sidebar_layout.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "oblada/browser/ui/floating_location_bar.h"
#include "oblada/browser/ui/horizontal_tabs.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/base/class_property.h"
#include "ui/compositor/layer.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"
#include "ui/events/event_handler.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_tracker.h"

namespace oblada::sidebar {
// Tells the tab strip when the mouse leaves the toolbar (see SetToolbarAbovePage).
class ToolbarHoverWatcher : public ui::EventHandler {
 public:
  ToolbarHoverWatcher(views::View* top_container, views::View* tab_strip)
      : top_container_(top_container), tab_strip_(tab_strip) {
    top_container->SetNotifyEnterExitOnChild(true);
    top_container->AddPreTargetHandler(this);
  }
  ~ToolbarHoverWatcher() override {
    if (top_container_.view()) {
      top_container_.view()->RemovePreTargetHandler(this);
    }
  }

  void OnMouseEvent(ui::MouseEvent* event) override {
    if (event->type() == ui::EventType::kMouseExited && tab_strip_.view()) {
      tab_strip_.view()->OnMouseExited(*event);
    }
  }

 private:
  views::ViewTracker top_container_;
  views::ViewTracker tab_strip_;
};

}  // namespace oblada::sidebar

DEFINE_UI_CLASS_PROPERTY_TYPE(oblada::sidebar::ToolbarHoverWatcher*)

namespace oblada::sidebar {

namespace {

DEFINE_UI_CLASS_PROPERTY_KEY(int, kToolbarInsetKey, -1)
DEFINE_UI_CLASS_PROPERTY_KEY(bool, kCompactKey, false)
DEFINE_UI_CLASS_PROPERTY_KEY(bool, kScaffoldKey, false)
DEFINE_UI_CLASS_PROPERTY_KEY(bool, kHiddenInSidebarKey, false)
DEFINE_UI_CLASS_PROPERTY_KEY(float, kExpansionKey, 1.0f)

DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(ToolbarHoverWatcher, kHoverWatcherKey)

// Space around the address bar in its row.
constexpr int kHorizontalPadding = 8;
constexpr int kBottomPadding = 8;

// Around the page: none on the sidebar's side, whose tabs have padding of their own.
constexpr int kContentMargin = 8;
constexpr float kContentCornerRadius = 10;

bool IsLaidOut(const views::View* view) {
  return view->GetVisible() && !view->GetProperty(views::kViewIgnoredByLayoutKey) &&
         !view->bounds().IsEmpty();
}

}  // namespace

void SetToolbarInSidebar(views::View* toolbar, int leading_inset, bool compact) {
  leading_inset = std::max(leading_inset, -1);
  compact = compact && leading_inset >= 0;
  if (ToolbarInset(toolbar) == leading_inset &&
      toolbar->GetProperty(kCompactKey) == compact) {
    return;
  }
  toolbar->SetProperty(kToolbarInsetKey, leading_inset);
  toolbar->SetProperty(kCompactKey, compact);
  toolbar->InvalidateLayout();
}

int ToolbarInset(const views::View* toolbar) {
  return toolbar->GetProperty(kToolbarInsetKey);
}

void SetScaffoldToolbar(views::View* toolbar, bool scaffold) {
  if (toolbar->GetProperty(kScaffoldKey) != scaffold) {
    toolbar->SetProperty(kScaffoldKey, scaffold);
    toolbar->InvalidateLayout();
  }
}

bool IsScaffoldToolbar(const views::View* toolbar) {
  return toolbar && toolbar->GetProperty(kScaffoldKey);
}

bool IsCompact(const views::View* toolbar) {
  return toolbar && toolbar->GetProperty(kCompactKey);
}

bool HasSidebarLocationBar(const views::View* toolbar) {
  return toolbar &&
         (ToolbarInset(toolbar) >= 0 ||
          horizontal::LayoutOf(toolbar) == Mode::kHorizonCompact);
}

bool IsCompactToolbarInUse(const views::View* toolbar) {
  if (!toolbar || !toolbar->GetProperty(kCompactKey) || !toolbar->parent()) {
    return false;
  }
  const views::View* top_container = toolbar->parent();
  if (top_container->IsMouseHovered()) {
    return true;
  }
  // A floating address bar is over the page, out of the sidebar.
  const views::FocusManager* focus_manager = top_container->GetFocusManager();
  return focus_manager && !HasFloatingLocationBar(toolbar) &&
         top_container->Contains(focus_manager->GetFocusedView());
}

void SetToolbarAbovePage(views::View* top_container,
                         views::View* page,
                         views::View* tab_strip,
                         bool above,
                         bool clip,
                         views::View* tabs) {
  views::View* parent = top_container->parent();
  if (!parent || page->parent() != parent) {
    return;
  }
  if (top_container->layer()) {
    top_container->layer()->SetMasksToBounds(clip);
  }
  const std::optional<size_t> top_index = parent->GetIndexOf(top_container);
  const std::optional<size_t> page_index = parent->GetIndexOf(page);
  if (!top_index || !page_index) {
    return;
  }
  if (above) {
    if (!top_container->GetProperty(kHoverWatcherKey)) {
      top_container->SetProperty(
          kHoverWatcherKey,
          std::make_unique<ToolbarHoverWatcher>(top_container, tab_strip));
    }
    if (!top_container->layer()) {
      // Without a clip path, which would mask the layer (and a floating address
      // bar out of it).
      top_container->SetClipPath(SkPath());
      top_container->SetPaintToLayer();
      top_container->layer()->SetFillsBoundsOpaquely(false);
      top_container->layer()->SetMasksToBounds(clip);
    }
    // Above everything else too: the toolbar is the topmost thing there, but
    // for `tabs`.
    const std::optional<size_t> tabs_index =
        tabs && tabs->parent() == parent ? parent->GetIndexOf(tabs)
                                         : std::nullopt;
    if (tabs_index) {
      if (*top_index < *page_index || *top_index > *tabs_index) {
        // Right below them, where they'll be once it has moved.
        parent->ReorderChildView(
            top_container,
            *top_index < *tabs_index ? *tabs_index - 1 : *tabs_index);
      }
    } else if (*top_index < *page_index) {
      parent->ReorderChildView(top_container, parent->children().size());
    }
  } else if (*top_index > *page_index) {
    parent->ReorderChildView(top_container, *page_index);
  }
}

void HideInSidebar(views::View* view, bool in_sidebar) {
  if (!view) {
    return;
  }
  const bool hidden = view->GetProperty(kHiddenInSidebarKey);
  if (in_sidebar && !hidden && view->GetVisible()) {
    view->SetProperty(kHiddenInSidebarKey, true);
    view->SetVisible(false);
  } else if (!in_sidebar && hidden) {
    view->SetProperty(kHiddenInSidebarKey, false);
    view->SetVisible(true);
  }
}

int LocationRowHeight(const views::View* location_bar) {
  return (location_bar ? location_bar->GetPreferredSize().height() : 0) +
         kBottomPadding;
}

int ButtonRowHeight(int toolbar_height, const views::View* location_bar) {
  return std::max(0, toolbar_height - LocationRowHeight(location_bar));
}

gfx::Rect LocationBarBounds(const gfx::Size& toolbar_size,
                            const views::View* location_bar) {
  const int height = location_bar->GetPreferredSize().height();
  return gfx::Rect(kHorizontalPadding,
                   toolbar_size.height() - kBottomPadding - height,
                   std::max(0, toolbar_size.width() - 2 * kHorizontalPadding),
                   height);
}

void ArrangeButtonRow(views::View* toolbar, const views::View* location_bar) {
  const int inset = ToolbarInset(toolbar);
  if (inset < 0 || !location_bar) {
    return;
  }
  std::vector<views::View*> leading;
  bool before_location_bar = true;
  int row_end = 0;
  for (views::View* child : toolbar->children()) {
    if (child == location_bar) {
      before_location_bar = false;
      continue;
    }
    if (!IsLaidOut(child)) {
      continue;
    }
    if (before_location_bar) {
      leading.push_back(child);
    }
    row_end = std::max(row_end, child->bounds().right());
  }
  if (leading.empty()) {
    return;
  }
  // As far from the window controls as the last button is from the edge.
  const int start = inset + (toolbar->width() - row_end);
  const int shift = leading.front()->x() - start;
  if (shift <= 0) {
    return;
  }
  for (views::View* view : leading) {
    view->SetX(view->x() - shift);
  }
}

void SetExpansion(views::View* tab_strip_region, float expansion) {
  if (tab_strip_region->GetProperty(kExpansionKey) != expansion) {
    tab_strip_region->SetProperty(kExpansionKey, expansion);
    tab_strip_region->InvalidateLayout();
  }
}

float Expansion(const views::View* tab_strip_region) {
  return tab_strip_region->GetProperty(kExpansionKey);
}

gfx::Insets ContentInsets() {
  return gfx::Insets::TLBR(kContentMargin, 0, kContentMargin, kContentMargin);
}

float ContentCornerRadius() {
  return kContentCornerRadius;
}

int SuggestionsWidth(const gfx::Rect& location_bar_in_screen) {
  if (IsFloatingLocationBarAt(location_bar_in_screen)) {
    return location_bar_in_screen.width();
  }
  constexpr int kMinimumWidth = 520;
  constexpr int kScreenMargin = 8;
  const int room = display::Screen::Get()
                       ->GetDisplayMatching(location_bar_in_screen)
                       .work_area()
                       .right() -
                   location_bar_in_screen.x() - kScreenMargin;
  return std::max(location_bar_in_screen.width(),
                  std::min(kMinimumWidth, room));
}

}  // namespace oblada::sidebar
