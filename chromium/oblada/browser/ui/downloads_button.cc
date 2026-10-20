#include "oblada/browser/ui/downloads_button.h"

#include <utility>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/actions/chrome_action_id.h"
#include "chrome/browser/ui/browser_actions.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/toolbar_button_provider.h"
#include "chrome/browser/ui/views/toolbar/pinned_toolbar_actions.h"
#include "chrome/browser/ui/views/toolbar/toolbar_button.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "ui/actions/actions.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/base/models/image_model.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/text_constants.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view_utils.h"

namespace oblada::downloads {

namespace {

// The button's view id (one Chromium's view_ids.h leaves unused).
constexpr int kButtonViewId = 55650;

// As big as the other icons at the bottom of the sidebar.
constexpr int kIconSize = 16;

// Follows the action as the toolbar's button does: its icon, tooltip, whether it's
// enabled or engaged (a download finished and not looked at yet).
class DownloadsButton : public ToolbarButton {
  METADATA_HEADER(DownloadsButton, ToolbarButton)

 public:
  explicit DownloadsButton(BrowserWindowInterface* browser)
      : ToolbarButton(base::BindRepeating(&DownloadsButton::Invoke,
                                          base::Unretained(this))),
        browser_(browser) {
    SetID(kButtonViewId);
    // The icon in the middle of however big the sidebar makes it.
    SetLayoutInsets(gfx::Insets(2));
    SetHorizontalAlignment(gfx::ALIGN_CENTER);
    Observe();
  }

  // ToolbarButton:
  void AddedToWidget() override {
    ToolbarButton::AddedToWidget();
    Observe();
    // Not the toolbar's too, if it showed before this one was there.
    BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
    ToolbarButtonProvider* provider =
        browser_view ? browser_view->toolbar_button_provider() : nullptr;
    if (PinnedToolbarActions* pinned =
            provider ? provider->GetPinnedToolbarActions() : nullptr) {
      pinned->ShowActionEphemerallyInToolbar(kActionShowDownloads, false);
    }
  }

  void UpdateIcon() override {
    if (!icon_ || !GetColorProvider()) {
      return;
    }
    if (engaged_) {
      const SkColor color =
          GetColorProvider()->GetColor(kColorToolbarActionItemEngaged);
      UpdateIconsWithColors(*icon_, color, color, color,
                            GetForegroundColor(ButtonState::STATE_DISABLED));
    } else {
      UpdateIconsWithStandardColors(*icon_);
    }
  }

  // A click while the bubble shows only closes it (as it loses the focus), rather
  // than open the downloads page.
  bool OnMousePressed(const ui::MouseEvent& event) override {
    skip_click_ = action_item_ && action_item_->GetIsShowingBubble();
    return ToolbarButton::OnMousePressed(event);
  }

  void OnMouseReleased(const ui::MouseEvent& event) override {
    if (skip_click_) {
      OnClickCanceled(event);
    } else {
      ToolbarButton::OnMouseReleased(event);
    }
    skip_click_ = false;
  }

 protected:
  int GetIconSize() const override { return kIconSize; }

 private:
  // Once the window's actions exist.
  void Observe() {
    if (action_item_) {
      return;
    }
    BrowserActions* actions = BrowserActions::From(browser_);
    actions::ActionItem* item =
        actions && actions->root_action_item()
            ? actions::ActionManager::Get().FindAction(
                  kActionShowDownloads, actions->root_action_item())
            : nullptr;
    if (!item) {
      return;
    }
    action_item_ = item->GetAsWeakPtr();
    action_changed_ = item->AddActionChangedCallback(base::BindRepeating(
        &DownloadsButton::OnActionChanged, base::Unretained(this)));
    OnActionChanged();
  }

  void OnActionChanged() {
    actions::ActionItem* item = action_item_.get();
    if (!item) {
      return;
    }
    SetTooltipText(std::u16string(item->GetTooltipText()));
    GetViewAccessibility().SetName(std::u16string(item->GetText()));
    SetEnabled(item->GetEnabled());
    engaged_ = item->GetProperty(kActionItemUnderlineIndicatorKey);
    if (item->GetImage().IsVectorIcon()) {
      icon_ = item->GetImage().GetVectorIcon().vector_icon();
    }
    UpdateIcon();
  }

  void Invoke() {
    if (action_item_) {
      action_item_->InvokeAction(
          actions::ActionInvocationContext::Builder().Build());
    }
  }

  raw_ptr<BrowserWindowInterface> browser_;
  base::WeakPtr<actions::ActionItem> action_item_;
  base::CallbackListSubscription action_changed_;
  raw_ptr<const gfx::VectorIcon> icon_ = nullptr;
  bool engaged_ = false;
  bool skip_click_ = false;
};

BEGIN_METADATA(DownloadsButton)
END_METADATA

}  // namespace

std::unique_ptr<views::View> CreateButton(BrowserWindowInterface* browser) {
  return std::make_unique<DownloadsButton>(browser);
}

ToolbarButton* SidebarButton(BrowserView* browser_view) {
  return browser_view ? views::AsViewClass<DownloadsButton>(
                            browser_view->GetViewByID(kButtonViewId))
                      : nullptr;
}

views::BubbleBorder::Arrow BubbleArrow(const views::View* anchor,
                                       views::BubbleBorder::Arrow arrow) {
  return views::IsViewClass<DownloadsButton>(anchor)
             ? views::BubbleBorder::BOTTOM_LEFT
             : arrow;
}

}  // namespace oblada::downloads
