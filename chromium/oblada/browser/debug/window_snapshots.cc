#include "oblada/browser/debug/window_snapshots.h"

#include <sstream>
#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/omnibox/omnibox_view.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "oblada/browser/new_tab/new_tab_ui.h"
#include "components/viz/common/frame_sinks/copy_output_request.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/debug_utils.h"
#include "url/gurl.h"
#include "ui/base/page_transition_types.h"
#include "content/public/common/referrer.h"
#include "content/public/browser/navigation_controller.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "oblada/browser/workspaces/workspace_switcher.h"
#include "oblada/browser/peek/peek.h"
#include "oblada/browser/workspaces/workspaces.h"
#include "ui/views/widget/widget.h"

#if BUILDFLAG(IS_MAC)
#include "oblada/browser/debug/window_controls_mac.h"
#endif

namespace oblada::debug {

namespace {

constexpr char kSwitch[] = "oblada-snapshot-dir";

// Consumes the request file, if there is one, returning what it asks to do first.
std::optional<std::string> TakeRequest(const base::FilePath& dir) {
  const base::FilePath request = dir.AppendASCII("request");
  std::string actions;
  if (!base::ReadFileToString(request, &actions) || !base::DeleteFile(request)) {
    return std::nullopt;
  }
  return actions;
}

// Writes the window's views, and their bounds in the window, to <dir>/layout.txt
// before the snapshot itself (<name>.png), which is what tools wait for.
void WriteFiles(const base::FilePath& dir,
                std::string name,
                std::string layout,
                std::vector<uint8_t> png) {
  if (!layout.empty()) {
    base::WriteFile(dir.AppendASCII("layout.txt"), layout);
  }
  const base::FilePath temp = dir.AppendASCII(name + ".png.tmp");
  if (base::WriteFile(temp, png)) {
    base::Move(temp, dir.AppendASCII(name + ".png"));
  }
}

const views::View* FindView(const views::View* view, std::string_view class_name) {
  if (view->GetVisible() && view->GetClassName() == class_name) {
    return view;
  }
  for (const views::View* child : view->children()) {
    if (const views::View* found = FindView(child, class_name)) {
      return found;
    }
  }
  return nullptr;
}

void FindViews(views::View* view,
               std::string_view class_name,
               std::vector<views::View*>& found) {
  if (!view->GetVisible()) {
    return;
  }
  if (view->GetClassName() == class_name) {
    found.push_back(view);
  }
  for (views::View* child : view->children()) {
    FindViews(child, class_name, found);
  }
}

// Sends the window's views a left button mouse event at `point` (in the window),
// straight to them: the window itself would check that a real button is down.
void SendMouse(views::Widget* widget, ui::EventType type, const gfx::Point& point) {
  ui::MouseEvent event(type, point, point, base::TimeTicks::Now(),
                       type == ui::EventType::kMouseReleased ? ui::EF_NONE
                                                             : ui::EF_LEFT_MOUSE_BUTTON,
                       ui::EF_LEFT_MOUSE_BUTTON);
  views::View* root = widget->GetRootView();
  switch (type) {
    case ui::EventType::kMousePressed:
      root->OnMousePressed(event);
      break;
    case ui::EventType::kMouseDragged:
      root->OnMouseDragged(event);
      break;
    default:
      root->OnMouseReleased(event);
      break;
  }
}

// Where a "drag:" left the pointer, for "drop".
gfx::Point& DragPoint() {
  static gfx::Point point;
  return point;
}

// "drag:<n>:<x>,<y>": presses the n-th view of `class_name` (the n-th tab of the
// sidebar for "TabView"; from the end if negative) and drags it to (x, y),
// fractions of the page's area, without letting go.
void Drag(views::Widget* widget,
          std::string_view spec,
          std::string_view class_name = "TabView") {
  const std::vector<std::string_view> parts = base::SplitStringPiece(
      spec, ":,", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  int n = 0;
  double fx = 0;
  double fy = 0;
  std::vector<views::View*> tabs;
  FindViews(widget->GetRootView(), class_name, tabs);
  const views::View* page = FindView(widget->GetRootView(), "MultiContentsView");
  if (parts.size() != 3 || !base::StringToInt(parts[0], &n) ||
      !base::StringToDouble(parts[1], &fx) || !base::StringToDouble(parts[2], &fy) ||
      !page) {
    return;
  }
  if (n < 0) {
    n += tabs.size();
  }
  if (n < 0 || n >= static_cast<int>(tabs.size())) {
    return;
  }
  const gfx::Point from =
      tabs[n]->ConvertRectToWidget(tabs[n]->GetLocalBounds()).CenterPoint();
  const gfx::Rect area = page->ConvertRectToWidget(page->GetLocalBounds());
  const gfx::Point to(area.x() + area.width() * fx, area.y() + area.height() * fy);
  SendMouse(widget, ui::EventType::kMousePressed, from);
  constexpr int kSteps = 12;
  for (int i = 1; i <= kSteps; ++i) {
    SendMouse(widget, ui::EventType::kMouseDragged,
              gfx::Point(from.x() + (to.x() - from.x()) * i / kSteps,
                         from.y() + (to.y() - from.y()) * i / kSteps));
  }
  DragPoint() = to;
}

// Sends the window a two-finger scroll over its sidebar (which, unlike the tabs
// it moves, stays put), `dx` in the gesture's `momentum` phase: as macOS's come,
// MAY_BEGIN as the fingers touch, NONE as they move, END as they lift.
void SendScroll(views::Widget* widget, ui::EventMomentumPhase momentum, float dx) {
  const views::View* sidebar =
      FindView(widget->GetRootView(), "VerticalTabStripRegionView");
  const gfx::PointF point(
      sidebar ? sidebar->ConvertRectToWidget(sidebar->GetLocalBounds()).CenterPoint()
              : gfx::Point(100, 200));
  ui::ScrollEvent event(ui::EventType::kScroll, point, point,
                        base::TimeTicks::Now(), ui::EF_NONE, dx, 0, dx, 0, 2,
                        momentum);
  widget->OnScrollEvent(&event);
}

// "swipe:<dx>[:hold]": a horizontal swipe on the tab list, let go unless held
// ("swipe-end" lets go).
void Swipe(views::Widget* widget, std::string_view spec) {
  const std::vector<std::string_view> parts = base::SplitStringPiece(
      spec, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  double dx = 0;
  if (parts.empty() || !base::StringToDouble(parts[0], &dx)) {
    return;
  }
  SendScroll(widget, ui::EventMomentumPhase::MAY_BEGIN, 0);
  constexpr int kSteps = 10;
  for (int i = 0; i < kSteps; ++i) {
    SendScroll(widget, ui::EventMomentumPhase::NONE, dx / kSteps);
  }
  if (parts.size() < 2 || parts[1] != "hold") {
    SendScroll(widget, ui::EventMomentumPhase::END, 0);
  }
}

views::Widget* BrowserWidget();

// "swipe-timed:<dx>:<ms>": the same, its moves spread over <ms> as fingers' are
// (an event every 8 ms), then let go.
void SwipeTimed(std::string_view spec) {
  const std::vector<std::string_view> parts = base::SplitStringPiece(
      spec, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  double dx = 0;
  int ms = 0;
  if (parts.size() < 2 || !base::StringToDouble(parts[0], &dx) ||
      !base::StringToInt(parts[1], &ms) || ms <= 0) {
    return;
  }
  constexpr base::TimeDelta kEvery = base::Milliseconds(8);
  const int steps = std::max(1, ms / 8);
  auto send = [](ui::EventMomentumPhase momentum, float step) {
    if (views::Widget* widget = BrowserWidget()) {
      SendScroll(widget, momentum, step);
    }
  };
  send(ui::EventMomentumPhase::MAY_BEGIN, 0);
  for (int i = 1; i <= steps; ++i) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(send, ui::EventMomentumPhase::NONE,
                       static_cast<float>(dx / steps)),
        kEvery * i);
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(send, ui::EventMomentumPhase::END, 0.f),
      kEvery * steps + base::Milliseconds(4));
}

// Does what a request asks before the snapshot: browser commands (their ids), clicks
// on views ("click:<view class name>[:<n>]"), "split" (the active tab and the one before
// it side by side), "drag:<n>:<x>,<y>" (see Drag()), "drop" and "grip:<n>:<x>,<y>"
// (drags a split pane's grip there), "press:<x>,<y>" (a click there), "select-to:<n>"
// (the tabs from the active one to the <n>th selected), "open:<url>" (in the active
// tab), "force-click" (a firm press on the trackpad), "expand" (the compact sidebar
// expanded, as hovered, until the next "expand"), "type:<text>"
// (into the address bar), "escape",
// "wait:<ms>" (before the actions after it) and
// "ws-new", "ws-next", "ws-prev", "ws-icon:<id>", "ws-color:<n>" (a TabGroupColorId),
// "ws-tint:<0|1>",
// "ws-picker", "ws-container:<id>" (workspaces), "swipe:<dx>[:hold]", "swipe-timed:<dx>:<ms>" and "swipe-end" (see
// Swipe()), and "film:<count>:<ms>" (see Film()).
void RunActions(BrowserWindowInterface* browser,
                views::Widget* widget,
                const std::string& actions) {
  const std::vector<std::string_view> all = base::SplitStringPiece(
      actions, " \n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (size_t i = 0; i < all.size(); ++i) {
    const std::string_view action = all[i];
    int command = 0;
    int wait = 0;
    if (action.starts_with("wait:") &&
        base::StringToInt(action.substr(5), &wait)) {
      // The rest later, in the window active then.
      std::vector<std::string_view> rest(all.begin() + i + 1, all.end());
      base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(
              [](std::string rest) {
                BrowserWindowInterface* browser =
                    GetLastActiveBrowserWindowInterfaceWithAnyProfile();
                views::Widget* widget = BrowserWidget();
                if (browser && widget) {
                  RunActions(browser, widget, rest);
                }
              },
              base::JoinString(rest, " ")),
          base::Milliseconds(wait));
      return;
    } else if (base::StringToInt(action, &command)) {
      chrome::ExecuteCommand(browser, command);
    } else if (action.starts_with("drag:")) {
      Drag(widget, action.substr(5));
    } else if (action.starts_with("grip:")) {
      // The same with a split pane's grip, let go at once.
      Drag(widget, action.substr(5), "SplitHandle");
      SendMouse(widget, ui::EventType::kMouseReleased, DragPoint());
    } else if (action.starts_with("swipe-timed:")) {
      SwipeTimed(action.substr(12));
    } else if (action.starts_with("swipe:")) {
      Swipe(widget, action.substr(6));
    } else if (action == "swipe-end") {
      SendScroll(widget, ui::EventMomentumPhase::END, 0);
    } else if (action == "drop") {
      SendMouse(widget, ui::EventType::kMouseDragged, DragPoint());
      SendMouse(widget, ui::EventType::kMouseReleased, DragPoint());
    } else if (action.starts_with("type:")) {
      // Into the address bar ("+" for spaces), which then suggests.
      if (BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser)) {
        std::string text(action.substr(5));
        base::ReplaceChars(text, "+", " ", &text);
        chrome::ExecuteCommand(browser, IDC_FOCUS_LOCATION);
        view->GetLocationBarView()->GetOmniboxView()->SetUserText(
            base::UTF8ToUTF16(text), /*update_popup=*/true);
      }
    } else if (action.starts_with("press:")) {
      // A click at <x>,<y> in the window, as the window gets it.
      const std::vector<std::string_view> xy = base::SplitStringPiece(
          action.substr(6), ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
      int x = 0;
      int y = 0;
      if (xy.size() == 2 && base::StringToInt(xy[0], &x) &&
          base::StringToInt(xy[1], &y)) {
        // Which view gets it, in the log (OBLADA_LOG=1 shows it).
        const views::View* target =
            widget->GetRootView()->GetEventHandlerForPoint(gfx::Point(x, y));
        // Window part HTCLIENT (1) for the page and the browser's UI, HTCAPTION
        // (2) or HTNOWHERE (0) where a click drags the window instead.
        LOG(ERROR) << "press:" << x << "," << y << " goes to "
                   << (target ? target->GetClassName() : "nothing")
                   << ", window part "
                   << widget->GetNonClientComponent(gfx::Point(x, y));
        SendMouse(widget, ui::EventType::kMousePressed, gfx::Point(x, y));
        // As a real release: the button in its flags too.
        const gfx::Point point(x, y);
        ui::MouseEvent release(ui::EventType::kMouseReleased, point, point,
                               base::TimeTicks::Now(), ui::EF_LEFT_MOUSE_BUTTON,
                               ui::EF_LEFT_MOUSE_BUTTON);
        widget->GetRootView()->OnMouseReleased(release);
      }
    } else if (action.starts_with("select-to:")) {
      // Selects the tabs from the active one to the <n>th, as a shift-click.
      int index = 0;
      if (base::StringToInt(action.substr(10), &index) &&
          browser->GetTabStripModel()->ContainsIndex(index)) {
        browser->GetTabStripModel()->AddSelectionFromAnchorTo(index);
      }
    } else if (action.starts_with("open:")) {
      // Loads <url> in the active tab.
      if (content::WebContents* contents =
              browser->GetTabStripModel()->GetActiveWebContents()) {
        contents->GetController().LoadURL(GURL(action.substr(5)),
                                          content::Referrer(),
                                          ui::PAGE_TRANSITION_TYPED,
                                          std::string());
      }
    } else if (action == "expand") {
      // The compact sidebar expanded, as with the mouse over it, until the next
      // "expand".
      static base::NoDestructor<std::unique_ptr<ExpandOnHoverLock>> lock;
      BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser);
      if (*lock) {
        lock->reset();
      } else if (view && view->vertical_tab_strip_region_view_for_testing()) {
        *lock = view->vertical_tab_strip_region_view_for_testing()
                    ->GetExpandOnHoverLock(ExpandOnHoverLockType::kKeepExpanded);
      }
    } else if (action == "force-click") {
      // A firm press on the trackpad, in the active tab.
      if (content::WebContents* contents =
              browser->GetTabStripModel()->GetActiveWebContents()) {
        peek::PeekAtHoveredLink(contents);
      }
    } else if (action == "escape") {
      // Escape pressed, where the focus is.
      ui::KeyEvent press(ui::EventType::kKeyPressed, ui::VKEY_ESCAPE,
                         ui::EF_NONE);
      widget->OnKeyEvent(&press);
      ui::KeyEvent release(ui::EventType::kKeyReleased, ui::VKEY_ESCAPE,
                           ui::EF_NONE);
      widget->OnKeyEvent(&release);
    } else if (action == "ws-new") {
      workspaces::AddAndSwitch(browser);
    } else if (action == "ws-next" || action == "ws-prev") {
      workspaces::SwitchRelative(browser, action == "ws-next");
    } else if (action.starts_with("ws-icon:")) {
      if (auto workspace = workspaces::CurrentWorkspace(browser)) {
        workspace->icon = std::string(action.substr(8));
        workspaces::UpdateWorkspace(browser->GetProfile(), *workspace);
      }
    } else if (action.starts_with("ws-tint:")) {
      // Whether the window takes the workspace's colors.
      if (auto workspace = workspaces::CurrentWorkspace(browser)) {
        workspace->tints_window = action.substr(8) != "0";
        workspaces::UpdateWorkspace(browser->GetProfile(), *workspace);
      }
    } else if (action.starts_with("ws-color:")) {
      int color = 0;
      if (auto workspace = workspaces::CurrentWorkspace(browser);
          workspace && base::StringToInt(action.substr(9), &color)) {
        workspace->color = color;
        workspaces::UpdateWorkspace(browser->GetProfile(), *workspace);
      }
    } else if (action == "ws-picker") {
      // The icon and color picker of the workspace shown, by its button.
      const std::optional<workspaces::Workspace> workspace =
          workspaces::CurrentWorkspace(browser);
      std::vector<views::View*> buttons;
      FindViews(widget->GetRootView(), "WorkspaceButton", buttons);
      const std::vector<workspaces::Workspace> all =
          workspaces::GetWorkspaces(browser->GetProfile());
      for (size_t i = 0; workspace && i < all.size() && i < buttons.size(); ++i) {
        if (all[i].id == workspace->id) {
          workspaces::ShowWorkspaceIconPicker(browser, workspace->id,
                                              buttons[i]);
        }
      }
    } else if (action.starts_with("ws-container:")) {
      if (auto workspace = workspaces::CurrentWorkspace(browser)) {
        workspace->container = std::string(action.substr(13));
        workspaces::UpdateWorkspace(browser->GetProfile(), *workspace);
      }
    } else if (action == "split") {
      TabStripModel* tabs = browser->GetTabStripModel();
      if (tabs->active_index() > 0) {
        tabs->AddToNewSplit({tabs->active_index() - 1},
                            split_tabs::SplitTabVisualData(
                                split_tabs::SplitTabLayout::kSideBySide),
                            split_tabs::SplitTabCreatedSource::kTabContextMenu);
      }
    } else if (action.starts_with("click:")) {
      // "click:<view class>[:<n>]": its n-th view (the first by default), in the
      // window or in a bubble open on it.
      const std::vector<std::string_view> parts = base::SplitStringPiece(
          action.substr(6), ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
      int n = 0;
      if (parts.empty() ||
          (parts.size() > 1 && !base::StringToInt(parts[1], &n))) {
        continue;
      }
      std::vector<views::View*> candidates;
      FindViews(widget->GetRootView(), parts[0], candidates);
      for (views::Widget* owned :
           views::Widget::GetAllOwnedWidgets(widget->GetNativeView())) {
        if (owned != widget && owned->IsVisible() && owned->GetRootView()) {
          FindViews(owned->GetRootView(), parts[0], candidates);
        }
      }
      views::View* view = n >= 0 && n < static_cast<int>(candidates.size())
                              ? candidates[n]
                              : nullptr;
      if (view) {
        const gfx::Point center = view->GetLocalBounds().CenterPoint();
        const base::TimeTicks now = base::TimeTicks::Now();
        view->OnMousePressed(ui::MouseEvent(ui::EventType::kMousePressed, center,
                                            center, now, ui::EF_LEFT_MOUSE_BUTTON,
                                            ui::EF_LEFT_MOUSE_BUTTON));
        view->OnMouseReleased(ui::MouseEvent(ui::EventType::kMouseReleased, center,
                                             center, now, ui::EF_LEFT_MOUSE_BUTTON,
                                             ui::EF_LEFT_MOUSE_BUTTON));
      }
    }
  }
}

void DescribeViews(const views::View* view, int depth, std::string& out) {
  const gfx::Rect bounds = view->ConvertRectToWidget(view->GetLocalBounds());
  std::string layer;
  if (const ui::Layer* own = view->layer()) {
    // Where it is drawn: its layer's state, and the layer it is drawn in.
    layer = base::StrCat(
        {" [layer ", own->bounds().ToString(), own->IsVisible() ? "" : " invisible",
         own->GetTargetOpacity() < 1 ? " translucent" : "",
         own->GetMasksToBounds() ? " clips" : "", " in ",
         own->parent() ? own->parent()->name() : "nothing", "]"});
  }
  base::StrAppend(&out, {std::string(depth * 2, ' '), view->GetClassName(), " ",
                         bounds.ToString(),
                         view->GetVisible() ? "" : " hidden", layer, "\n"});
  if (!view->GetVisible() || depth >= 12) {
    return;
  }
  for (const views::View* child : view->children()) {
    DescribeViews(child, depth + 1, out);
  }
}

void OnCopied(base::FilePath dir,
              std::string name,
              float dip_width,
              std::vector<gfx::Rect> window_controls,
              std::string layout,
              std::unique_ptr<viz::CopyOutputResult> result) {
  if (result->IsEmpty()) {
    return;
  }
  auto scoped = result->ScopedAccessSkBitmap();
  const SkBitmap& copied = scoped.GetOutScopedBitmap();
  SkBitmap bitmap;
  if (!bitmap.tryAllocPixels(copied.info()) || !copied.readPixels(bitmap.pixmap())) {
    return;
  }
  // Window controls drawn by the system, where they are.
  if (!window_controls.empty() && dip_width > 0) {
    const float scale = bitmap.width() / dip_width;
    SkCanvas canvas(bitmap);
    const SkColor colors[] = {SkColorSetRGB(0xff, 0x5f, 0x57),
                              SkColorSetRGB(0xfe, 0xbc, 0x2e),
                              SkColorSetRGB(0x28, 0xc8, 0x40)};
    SkPaint paint;
    paint.setAntiAlias(true);
    for (size_t i = 0; i < window_controls.size(); ++i) {
      const gfx::Rect& rect = window_controls[i];
      paint.setColor(colors[i % 3]);
      canvas.drawCircle(rect.CenterPoint().x() * scale, rect.CenterPoint().y() * scale,
                        std::min(rect.width(), rect.height()) * scale / 2, paint);
      base::StrAppend(&layout, {"window control ", rect.ToString(), "\n"});
    }
  }
  std::optional<std::vector<uint8_t>> png =
      gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, /*discard_transparency=*/false);
  if (png) {
    base::ThreadPool::PostTask(
        FROM_HERE, {base::MayBlock()},
        base::BindOnce(&WriteFiles, dir, std::move(name), std::move(layout),
                       std::move(*png)));
  }
}

views::Widget* BrowserWidget() {
  BrowserWindowInterface* browser = GetLastActiveBrowserWindowInterfaceWithAnyProfile();
  BrowserView* view = browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  return view ? view->GetWidget() : nullptr;
}

// Snapshots `widget` to <dir>/<name>.png (with, for the browser window, layout.txt).
void CaptureWidget(const base::FilePath& dir,
                   views::Widget* widget,
                   const std::string& name) {
  ui::Compositor* compositor = widget ? widget->GetCompositor() : nullptr;
  if (!compositor || !compositor->root_layer()) {
    return;
  }
  const bool is_window = name == "window";
  std::vector<gfx::Rect> window_controls;
#if BUILDFLAG(IS_MAC)
  if (is_window) {
    window_controls = WindowControlBounds(widget->GetNativeWindow());
  }
#endif
  std::string layout;
  if (is_window) {
    DescribeViews(widget->GetRootView(), 0, layout);
    // The layers, bottom to top, as the compositor draws them.
    std::ostringstream layers;
    ui::PrintLayerHierarchy(compositor->root_layer(), gfx::Point(),
                            /*print_invisible=*/false, &layers);
    base::StrAppend(&layout, {"layers:\n", layers.str()});
    // The page's background, as the browser and the page last set it.
    BrowserView* browser_view =
        BrowserView::GetBrowserViewForNativeWindow(widget->GetNativeWindow());
    content::WebContents* contents =
        browser_view ? browser_view->GetActiveWebContents() : nullptr;
    content::RenderWidgetHostView* page_view =
        contents ? contents->GetRenderWidgetHostView() : nullptr;
    if (page_view) {
      const std::optional<SkColor> color = page_view->GetBackgroundColor();
      base::StrAppend(
          &layout,
          {"page ", contents->GetVisibleURL().spec(), " see-through=",
           new_tab::IsSeeThrough(contents) ? "yes" : "no", " background=",
           color ? base::StringPrintf("#%08X", *color) : "none", "\n"});
    }
    const gfx::Point origin = widget->GetWindowBoundsInScreen().origin();
    for (views::Widget* owned :
         views::Widget::GetAllOwnedWidgets(widget->GetNativeView())) {
      if (owned != widget && owned->IsVisible()) {
        base::StrAppend(&layout, {"bubble ",
                                  (owned->GetWindowBoundsInScreen() -
                                   origin.OffsetFromOrigin())
                                      .ToString(),
                                  "\n"});
      }
    }
  }
  auto request = std::make_unique<viz::CopyOutputRequest>(
      viz::CopyOutputRequest::ResultFormat::RGBA,
      viz::CopyOutputRequest::ResultDestination::kSystemMemory,
      base::BindOnce(&OnCopied, dir, name,
                     static_cast<float>(widget->GetRootView()->width()),
                     std::move(window_controls), std::move(layout)));
  request->set_result_task_runner(base::SequencedTaskRunner::GetCurrentDefault());
  compositor->root_layer()->RequestCopyOfOutput(std::move(request));
}

// The browser window to window.png, and a bubble open on it (if any) to popup.png
// first.
void Capture(const base::FilePath& dir) {
  views::Widget* widget = BrowserWidget();
  if (!widget) {
    return;
  }
  for (views::Widget* owned : views::Widget::GetAllOwnedWidgets(widget->GetNativeView())) {
    if (owned != widget && owned->IsVisible() && owned->GetCompositor()) {
      CaptureWidget(dir, owned, "popup");
      break;
    }
  }
  CaptureWidget(dir, widget, "window");
}

// "film:<count>:<ms>": frames of what the other actions do, <ms> apart, to
// <dir>/film-<i>.png. Returns how long it takes.
base::TimeDelta Film(const base::FilePath& dir, const std::string& actions) {
  base::TimeDelta length;
  // Frames start after the waits before them ("wait:<ms>").
  base::TimeDelta start;
  for (std::string_view action : base::SplitStringPiece(
           actions, " \n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    int wait = 0;
    if (action.starts_with("wait:") &&
        base::StringToInt(action.substr(5), &wait)) {
      start += base::Milliseconds(wait);
      length = std::max(length, start);
      continue;
    }
    if (!action.starts_with("film:")) {
      continue;
    }
    const std::vector<std::string_view> parts = base::SplitStringPiece(
        action.substr(5), ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    int count = 0;
    int ms = 0;
    if (parts.size() != 2 || !base::StringToInt(parts[0], &count) ||
        !base::StringToInt(parts[1], &ms) || count <= 0 || ms <= 0) {
      continue;
    }
    for (int i = 0; i < count; ++i) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(
              [](base::FilePath dir, int i) {
                CaptureWidget(dir, BrowserWidget(),
                              base::StrCat({"film-", base::NumberToString(i)}));
              },
              dir, i),
          start + base::Milliseconds(ms) * i);
    }
    length = std::max(length, start + base::Milliseconds(ms) * count);
  }
  return length;
}

void Poll(const base::FilePath& dir) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()}, base::BindOnce(&TakeRequest, dir),
      base::BindOnce(
          [](base::FilePath dir, std::optional<std::string> actions) {
            if (!actions) {
              return;
            }
            BrowserWindowInterface* browser =
                GetLastActiveBrowserWindowInterfaceWithAnyProfile();
            views::Widget* widget = BrowserWidget();
            if (actions->find_first_not_of(" \n") == std::string::npos ||
                !browser || !widget) {
              Capture(dir);
              return;
            }
            RunActions(browser, widget, *actions);
            // Once what they do (e.g. animations) is over.
            base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
                FROM_HERE, base::BindOnce(&Capture, dir),
                std::max(base::Milliseconds(1200),
                         Film(dir, *actions) + base::Milliseconds(600)));
          },
          dir));
}

}  // namespace

void MaybeStartWindowSnapshots() {
  const base::CommandLine& command_line = *base::CommandLine::ForCurrentProcess();
  if (!command_line.HasSwitch(kSwitch)) {
    return;
  }
  static base::NoDestructor<base::RepeatingTimer> timer;
  timer->Start(FROM_HERE, base::Milliseconds(300),
               base::BindRepeating(&Poll, command_line.GetSwitchValuePath(kSwitch)));
}

}  // namespace oblada::debug
