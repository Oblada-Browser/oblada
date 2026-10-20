#include "oblada/browser/workspaces/workspace_slider.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/tabs/common/tab_collection_animating_layout_manager.h"
#include "chrome/browser/ui/views/frame/glass_frame_service.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/grit/theme_resources.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/strings/grit/components_strings.h"
#include "components/tabs/public/tab_interface.h"
#include "oblada/browser/containers/containers.h"
#include "oblada/browser/workspaces/workspace_switcher.h"
#include "oblada/browser/workspaces/workspaces.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_features.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/render_text.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace oblada::workspaces {

namespace {

// Before a swipe counts as one, and which way.
constexpr float kAxisLock = 6;
// The share of the sidebar's width past which letting go switches...
constexpr float kCommitShare = 0.25f;
// ...as does a flick (in DIPs per millisecond).
constexpr float kFlick = 0.4f;
// How far the tabs follow a swipe with no workspace that way.
constexpr float kRubberBand = 0.12f;
// A slide: Zen's curve (a spring with no bounce, which FAST_OUT_SLOW_IN_3
// follows closely), a little quicker.
constexpr base::TimeDelta kSlide = base::Milliseconds(200);
// How long snapping may take, the fingers' speed carried on.
constexpr base::TimeDelta kSnapMin = base::Milliseconds(110);
constexpr base::TimeDelta kSnapMax = base::Milliseconds(240);
// Events further apart than this aren't the same gesture (fingers held still
// send none).
constexpr base::TimeDelta kGestureGap = base::Seconds(1);
// After a switch: how often the tabs under the preview are looked at, how long
// it waits for them at least and at most, and how it fades.
constexpr base::TimeDelta kHandOffCheck = base::Milliseconds(16);
constexpr base::TimeDelta kHandOffMin = base::Milliseconds(60);
constexpr base::TimeDelta kHandOffMax = base::Milliseconds(900);
constexpr base::TimeDelta kFade = base::Milliseconds(100);

// Where the workspace's tabs are drawn, and how, measured on the ones shown so
// that the next workspace's look the same.
struct Geometry {
  // In the tab list, as if unscrolled.
  gfx::Rect header{12, 4, 256, 26};
  int rows_top = 36;
  int row_gap = 2;
  int tab_height = 30;
  // A split's card: its height, the inset of its tabs and the gap between them.
  int split_height = 36;
  int split_inset = 3;
  int split_gap = 2;

  // In the header.
  gfx::Rect header_icon{8, 5, 16, 16};
  gfx::Rect header_label{32, 3, 216, 20};
  gfx::FontList header_font;
  std::optional<SkColor> header_color;

  // In a tab: its icon (the favicon centered in it), its title (and how far it
  // ends from the tab's end, with the close button shown or not) and the close
  // button's icon (its x from the tab's end).
  gfx::Rect icon{5, 4, 22, 22};
  gfx::Rect title{32, 5, 0, 20};
  int title_end = 12;
  int title_end_with_close = 32;
  gfx::Rect close{24, 7, 16, 16};
  gfx::ImageSkia close_icon;
  gfx::FontList title_font;
  std::optional<SkColor> title_color;
  std::optional<SkColor> active_title_color;
  std::optional<SkColor> active_background;
  std::optional<SkColor> split_background;

  // "New tab" below the tabs: how far below the last, and its parts.
  int new_tab_gap = 10;
  int new_tab_height = 32;
  gfx::Rect new_tab_icon{5, 6, 20, 20};
  gfx::ImageSkia new_tab_image;
  gfx::Rect new_tab_label{32, 0, 200, 32};
  std::u16string new_tab_text;
  gfx::FontList new_tab_font;
  std::optional<SkColor> new_tab_color;
};

void FindAll(views::View* view,
             std::string_view class_name,
             std::vector<views::View*>& found) {
  if (!view->GetVisible()) {
    return;
  }
  if (std::string_view(view->GetClassName()) == class_name) {
    found.push_back(view);
  }
  for (views::View* child : view->children()) {
    FindAll(child, class_name, found);
  }
}

views::View* FindFirst(views::View* view, std::string_view class_name) {
  std::vector<views::View*> found;
  FindAll(view, class_name, found);
  return found.empty() ? nullptr : found.front();
}

views::Label* FindLabel(views::View* view) {
  if (!view->GetVisible()) {
    return nullptr;
  }
  if (auto* label = views::AsViewClass<views::Label>(view)) {
    return label;
  }
  for (views::View* child : view->children()) {
    if (views::Label* label = FindLabel(child)) {
      return label;
    }
  }
  return nullptr;
}

views::ImageView* FindImage(views::View* view) {
  return views::AsViewClass<views::ImageView>(FindFirst(view, "ImageView"));
}

// `view`'s bounds in `ancestor`.
gfx::Rect In(views::View* view, views::View* ancestor) {
  return views::View::ConvertRectToTarget(view->parent(), ancestor,
                                          view->bounds());
}

// The view of the tabs that are the workspace's: the scrolling list around the
// unpinned ones (the pinned ones are every workspace's).
views::View* FindTabList(views::View* sidebar) {
  views::View* unpinned = FindFirst(sidebar, "UnpinnedTabContainerView");
  for (views::View* view = unpinned; view && view != sidebar;
       view = view->parent()) {
    if (std::string_view(view->GetClassName()) == "ScrollView") {
      return view;
    }
  }
  return unpinned;
}

// The rows of a group: its tabs, and its splits' cards.
std::vector<views::View*> RowsOf(views::View* group) {
  std::vector<views::View*> rows;
  for (views::View* child : group->children()) {
    const std::string_view name = child->GetClassName();
    if (child->GetVisible() && !child->bounds().IsEmpty() &&
        (name == "TabView" || name == "SplitTabView")) {
      rows.push_back(child);
    }
  }
  std::ranges::sort(rows, {}, [](views::View* row) { return row->y(); });
  return rows;
}

Geometry Measure(views::View* list,
                 views::View* sidebar,
                 const tabs::TabInterface* active) {
  Geometry geometry;
  views::View* contents = FindFirst(list, "UnpinnedTabContainerView");
  views::View* group = contents ? FindFirst(contents, "TabGroupView") : nullptr;
  if (!group) {
    return geometry;
  }
  // Where they'd be unscrolled.
  const gfx::Vector2d scroll =
      In(contents, list).origin() - gfx::Point() -
      (contents->parent() ? In(contents->parent(), list).origin() - gfx::Point()
                          : gfx::Vector2d());

  if (views::View* header = FindFirst(group, "TabGroupHeaderView")) {
    geometry.header = In(header, list) - scroll;
    geometry.rows_top = geometry.header.bottom() + 6;
    if (views::ImageView* icon = FindImage(header)) {
      geometry.header_icon = In(icon, header);
    }
    if (views::Label* label = FindLabel(header)) {
      geometry.header_label = In(label, header);
      geometry.header_font = label->font_list();
      geometry.header_color = label->GetEnabledColor();
    }
  }

  const std::vector<views::View*> rows = RowsOf(group);
  if (!rows.empty()) {
    geometry.rows_top = In(rows.front(), list).y() - scroll.y();
    if (rows.size() > 1) {
      geometry.row_gap = rows[1]->y() - rows[0]->bounds().bottom();
    }
  }
  auto measure_tab = [&](TabView* tab, bool in_split) {
    const bool is_active = tab->GetTabInterface() == active;
    const std::optional<SkColor> background = tab->GetBackgroundColor();
    if (is_active && background) {
      geometry.active_background = background;
    } else if (in_split && background) {
      geometry.split_background = background;
    }
    if (in_split) {
      return;
    }
    geometry.tab_height = tab->height();
    if (views::View* icon = FindFirst(tab, "TabIcon")) {
      geometry.icon = In(icon, tab);
    }
    views::View* close = FindFirst(tab, "TabCloseButton");
    if (close && close->bounds().IsEmpty()) {
      close = nullptr;
    }
    if (views::Label* title = FindLabel(tab)) {
      geometry.title = In(title, tab);
      (close ? geometry.title_end_with_close : geometry.title_end) =
          tab->width() - geometry.title.right();
      geometry.title_font = title->font_list();
      (is_active ? geometry.active_title_color : geometry.title_color) =
          title->GetEnabledColor();
    }
    if (close) {
      if (views::ImageView* icon = FindImage(close)) {
        const gfx::Rect bounds = In(icon, tab);
        geometry.close = gfx::Rect(tab->width() - bounds.x(), bounds.y(),
                                   bounds.width(), bounds.height());
        geometry.close_icon = icon->GetImage();
      }
    }
  };
  for (views::View* row : rows) {
    if (auto* tab = views::AsViewClass<TabView>(row)) {
      measure_tab(tab, /*in_split=*/false);
      continue;
    }
    // A split's card.
    geometry.split_height = row->height();
    std::vector<views::View*> halves;
    for (views::View* child : row->children()) {
      if (views::IsViewClass<TabView>(child) && child->GetVisible()) {
        halves.push_back(child);
      }
    }
    std::ranges::sort(halves, {}, [](views::View* half) { return half->x(); });
    if (halves.size() == 2) {
      geometry.split_inset = halves[0]->y();
      geometry.split_gap = halves[1]->x() - halves[0]->bounds().right();
    }
    for (views::View* half : halves) {
      measure_tab(views::AsViewClass<TabView>(half), /*in_split=*/true);
    }
  }

  if (views::View* new_tab = FindFirst(sidebar, "NewTabButton")) {
    const gfx::Rect bounds = In(new_tab, list);
    if (!rows.empty()) {
      const int gap = bounds.y() - In(rows.back(), list).bottom();
      if (gap >= 0 && gap <= 40) {
        geometry.new_tab_gap = gap;
      }
    }
    geometry.new_tab_height = bounds.height();
    if (views::ImageView* icon = FindImage(new_tab)) {
      geometry.new_tab_icon = In(icon, new_tab);
      geometry.new_tab_image = icon->GetImage();
    }
    if (views::Label* label = FindLabel(new_tab)) {
      geometry.new_tab_label = In(label, new_tab);
      geometry.new_tab_text = label->GetText();
      geometry.new_tab_font = label->font_list();
      geometry.new_tab_color = label->GetEnabledColor();
    }
  }
  return geometry;
}

void DrawText(gfx::Canvas* canvas,
              const std::u16string& text,
              const gfx::FontList& font,
              SkColor color,
              const gfx::Rect& bounds,
              gfx::ElideBehavior elide) {
  std::unique_ptr<gfx::RenderText> render = gfx::RenderText::CreateRenderText();
  render->SetText(text);
  render->SetFontList(font);
  render->SetColor(color);
  render->SetCursorEnabled(false);
  render->SetElideBehavior(elide);
  render->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  render->SetVerticalAlignment(gfx::ALIGN_MIDDLE);
  render->SetDisplayRect(bounds);
  render->Draw(canvas);
}

// `image` centered in `bounds`, at most as big.
void DrawCentered(gfx::Canvas* canvas,
                  const gfx::ImageSkia& image,
                  const gfx::Rect& bounds) {
  if (image.isNull()) {
    return;
  }
  const int width = std::min(image.width(), bounds.width());
  const int height = std::min(image.height(), bounds.height());
  canvas->DrawImageInt(image, 0, 0, image.width(), image.height(),
                       bounds.x() + (bounds.width() - width) / 2,
                       bounds.y() + (bounds.height() - height) / 2, width,
                       height, true);
}

gfx::Transform Translation(float x) {
  gfx::Transform transform;
  transform.Translate(x, 0);
  return transform;
}

// Where the tabs of the list, and "New tab" below them, are and how visible: it
// changes as long as they're moving.
std::string TabsSignature(views::View* sidebar) {
  std::string signature;
  std::vector<views::View*> views;
  for (std::string_view name : {"TabGroupHeaderView", "TabView", "SplitTabView",
                                "NewTabButton", "ScrollView"}) {
    FindAll(sidebar, name, views);
  }
  for (views::View* view : views) {
    base::StrAppend(&signature, {In(view, sidebar).ToString(), ";"});
    if (view->layer()) {
      base::StrAppend(
          &signature,
          {base::NumberToString(std::lround(view->layer()->opacity() * 100)),
           ";"});
    }
  }
  return signature;
}

// Ends the animations with which the tab strip shows its changes (tabs moving,
// groups growing or shrinking, new tabs fading in): the tabs right where they
// go.
void SnapTabs(views::View* sidebar) {
  std::vector<views::View*> hosts;
  std::vector<views::View*> pending = {sidebar};
  while (!pending.empty()) {
    views::View* view = pending.back();
    pending.pop_back();
    if (view->GetProperty(kHasAnimatingLayoutManagerKey)) {
      hosts.push_back(view);
    }
    for (views::View* child : view->children()) {
      pending.push_back(child);
    }
  }
  for (views::View* host : hosts) {
    static_cast<TabCollectionAnimatingLayoutManager*>(host->GetLayoutManager())
        ->SnapToTarget();
  }
}

// Whether `browser`'s window is see-through (macOS's glass behind its sidebar).
bool OnGlass(BrowserWindowInterface* browser) {
  GlassFrameService* glass = GlassFrameService::GetInstance();
  return features::IsGlassFrameEnabled() && glass &&
         glass->IsBrowserWindowEligible(browser);
}

// Gives `view` a layer of its own, to move it without painting it again.
void EnsureLayer(views::View* view) {
  if (!view->layer()) {
    view->SetPaintToLayer();
    view->layer()->SetFillsBoundsOpaquely(false);
  }
}

}  // namespace

// The workspace coming in during a slide, drawn the way its tabs will look.
class WorkspacePreview : public views::View {
  METADATA_HEADER(WorkspacePreview, views::View)

 public:
  WorkspacePreview(BrowserWindowInterface* browser,
                   std::optional<std::string> id,
                   Geometry geometry)
      : browser_(browser), id_(std::move(id)), geometry_(std::move(geometry)) {
    SetProperty(views::kViewIgnoredByLayoutKey, true);
    SetCanProcessEventsWithinSubtree(false);
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    if (!id_) {
      return;
    }
    if (std::optional<Workspace> workspace =
            FindWorkspace(browser->GetProfile(), *id_)) {
      name_ = base::UTF8ToUTF16(workspace->name);
      container_ = workspace->container;
    }
    // Its tabs, a split's two on one row.
    for (tabs::TabInterface* tab : TabsOf(browser, *id_)) {
      if (!rows_.empty() && tab->GetSplit() &&
          rows_.back().split == tab->GetSplit()) {
        rows_.back().tabs.push_back(tab->GetWeakPtr());
        continue;
      }
      rows_.push_back({tab->GetSplit(), {tab->GetWeakPtr()}});
    }
    if (tabs::TabInterface* tab = LastActiveTabOf(browser, *id_)) {
      active_ = tab->GetWeakPtr();
    } else if (!rows_.empty()) {
      active_ = rows_.front().tabs.front();
    }
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    // Opaque, it hides the tabs going away as it takes their place; on glass, the
    // sidebar shows through it as through the tabs.
    if (!OnGlass(browser_)) {
      canvas->DrawColor(colors->GetColor(ui::kColorFrameActive));
    }
    if (!id_) {
      return;
    }
    const Geometry& geometry = geometry_;
    const SkColor active_text = geometry.active_title_color.value_or(
        colors->GetColor(kColorTabForegroundActiveFrameActive));

    // The workspace's icon and name, as in its header.
    const gfx::Rect& header = geometry.header;
    const ui::ImageModel icon =
        WorkspaceIcon(browser_, name_, geometry.header_icon.width());
    if (!icon.IsEmpty()) {
      DrawCentered(canvas, icon.Rasterize(colors),
                   geometry.header_icon + header.OffsetFromOrigin());
    }
    DrawText(canvas, name_, geometry.header_font,
             geometry.header_color.value_or(active_text),
             geometry.header_label + header.OffsetFromOrigin(),
             gfx::ELIDE_TAIL);

    // Its tabs (with none, the new one it gets), the one it showed last
    // highlighted.
    int y = geometry.rows_top;
    const int x = header.x();
    const int width = header.width();
    if (rows_.empty()) {
      PaintTab(canvas, nullptr, gfx::Rect(x, y, width, geometry.tab_height),
               /*active=*/true, /*in_split=*/false);
      y += geometry.tab_height + geometry.row_gap;
    }
    for (const Row& row : rows_) {
      if (row.tabs.size() < 2) {
        PaintTab(canvas, row.tabs.front().get(),
                 gfx::Rect(x, y, width, geometry.tab_height),
                 row.tabs.front().get() == active_.get(), /*in_split=*/false);
        y += geometry.tab_height + geometry.row_gap;
        continue;
      }
      // A split's card, its tabs side by side.
      const gfx::Rect card(x, y, width, geometry.split_height);
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(SkColorSetA(
          colors->GetColor(kColorTabForegroundInactiveFrameActive), 0x1A));
      canvas->DrawRoundRect(
          gfx::RectF(card),
          GetLayoutConstant(LayoutConstant::kVerticalTabCornerRadius) +
              geometry.split_inset,
          flags);
      const int inner = width - 2 * geometry.split_inset - geometry.split_gap;
      gfx::Rect half(card.x() + geometry.split_inset,
                     card.y() + geometry.split_inset, inner / 2,
                     geometry.tab_height);
      for (const base::WeakPtr<tabs::TabInterface>& tab : row.tabs) {
        PaintTab(canvas, tab.get(), half, tab.get() == active_.get(),
                 /*in_split=*/true);
        half = gfx::Rect(half.right() + geometry.split_gap, half.y(),
                         inner - inner / 2, half.height());
      }
      y += geometry.split_height + geometry.row_gap;
    }

    // "New tab", below them.
    const gfx::Rect new_tab(x, y - geometry.row_gap + geometry.new_tab_gap,
                            width, geometry.new_tab_height);
    const SkColor new_tab_color = geometry.new_tab_color.value_or(active_text);
    DrawCentered(canvas,
                 geometry.new_tab_image.isNull()
                     ? ui::ImageModel::FromVectorIcon(kAddIcon, new_tab_color,
                                                      16)
                           .Rasterize(colors)
                     : geometry.new_tab_image,
                 geometry.new_tab_icon + new_tab.OffsetFromOrigin());
    DrawText(canvas, geometry.new_tab_text, geometry.new_tab_font,
             new_tab_color, geometry.new_tab_label + new_tab.OffsetFromOrigin(),
             gfx::ELIDE_TAIL);
  }

 private:
  struct Row {
    std::optional<split_tabs::SplitTabId> split;
    std::vector<base::WeakPtr<tabs::TabInterface>> tabs;
  };

  // A tab at `bounds`; with no `tab`, the new tab the workspace gets.
  void PaintTab(gfx::Canvas* canvas,
                tabs::TabInterface* tab,
                const gfx::Rect& bounds,
                bool active,
                bool in_split) {
    const ui::ColorProvider* colors = GetColorProvider();
    const Geometry& geometry = geometry_;
    const std::optional<SkColor> background =
        active ? geometry.active_background.value_or(
                     colors->GetColor(kColorTabBackgroundActiveFrameActive))
               : (in_split ? geometry.split_background : std::nullopt);
    if (background) {
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(*background);
      canvas->DrawRoundRect(
          gfx::RectF(bounds),
          GetLayoutConstant(LayoutConstant::kVerticalTabCornerRadius), flags);
    }
    gfx::ImageSkia favicon;
    std::u16string title;
    if (tab) {
      containers::PaintTabIndicator(canvas, bounds, /*vertical=*/true, tab);
      if (TabUIHelper* helper = TabUIHelper::From(tab)) {
        const ui::ImageModel image = helper->GetFavicon();
        if (!image.IsEmpty()) {
          favicon = image.Rasterize(colors);
        }
        title = helper->GetTitle();
      }
    } else {
      containers::PaintContainerIndicator(canvas, bounds, /*vertical=*/true,
                                          browser_->GetProfile(), container_);
      if (const gfx::ImageSkia* image =
              ui::ResourceBundle::GetSharedInstance().GetImageSkiaNamed(
                  IDR_NTP_FAVICON)) {
        favicon = *image;
      }
      title = l10n_util::GetStringUTF16(IDS_NEW_TAB_TITLE);
    }
    gfx::Rect icon = geometry.icon + bounds.OffsetFromOrigin();
    icon.ClampToCenteredSize(gfx::Size(16, 16));
    if (!favicon.isNull()) {
      canvas->DrawImageInt(favicon, 0, 0, favicon.width(), favicon.height(),
                           icon.x(), icon.y(), icon.width(), icon.height(),
                           true);
    }
    // The active tab shows its close button, but in a split.
    const bool close = active && !in_split;
    gfx::Rect title_bounds = geometry.title + bounds.OffsetFromOrigin();
    title_bounds.set_width(std::max(
        0, bounds.right() -
               (close ? geometry.title_end_with_close : geometry.title_end) -
               title_bounds.x()));
    DrawText(canvas, title, geometry.title_font,
             active ? geometry.active_title_color.value_or(colors->GetColor(
                          kColorTabForegroundActiveFrameActive))
                    : geometry.title_color.value_or(colors->GetColor(
                          kColorTabForegroundInactiveFrameActive)),
             title_bounds, gfx::FADE_TAIL);
    if (close && !geometry.close_icon.isNull()) {
      DrawCentered(canvas, geometry.close_icon,
                   gfx::Rect(bounds.right() - geometry.close.x(),
                             bounds.y() + geometry.close.y(),
                             geometry.close.width(), geometry.close.height()));
    }
  }

  raw_ptr<BrowserWindowInterface> browser_;
  std::optional<std::string> id_;
  Geometry geometry_;
  std::u16string name_;
  std::string container_;
  std::vector<Row> rows_;
  base::WeakPtr<tabs::TabInterface> active_;
};

BEGIN_METADATA(WorkspacePreview)
END_METADATA

WorkspaceSlider::WorkspaceSlider(BrowserWindowInterface* browser,
                                 views::View* sidebar)
    : browser_(browser), sidebar_(sidebar) {
  sidebar_->AddPreTargetHandler(this);
  SetSwitchAnimator(browser_, base::BindRepeating(&WorkspaceSlider::SlideTo,
                                                  weak_factory_.GetWeakPtr()));
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WorkspaceSlider::Prepare,
                                weak_factory_.GetWeakPtr()));
}

WorkspaceSlider::~WorkspaceSlider() {
  SetSwitchAnimator(browser_, SwitchAnimator());
  sidebar_->RemovePreTargetHandler(this);
  finish_timer_.Stop();
  hand_off_timer_.Stop();
  Reset();
}

void WorkspaceSlider::Prepare() {
  if (views::View* list = FindTabList(sidebar_)) {
    EnsureLayer(list);
  }
  if (views::View* new_tab =
          FindFirst(sidebar_, "VerticalTabStripBottomContainer")) {
    EnsureLayer(new_tab);
  }
}

void WorkspaceSlider::SlideTo(const std::string& id, bool forward) {
  // One at a time: the one under way ends at once.
  if (settling_) {
    Finish();
    EndHandOff();
  }
  if (swiping_ || !gfx::Animation::ShouldRenderRichAnimation() || !Begin()) {
    ShowNow(browser_, id);
    return;
  }
  ShowPreview(id, forward);
  const bool from_right = forward != base::i18n::IsRTL();
  Settle(from_right ? -tabs_bounds_.width() : tabs_bounds_.width(),
         /*commit=*/true, {gfx::Tween::FAST_OUT_SLOW_IN_3, kSlide});
}

void WorkspaceSlider::OnScrollEvent(ui::ScrollEvent* event) {
  // An event the sidebar's views pass on to one another (unhandled) comes back.
  if (event == last_event_ && event->time_stamp() == last_event_time_) {
    return;
  }
  const base::TimeTicks previous_time = last_event_time_;
  last_event_ = event;
  last_event_time_ = event->time_stamp();
  auto consume = [event] {
    event->SetHandled();
    event->StopPropagation();
  };

  // On macOS a trackpad gesture's phases come as momentum phases: MAY_BEGIN as
  // the fingers touch, NONE as they move, END as they lift, then INERTIAL_UPDATE
  // and END for the momentum that follows (scroll_event_phase() is left unset).
  const ui::EventMomentumPhase momentum = event->momentum_phase();
  const ui::ScrollEventPhase phase = event->scroll_event_phase();
  const bool fingers_lifted =
      phase == ui::ScrollEventPhase::kEnd ||
      event->type() == ui::EventType::kScrollFlingStart ||
      event->type() == ui::EventType::kScrollFlingCancel ||
      momentum == ui::EventMomentumPhase::END ||
      momentum == ui::EventMomentumPhase::INERTIAL_UPDATE;
  if (fingers_lifted) {
    if (axis_ == Axis::kHorizontal) {
      // Its momentum is the swipe's: it doesn't scroll the tabs.
      consume();
    }
    if (swiping_) {
      Release();
    }
    return;
  }
  const bool begins = phase == ui::ScrollEventPhase::kBegan ||
                      momentum == ui::EventMomentumPhase::MAY_BEGIN ||
                      event->time_stamp() - previous_time > kGestureGap;
  if (begins) {
    if (swiping_) {
      Release();
    }
    axis_ = Axis::kNone;
    x_ = y_ = velocity_ = 0;
  }

  x_ += event->x_offset();
  y_ += event->y_offset();
  // The fingers' speed, smoothed over the last few events.
  if (!begins && event->x_offset() != 0) {
    const double ms = std::max(
        (event->time_stamp() - previous_time).InMillisecondsF(), 4.0);
    velocity_ = 0.6f * static_cast<float>(event->x_offset() / ms) +
                0.4f * velocity_;
    last_move_time_ = event->time_stamp();
  }
  if (axis_ == Axis::kNone) {
    if (std::abs(y_) >= kAxisLock && std::abs(y_) > std::abs(x_)) {
      axis_ = Axis::kVertical;
    } else if (std::abs(x_) >= kAxisLock && std::abs(x_) > std::abs(y_)) {
      axis_ = Axis::kHorizontal;
      swiping_ = !settling_ && Begin();
    }
  }
  if (axis_ != Axis::kHorizontal) {
    return;
  }
  consume();
  if (swiping_) {
    Track(x_);
    // Should the end of the gesture never come, it ends once it stops.
    release_timer_.Start(FROM_HERE, kGestureGap,
                         base::BindOnce(&WorkspaceSlider::Release,
                                        base::Unretained(this)));
  }
}

bool WorkspaceSlider::Begin() {
  EndHandOff();
  tabs_ = FindTabList(sidebar_);
  if (!tabs_ || tabs_->bounds().IsEmpty()) {
    tabs_ = nullptr;
    return false;
  }
  tabs_bounds_ = In(tabs_, sidebar_);
  // Down to the workspaces' buttons, which stay.
  int bottom = sidebar_->height();
  if (views::View* switcher = FindFirst(sidebar_, "WorkspaceSwitcher")) {
    bottom = In(switcher, sidebar_).y();
  }
  tabs_bounds_.set_height(
      std::max(tabs_bounds_.height(), bottom - tabs_bounds_.y()));
  moving_ = {tabs_};
  if (views::View* new_tab =
          FindFirst(sidebar_, "VerticalTabStripBottomContainer")) {
    moving_.push_back(new_tab);
  }
  for (views::View* view : moving_) {
    EnsureLayer(view);
    // The pointer stays put as they move under it: nothing to hover.
    view->SetCanProcessEventsWithinSubtree(false);
  }
  // What moves out of the sidebar is out of sight.
  if (sidebar_->layer()) {
    masked_before_ = sidebar_->layer()->GetMasksToBounds();
    sidebar_->layer()->SetMasksToBounds(true);
  }
  target_.reset();
  offset_ = 0;
  return true;
}

void WorkspaceSlider::Track(float distance) {
  // Which way the fingers go: towards the next workspace, or the previous one.
  const bool forward = (distance < 0) != base::i18n::IsRTL();
  if (!preview_ || forward != forward_) {
    const std::optional<Workspace> neighbor = Neighbor(browser_, forward);
    ShowPreview(neighbor ? std::make_optional(neighbor->id) : std::nullopt,
                forward);
  }
  // Nowhere to go that way: a little give, no more.
  if (!target_) {
    const float limit = tabs_bounds_.width() * kRubberBand;
    distance = std::copysign(
        limit * (1 - std::exp(-std::abs(distance) / limit)), distance);
  }
  Place(distance);
}

void WorkspaceSlider::Place(float offset) {
  offset_ = offset;
  for (views::View* view : moving_) {
    view->layer()->SetTransform(Translation(offset));
  }
  if (preview_) {
    // Beside them, on the side it comes in from.
    const bool from_right = forward_ != base::i18n::IsRTL();
    preview_->layer()->SetTransform(Translation(
        offset + (from_right ? tabs_bounds_.width() : -tabs_bounds_.width())));
  }
}

void WorkspaceSlider::ShowPreview(std::optional<std::string> id, bool forward) {
  forward_ = forward;
  target_ = id;
  if (preview_) {
    sidebar_->RemoveChildViewT(preview_.get());
    preview_ = nullptr;
  }
  TabStripModel* model = browser_->GetTabStripModel();
  preview_ = sidebar_->AddChildView(std::make_unique<WorkspacePreview>(
      browser_, std::move(id),
      Measure(tabs_, sidebar_, model ? model->GetActiveTab() : nullptr)));
  preview_->SetBoundsRect(tabs_bounds_);
  Place(offset_);
}

void WorkspaceSlider::Release() {
  if (!swiping_) {
    return;
  }
  swiping_ = false;
  release_timer_.Stop();
  // Fingers that stopped before lifting aren't flicking.
  if (base::TimeTicks::Now() - last_move_time_ > base::Milliseconds(80)) {
    velocity_ = 0;
  }
  // Snaps to one workspace or the other: the next, if dragged far enough or
  // flicked towards it, else back.
  const float width = tabs_bounds_.width();
  const bool flicked_towards =
      std::abs(velocity_) > kFlick && (velocity_ < 0) == (offset_ < 0);
  const bool flicked_back =
      std::abs(velocity_) > kFlick && (velocity_ < 0) != (offset_ < 0);
  const bool commit =
      target_ && !flicked_back &&
      (std::abs(offset_) > width * kCommitShare || flicked_towards);
  const float to = commit ? (offset_ < 0 ? -width : width) : 0;
  const float distance = std::abs(to - offset_);
  const float speed = (to - offset_) * velocity_ > 0 ? std::abs(velocity_) : 0;

  // Moving on at the fingers' speed, slowing down to a stop: of the ease-outs,
  // the one whose start (that many times its average speed) is closest to it in
  // about as long as a slide, for as long as that start takes. Fingers that
  // weren't going that way start from rest, as Zen's spring does.
  struct Curve {
    gfx::Tween::Type tween;
    float start;
  };
  static constexpr Curve kCurves[] = {
      {gfx::Tween::EASE_OUT, 2},               // 1 - (1 - t)^2
      {gfx::Tween::ACCEL_LIN_DECEL_60, 2.5f},  // cubic-bezier(0, 0, .4, 1)
      {gfx::Tween::LINEAR_OUT_SLOW_IN, 5},     // cubic-bezier(0, 0, .2, 1)
  };
  Snap snap{gfx::Tween::FAST_OUT_SLOW_IN_3, kSlide};
  const float start =
      distance > 0 ? speed * kSlide.InMillisecondsF() / distance : 0;
  if (start >= 1) {
    const Curve* curve = &kCurves[0];
    for (const Curve& other : kCurves) {
      if (std::abs(other.start - start) < std::abs(curve->start - start)) {
        curve = &other;
      }
    }
    snap = {curve->tween,
            std::clamp(base::Milliseconds(curve->start * distance / speed),
                       kSnapMin, kSnapMax)};
  }
  if (distance < 1) {
    snap.duration = base::TimeDelta();
  }
  Settle(to, commit, snap);
}

void WorkspaceSlider::Settle(float offset, bool commit, Snap snap) {
  settling_ = true;
  commit_ = commit;
  if (snap.duration.is_zero() || !sidebar_->GetWidget() ||
      !sidebar_->GetWidget()->GetCompositor()) {
    Place(offset);
    Finish();
    return;
  }
  // On the compositor.
  {
    views::AnimationBuilder builder;
    builder
        .SetPreemptionStrategy(ui::LayerAnimator::IMMEDIATELY_SET_NEW_TARGET)
        .OnEnded(base::BindOnce(&WorkspaceSlider::OnSettled,
                                weak_factory_.GetWeakPtr()))
        .OnAborted(base::BindOnce(&WorkspaceSlider::OnSettled,
                                  weak_factory_.GetWeakPtr()));
    views::AnimationSequenceBlock& block =
        builder.Once().SetDuration(snap.duration);
    for (views::View* view : moving_) {
      block.SetTransform(view->layer(), Translation(offset), snap.tween);
    }
    if (preview_) {
      const bool from_right = forward_ != base::i18n::IsRTL();
      block.SetTransform(
          preview_->layer(),
          Translation(offset + (from_right ? tabs_bounds_.width()
                                           : -tabs_bounds_.width())),
          snap.tween);
    }
  }
  offset_ = offset;
  // Should its frames not come.
  finish_timer_.Start(FROM_HERE, snap.duration + base::Milliseconds(500),
                      base::BindOnce(&WorkspaceSlider::Finish,
                                     base::Unretained(this)));
}

void WorkspaceSlider::OnSettled() {
  // Not from within the animator.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&WorkspaceSlider::Finish, weak_factory_.GetWeakPtr()));
}

void WorkspaceSlider::Finish() {
  if (!settling_) {
    return;
  }
  settling_ = false;
  finish_timer_.Stop();
  // Where they were going, if they aren't there yet.
  for (views::View* view : moving_) {
    view->layer()->GetAnimator()->StopAnimating();
  }
  if (preview_) {
    preview_->layer()->GetAnimator()->StopAnimating();
  }
  const bool switching = commit_ && target_ && preview_;
  if (switching) {
    // The tabs, now the next workspace's, back in place under its preview: right
    // where they go, as it shows them.
    ShowNow(browser_, *target_);
    SnapTabs(sidebar_);
  }
  for (views::View* view : moving_) {
    view->layer()->SetTransform(gfx::Transform());
    view->SetCanProcessEventsWithinSubtree(true);
  }
  moving_.clear();
  if (sidebar_->layer()) {
    sidebar_->layer()->SetMasksToBounds(masked_before_);
  }
  if (switching) {
    HandOff();
  } else if (preview_) {
    sidebar_->RemoveChildViewT(preview_.get());
    preview_ = nullptr;
  }
  tabs_ = nullptr;
  target_.reset();
  commit_ = false;
  offset_ = 0;
}

void WorkspaceSlider::HandOff() {
  hand_off_start_ = base::TimeTicks::Now();
  tabs_signature_.clear();
  stable_checks_ = 0;
  hand_off_timer_.Start(FROM_HERE, kHandOffCheck, this,
                        &WorkspaceSlider::CheckHandOff);
}

void WorkspaceSlider::CheckHandOff() {
  if (!preview_) {
    hand_off_timer_.Stop();
    return;
  }
  // What changes still (e.g. once a new tab's page loads) shows at once too.
  SnapTabs(sidebar_);
  std::string signature = TabsSignature(sidebar_);
  if (signature == tabs_signature_) {
    ++stable_checks_;
  } else {
    stable_checks_ = 0;
    tabs_signature_ = std::move(signature);
  }
  const base::TimeDelta waited = base::TimeTicks::Now() - hand_off_start_;
  if ((stable_checks_ >= 3 && waited >= kHandOffMin) || waited >= kHandOffMax) {
    hand_off_timer_.Stop();
    FadePreview();
  }
}

void WorkspaceSlider::FadePreview() {
  if (!preview_) {
    return;
  }
  RemoveFaded();
  fading_ = preview_;
  preview_ = nullptr;
  auto remove = [](base::WeakPtr<WorkspaceSlider> slider) {
    // Not from within the animator.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&WorkspaceSlider::RemoveFaded, slider));
  };
  views::AnimationBuilder()
      .OnEnded(base::BindOnce(remove, weak_factory_.GetWeakPtr()))
      .OnAborted(base::BindOnce(remove, weak_factory_.GetWeakPtr()))
      .Once()
      .SetDuration(kFade)
      .SetOpacity(fading_->layer(), 0, gfx::Tween::EASE_OUT);
}

void WorkspaceSlider::RemoveFaded() {
  if (fading_) {
    WorkspacePreview* fading = fading_;
    fading_ = nullptr;
    sidebar_->RemoveChildViewT(fading);
  }
}

void WorkspaceSlider::EndHandOff() {
  hand_off_timer_.Stop();
  RemoveFaded();
  if (preview_ && !settling_ && !swiping_) {
    sidebar_->RemoveChildViewT(preview_.get());
    preview_ = nullptr;
  }
}

void WorkspaceSlider::Reset() {
  RemoveFaded();
  if (preview_ && preview_->parent() == sidebar_) {
    sidebar_->RemoveChildViewT(preview_.get());
  }
  preview_ = nullptr;
  for (views::View* view : moving_) {
    if (view->layer()) {
      view->layer()->GetAnimator()->AbortAllAnimations();
      view->layer()->SetTransform(gfx::Transform());
    }
    view->SetCanProcessEventsWithinSubtree(true);
  }
  moving_.clear();
  tabs_ = nullptr;
}

}  // namespace oblada::workspaces
