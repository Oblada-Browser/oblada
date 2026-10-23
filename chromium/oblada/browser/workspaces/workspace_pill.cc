#include "oblada/browser/workspaces/workspace_pill.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/vector_icons/vector_icons.h"
#include "oblada/browser/l10n.h"
#include "oblada/browser/ui/horizontal_tabs.h"
#include "oblada/browser/workspaces/workspace_switcher.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/text_elider.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/widget/widget.h"

namespace oblada::workspaces {

namespace {

constexpr int kIconSize = 16;
constexpr int kChevronSize = 12;
constexpr int kLeadingPadding = 8;
constexpr int kIconNameGap = 6;
constexpr int kChevronGap = 4;
constexpr int kTrailingPadding = 6;
// Longer names fade out.
constexpr int kMaxNameWidth = 140;
// As round as the tabs' veils next to it.
constexpr float kCornerRadius = 10;
// How far the fingers go across the tabs for a swipe to show another
// workspace.
constexpr float kSwipeDistance = 60;

// The list's commands: the workspaces, in order, then "New workspace".
constexpr int kAddCommand = 1;
constexpr int kFirstWorkspaceCommand = 100;

gfx::FontList NameFont() {
  return gfx::FontList().DeriveWithWeight(gfx::Font::Weight::MEDIUM);
}

}  // namespace

WorkspacePill::WorkspacePill(BrowserWindowInterface* browser)
    : views::Button(base::BindRepeating(&WorkspacePill::ShowList,
                                        base::Unretained(this))),
      browser_(browser) {
  set_context_menu_controller(this);
  GetViewAccessibility().SetRole(ax::mojom::Role::kPopUpButton);
  browser_->GetTabStripModel()->AddObserver(this);
  subscription_ = ObserveWorkspaces(
      browser_->GetProfile(),
      base::BindRepeating(&WorkspacePill::Update, base::Unretained(this)));
  Update();
}

WorkspacePill::~WorkspacePill() {
  if (swipe_source_) {
    swipe_source_->RemovePreTargetHandler(&swipes_);
  }
}

bool WorkspacePill::IsIconOnly() const {
  return horizontal::LayoutOf(this) == sidebar::Mode::kHorizonCompact;
}

gfx::Size WorkspacePill::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  int width = kLeadingPadding + kIconSize + kChevronGap + kChevronSize +
              kTrailingPadding;
  if (shown_ && !IsIconOnly()) {
    width += kIconNameGap +
             std::min(gfx::GetStringWidth(base::UTF8ToUTF16(shown_->name),
                                          NameFont()),
                      kMaxNameWidth);
  }
  // As tall as a tab, its veil where theirs are.
  return gfx::Size(width, GetLayoutConstant(LayoutConstant::kTabHeight));
}

void WorkspacePill::PaintButtonContents(gfx::Canvas* canvas) {
  const ui::ColorProvider* colors = GetColorProvider();
  if (!colors || !shown_) {
    return;
  }
  const SkColor foreground =
      colors->GetColor(kColorTabForegroundActiveFrameActive);
  const int top = GetLayoutConstant(LayoutConstant::kTabStripPadding);
  const int bottom = GetLayoutConstant(LayoutConstant::kTabHeight) -
                     GetLayoutConstant(LayoutConstant::kTabstripToolbarOverlap);
  const gfx::RectF veil(0, top, width(), bottom - top);
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(SkColorSetA(foreground, GetState() == STATE_PRESSED   ? 0x2E
                                         : GetState() == STATE_HOVERED ? 0x22
                                                                       : 0x14));
  canvas->DrawRoundRect(veil, kCornerRadius, flags);

  const std::u16string name = base::UTF8ToUTF16(shown_->name);
  int x = kLeadingPadding;
  const int middle = base::ClampRound(veil.CenterPoint().y());
  if (const ui::ImageModel icon = WorkspaceIcon(browser_, name, kIconSize);
      !icon.IsEmpty()) {
    const gfx::ImageSkia image = icon.Rasterize(colors);
    canvas->DrawImageInt(image, x, middle - image.height() / 2);
  }
  x += kIconSize;
  if (!IsIconOnly()) {
    x += kIconNameGap;
    const gfx::FontList font = NameFont();
    const std::u16string text =
        gfx::ElideText(name, font, kMaxNameWidth, gfx::ELIDE_TAIL);
    const int text_width = gfx::GetStringWidth(text, font);
    canvas->DrawStringRect(
        text, font, foreground,
        gfx::Rect(x, middle - font.GetHeight() / 2, text_width,
                  font.GetHeight()));
    x += text_width;
  }
  x += kChevronGap;
  canvas->DrawImageInt(
      gfx::CreateVectorIcon(vector_icons::kKeyboardArrowDownIcon,
                            kChevronSize, SkColorSetA(foreground, 0xA0)),
      x, middle - kChevronSize / 2);
}

void WorkspacePill::AddedToWidget() {
  views::Button::AddedToWidget();
  // Swipes across the tabs, as across the sidebar.
  if (!swipe_source_ && parent()) {
    swipe_source_ = parent();
    swipe_source_->AddPreTargetHandler(&swipes_);
  }
}

void WorkspacePill::RemovedFromWidget() {
  if (swipe_source_) {
    swipe_source_->RemovePreTargetHandler(&swipes_);
    swipe_source_ = nullptr;
  }
  views::Button::RemovedFromWidget();
}

void WorkspacePill::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (shown_) {
    ShowWorkspaceMenu(browser_, base::UTF8ToUTF16(shown_->name), this);
  }
}

void WorkspacePill::ShowList() {
  list_ = std::make_unique<ui::SimpleMenuModel>(this);
  list_ids_.clear();
  for (const Workspace& workspace : GetWorkspaces(browser_->GetProfile())) {
    const std::u16string name = base::UTF8ToUTF16(workspace.name);
    list_->AddCheckItem(
        kFirstWorkspaceCommand + static_cast<int>(list_ids_.size()), name);
    if (const ui::ImageModel icon = WorkspaceIcon(browser_, name, kIconSize);
        !icon.IsEmpty()) {
      list_->SetIcon(list_->GetItemCount() - 1, icon);
    }
    list_ids_.push_back(workspace.id);
  }
  list_->AddSeparator(ui::NORMAL_SEPARATOR);
  list_->AddItemWithIcon(
      kAddCommand, L10n("workspaces.add"),
      ui::ImageModel::FromVectorIcon(kAddIcon, ui::kColorMenuIcon, kIconSize));
  runner_ = std::make_unique<views::MenuRunner>(
      list_.get(), views::MenuRunner::HAS_MNEMONICS);
  runner_->RunMenuAt(GetWidget(), nullptr, GetBoundsInScreen(),
                     views::MenuAnchorPosition::kTopLeft,
                     ui::mojom::MenuSourceType::kMouse);
}

bool WorkspacePill::IsCommandIdChecked(int command_id) const {
  const size_t index = command_id - kFirstWorkspaceCommand;
  return shown_ && command_id >= kFirstWorkspaceCommand &&
         index < list_ids_.size() && list_ids_[index] == shown_->id;
}

void WorkspacePill::ExecuteCommand(int command_id, int event_flags) {
  if (command_id == kAddCommand) {
    AddAndSwitch(browser_);
    return;
  }
  const size_t index = command_id - kFirstWorkspaceCommand;
  if (command_id >= kFirstWorkspaceCommand && index < list_ids_.size()) {
    SwitchTo(browser_, list_ids_[index]);
  }
}

void WorkspacePill::Swipes::OnScrollEvent(ui::ScrollEvent* event) {
  pill_->OnSwipe(event);
}

void WorkspacePill::OnSwipe(ui::ScrollEvent* event) {
  // On macOS a trackpad gesture's phases come as momentum phases: MAY_BEGIN as
  // the fingers touch, NONE as they move, END as they lift, then
  // INERTIAL_UPDATE and END for the momentum that follows (see
  // workspace_slider.h, whose sidebar swipes these follow).
  const ui::EventMomentumPhase momentum = event->momentum_phase();
  const ui::ScrollEventPhase phase = event->scroll_event_phase();
  if (phase == ui::ScrollEventPhase::kBegan ||
      momentum == ui::EventMomentumPhase::MAY_BEGIN) {
    swipe_ = 0;
  }
  if (!swipe_) {
    return;
  }
  const bool fingers_lifted =
      phase == ui::ScrollEventPhase::kEnd ||
      event->type() == ui::EventType::kScrollFlingStart ||
      event->type() == ui::EventType::kScrollFlingCancel ||
      momentum == ui::EventMomentumPhase::END ||
      momentum == ui::EventMomentumPhase::INERTIAL_UPDATE;
  if (fingers_lifted) {
    const float distance = *swipe_;
    swipe_.reset();
    if (std::abs(distance) >= kSwipeDistance) {
      event->SetHandled();
      event->StopPropagation();
      // Which way the fingers went: towards the next workspace, or the
      // previous one.
      SwitchRelative(browser_, (distance < 0) != base::i18n::IsRTL());
    }
    return;
  }
  if (std::abs(event->x_offset()) > std::abs(event->y_offset())) {
    *swipe_ += event->x_offset();
    event->SetHandled();
    event->StopPropagation();
  }
}

void WorkspacePill::OnTabGroupFocusChanged(
    std::optional<tab_groups::TabGroupId> new_focused_group_id,
    std::optional<tab_groups::TabGroupId> old_focused_group_id) {
  Update();
}

void WorkspacePill::Update() {
  shown_ = CurrentWorkspace(browser_);
  const std::u16string name =
      shown_ ? base::UTF8ToUTF16(shown_->name) : std::u16string();
  SetTooltipText(name);
  GetViewAccessibility().SetName(name);
  PreferredSizeChanged();
  SchedulePaint();
}

BEGIN_METADATA(WorkspacePill)
END_METADATA

}  // namespace oblada::workspaces
