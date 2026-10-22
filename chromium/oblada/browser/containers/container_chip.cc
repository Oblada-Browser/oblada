#include "oblada/browser/containers/container_chip.h"

#include <memory>
#include <string>
#include <utility>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "content/public/browser/web_contents.h"
#include "oblada/browser/containers/containers.h"
#include "oblada/browser/containers/containers_menu_model.h"
#include "oblada/browser/containers/containers_ui.h"
#include "oblada/browser/l10n.h"
#include "oblada/browser/ui/icons.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view.h"

namespace oblada::containers {

namespace {

// The chip's view id in the location bar (one Chromium's view_ids.h leaves unused).
constexpr int kChipViewId = 55620;

constexpr int kAlwaysOpenId = 55621;
constexpr int kManageId = 55622;

constexpr int kIconSize = 14;
// On either side of the icon: the chip is a circle in the address bar.
constexpr int kHorizontalPadding = 6;

class ChipMenuModel : public ui::SimpleMenuModel,
                      public ui::SimpleMenuModel::Delegate {
 public:
  ChipMenuModel(BrowserWindowInterface* browser,
                content::WebContents* contents,
                std::string container_id)
      : ui::SimpleMenuModel(this),
        browser_(browser),
        container_id_(std::move(container_id)),
        site_(SiteOf(contents->GetLastCommittedURL())) {
    if (!site_.empty()) {
      std::u16string label = L10n("containers.always_open");
      base::ReplaceFirstSubstringAfterOffset(&label, 0, u"$1",
                                             base::UTF8ToUTF16(site_));
      AddCheckItem(kAlwaysOpenId, label);
    }
    reopen_ = ContainersMenuModel::ForReopen(browser, contents);
    AddSubMenu(kReopenInContainerMenuId, L10n("containers.reopen"), reopen_.get());
    AddSeparator(ui::NORMAL_SEPARATOR);
    AddItem(kManageId, L10n("containers.manage"));
  }

  bool IsCommandIdChecked(int command_id) const override {
    return command_id == kAlwaysOpenId &&
           AssignedContainer(browser_->GetProfile(),
                             GURL("https://" + site_)) == container_id_;
  }

  void ExecuteCommand(int command_id, int event_flags) override {
    if (command_id == kAlwaysOpenId) {
      AssignSite(browser_->GetProfile(), site_,
                 IsCommandIdChecked(kAlwaysOpenId) ? std::string() : container_id_);
    } else if (command_id == kManageId) {
      NavigateParams params(browser_, GURL(kContainersURL),
                            ui::PAGE_TRANSITION_AUTO_BOOKMARK);
      params.disposition = WindowOpenDisposition::SINGLETON_TAB;
      Navigate(&params);
    }
  }

 private:
  raw_ptr<BrowserWindowInterface> browser_;
  const std::string container_id_;
  const std::string site_;
  std::unique_ptr<ContainersMenuModel> reopen_;
};

class ContainerChip : public views::LabelButton {
  METADATA_HEADER(ContainerChip, views::LabelButton)

 public:
  explicit ContainerChip(BrowserWindowInterface* browser)
      : views::LabelButton(base::BindRepeating(&ContainerChip::ShowMenu,
                                               base::Unretained(this))),
        browser_(browser) {
    SetID(kChipViewId);
    SetVisible(false);
    SetFocusBehavior(FocusBehavior::ACCESSIBLE_ONLY);
    // Just the icon: the address needs the room. Its name is in the tooltip.
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, kHorizontalPadding)));
    SetHorizontalAlignment(gfx::ALIGN_CENTER);
    label()->SetFontList(
        gfx::FontList().DeriveWithWeight(gfx::Font::Weight::SEMIBOLD));
    containers_changed_ = ObserveContainers(
        browser->GetProfile(),
        base::BindRepeating(&ContainerChip::Refresh, base::Unretained(this)));
  }

  void SetContents(content::WebContents* contents) {
    contents_ = contents ? contents->GetWeakPtr() : nullptr;
    Refresh();
  }

  // views::LabelButton:
  void OnPaintBackground(gfx::Canvas* canvas) override {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(SkColorSetA(color_, IsMouseHovered() ? 0x55 : 0x33));
    // A circle, or a pill if the bar is low.
    gfx::RectF bounds(GetLocalBounds());
    bounds.Inset(gfx::InsetsF::VH(3, 0));
    if (bounds.height() < bounds.width()) {
      bounds.Inset(gfx::InsetsF::VH(0, (bounds.width() - bounds.height()) / 2));
    }
    canvas->DrawRoundRect(bounds, bounds.height() / 2, flags);
    // A container without an icon has a dot.
    if (GetImage(views::Button::STATE_NORMAL).isNull() && GetText().empty()) {
      flags.setColor(text_color_);
      canvas->DrawCircle(bounds.CenterPoint(), 4, flags);
    }
  }

  void OnThemeChanged() override {
    views::LabelButton::OnThemeChanged();
    UpdateTextColor();
  }

 private:
  void Refresh() {
    const std::string id = GetContainerId(contents_.get());
    std::optional<Container> container =
        id.empty() ? std::nullopt : FindContainer(browser_->GetProfile(), id);
    container_id_ = container ? container->id : std::string();
    if (container) {
      color_ = ColorFor(container->color);
      icon_ = container->icon;
      temporary_number_ = TemporaryNumber(*container);
      SetTooltipText(base::UTF8ToUTF16(container->name));
      GetViewAccessibility().SetName(base::UTF8ToUTF16(container->name));
      UpdateTextColor();
    }
    if (GetVisible() != container.has_value()) {
      SetVisible(container.has_value());
    }
    PreferredSizeChanged();
    SchedulePaint();
  }

  void UpdateTextColor() {
    if (!GetColorProvider()) {
      return;
    }
    const SkColor background =
        GetColorProvider()->GetColor(kColorLocationBarBackground);
    text_color_ = color_utils::BlendForMinContrast(color_, background).color;
    // A temporary container has its number, which tells it from the others,
    // rather than their common icon.
    if (temporary_number_) {
      SetImageModel(views::Button::STATE_NORMAL, ui::ImageModel());
      SetText(base::NumberToString16(temporary_number_));
      SetEnabledTextColors(text_color_);
    } else {
      SetText(std::u16string());
      SetImageModel(views::Button::STATE_NORMAL,
                    icons::Image(icon_, text_color_, kIconSize));
    }
    SetMinSize(gfx::Size(kIconSize + 2 * kHorizontalPadding, 0));
  }

  void ShowMenu() {
    if (!contents_ || container_id_.empty()) {
      return;
    }
    menu_model_ =
        std::make_unique<ChipMenuModel>(browser_, contents_.get(), container_id_);
    menu_runner_ = std::make_unique<views::MenuRunner>(
        menu_model_.get(), views::MenuRunner::HAS_MNEMONICS);
    // Towards the page: the chip is in the sidebar, near the screen's edge.
    menu_runner_->RunMenuAt(GetWidget(), nullptr, GetAnchorBoundsInScreen(),
                            views::MenuAnchorPosition::kTopLeft,
                            ui::mojom::MenuSourceType::kMouse);
  }

  raw_ptr<BrowserWindowInterface> browser_;
  base::WeakPtr<content::WebContents> contents_;
  std::string container_id_;
  SkColor color_ = SK_ColorGRAY;
  // The icon's, readable on the address bar.
  SkColor text_color_ = SK_ColorGRAY;
  std::string icon_;
  // For a temporary container, else 0.
  int temporary_number_ = 0;
  base::CallbackListSubscription containers_changed_;
  std::unique_ptr<ChipMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
};

BEGIN_METADATA(ContainerChip)
END_METADATA

}  // namespace

void AddContainerChip(views::View* location_bar, BrowserWindowInterface* browser) {
  if (browser) {
    location_bar->AddChildView(std::make_unique<ContainerChip>(browser));
  }
}

views::View* ContainerChipOf(views::View* location_bar) {
  return location_bar->GetViewByID(kChipViewId);
}

void UpdateContainerChip(views::View* location_bar, content::WebContents* contents) {
  if (views::View* chip = ContainerChipOf(location_bar)) {
    static_cast<ContainerChip*>(chip)->SetContents(contents);
  }
}

}  // namespace oblada::containers
