#include "oblada/browser/ui/split_handle.h"

#include <optional>
#include <utility>

#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_interface.h"
#include "oblada/browser/l10n.h"
#include "oblada/browser/ui/drag_ghost.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/cursor/cursor.h"
#include "ui/base/cursor/mojom/cursor_type.mojom-shared.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"

namespace oblada::split {

namespace {

constexpr int kGripWidth = 32;
constexpr int kGripHeight = 4;

}  // namespace

SplitHandle::SplitHandle(BrowserWindowInterface* browser,
                         views::View* panes,
                         PaneGetter pane)
    : browser_(browser), panes_(panes), pane_(std::move(pane)) {
  SetTooltipText(L10n("split.handle"));
  GetViewAccessibility().SetRole(ax::mojom::Role::kButton);
  GetViewAccessibility().SetName(L10n("split.handle"));
}

SplitHandle::~SplitHandle() = default;

void SplitHandle::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);
  if (!GetColorProvider()) {
    return;
  }
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(SkColorSetA(
      GetColorProvider()->GetColor(kColorTabForegroundInactiveFrameActive),
      hovered_ || dragging_ ? 0xB0 : 0x50));
  const gfx::Rect grip((width() - kGripWidth) / 2, (height() - kGripHeight) / 2,
                       kGripWidth, kGripHeight);
  canvas->DrawRoundRect(grip, kGripHeight / 2, flags);
}

ui::Cursor SplitHandle::GetCursor(const ui::MouseEvent& event) {
  return ui::Cursor(dragging_ ? ui::mojom::CursorType::kGrabbing
                              : ui::mojom::CursorType::kGrab);
}

bool SplitHandle::OnMousePressed(const ui::MouseEvent& event) {
  if (!event.IsOnlyLeftMouseButton()) {
    return false;
  }
  press_point_ = event.location();
  dragging_ = false;
  return true;
}

bool SplitHandle::OnMouseDragged(const ui::MouseEvent& event) {
  if (!dragging_ && ExceededDragThreshold(event.location() - press_point_)) {
    dragging_ = true;
    SchedulePaint();
  }
  if (dragging_ && GetWidget()) {
    if (!ghost_) {
      ghost_ = std::make_unique<DragGhost>(GetWidget()->GetTopLevelWidget());
    }
    gfx::Point point = event.location();
    views::View::ConvertPointToScreen(this, &point);
    ghost_->Show(pane_.Run()->web_contents(), point, gfx::PointF(0.5f, 0.5f));
  }
  return true;
}

void SplitHandle::OnMouseReleased(const ui::MouseEvent& event) {
  if (!dragging_) {
    if (HitTestPoint(event.location())) {
      ShowMenu();
    }
    return;
  }
  dragging_ = false;
  ghost_.reset();
  SchedulePaint();
  gfx::Point point = event.location();
  views::View::ConvertPointToScreen(this, &point);
  if (!panes_->GetBoundsInScreen().Contains(point)) {
    ExecuteCommand(kSeparate, 0);
  } else if (!pane_.Run()->GetBoundsInScreen().Contains(point)) {
    ExecuteCommand(kSwap, 0);
  }
}

void SplitHandle::OnMouseCaptureLost() {
  dragging_ = false;
  ghost_.reset();
  SchedulePaint();
}

void SplitHandle::OnMouseEntered(const ui::MouseEvent& event) {
  hovered_ = true;
  SchedulePaint();
}

void SplitHandle::OnMouseExited(const ui::MouseEvent& event) {
  hovered_ = false;
  SchedulePaint();
}

void SplitHandle::ExecuteCommand(int command_id, int event_flags) {
  tabs::TabInterface* const tab =
      tabs::TabInterface::MaybeGetFromContents(pane_.Run()->web_contents());
  TabStripModel* const model = browser_->GetTabStripModel();
  if (!tab || !model) {
    return;
  }
  const std::optional<split_tabs::SplitTabId> split = tab->GetSplit();
  switch (command_id) {
    case kSwap:
      if (split) {
        model->ReverseTabsInSplit(*split);
      }
      break;
    case kSeparate:
      if (split) {
        model->RemoveSplit(*split);
        // The pane taken out of the split is the page then shown.
        if (const int index = model->GetIndexOfTab(tab);
            index != TabStripModel::kNoTab) {
          model->ActivateTabAt(index);
        }
      }
      break;
    case kClose:
      if (const int index = model->GetIndexOfTab(tab);
          index != TabStripModel::kNoTab) {
        model->CloseWebContentsAt(index, TabCloseTypes::CLOSE_USER_GESTURE);
      }
      break;
  }
}

void SplitHandle::ShowMenu() {
  menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  menu_model_->AddItem(kSwap, L10n("split.swap"));
  menu_model_->AddItem(kSeparate, L10n("split.separate"));
  menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  menu_model_->AddItem(kClose, L10n("split.close"));
  menu_runner_ = std::make_unique<views::MenuRunner>(
      menu_model_.get(), views::MenuRunner::HAS_MNEMONICS);
  menu_runner_->RunMenuAt(GetWidget(), nullptr, GetBoundsInScreen(),
                          views::MenuAnchorPosition::kTopLeft,
                          ui::mojom::MenuSourceType::kMouse);
}

BEGIN_METADATA(SplitHandle)
END_METADATA

}  // namespace oblada::split
