#include "oblada/browser/ui/site_panel.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/extensions/extension_context_menu_model.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/bubble_anchor_util.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/extensions/extensions_container.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/page_info/page_info_dialog.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/toolbar/toolbar_action_view_model.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "chrome/browser/ui/views/extensions/extensions_toolbar_desktop.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_icon_state_helper.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "components/omnibox/browser/location_bar_model.h"
#include "components/security_state/core/security_state.h"
#include "components/vector_icons/vector_icons.h"
#include "oblada/browser/l10n.h"
#include "oblada/browser/ui/sidebar_layout.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/color/color_id.h"
#include "ui/gfx/font.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/background.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/separator.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/vector_icons.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace oblada::site_panel {

namespace {

// The button's view id in the location bar (one Chromium's view_ids.h leaves unused).
constexpr int kButtonViewId = 55630;

constexpr int kColumns = 4;
constexpr gfx::Size kTileSize(56, 44);
constexpr int kTileRadius = 10;
constexpr int kSpacing = 8;
constexpr int kIconSize = 20;
constexpr int kExtensionIconSize = 28;

constexpr char kAddExtensionsUrl[] = "https://addons.mozilla.org/firefox/extensions/";

std::unique_ptr<views::ImageButton> CreateTile(views::Button::PressedCallback callback,
                                               const std::u16string& tooltip) {
  auto tile = std::make_unique<views::ImageButton>(std::move(callback));
  tile->SetPreferredSize(kTileSize);
  tile->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  tile->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  tile->SetBackground(
      views::CreateRoundedRectBackground(ui::kColorSysNeutralContainer, kTileRadius));
  tile->SetTooltipText(tooltip);
  tile->GetViewAccessibility().SetName(tooltip);
  views::InstallRoundRectHighlightPathGenerator(tile.get(), gfx::Insets(), kTileRadius);
  return tile;
}

// An extension in the grid; right-clicking it shows its usual menu.
class ExtensionTile : public views::ImageButton, public views::ContextMenuController {
  METADATA_HEADER(ExtensionTile, views::ImageButton)

 public:
  ExtensionTile(PressedCallback callback, ToolbarActionViewModel* model)
      : views::ImageButton(std::move(callback)), model_(model) {
    set_context_menu_controller(this);
  }

  void ShowContextMenuForViewImpl(views::View* source,
                                  const gfx::Point& point,
                                  ui::mojom::MenuSourceType source_type) override {
    ui::MenuModel* menu = model_->GetContextMenu(
        extensions::ExtensionContextMenuModel::ContextMenuSource::kMenuItem);
    if (!menu) {
      return;
    }
    menu_runner_ = std::make_unique<views::MenuRunner>(
        menu, views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU);
    menu_runner_->RunMenuAt(GetWidget(), nullptr, gfx::Rect(point, gfx::Size()),
                            views::MenuAnchorPosition::kTopLeft, source_type);
  }

 private:
  raw_ptr<ToolbarActionViewModel> model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
};

BEGIN_METADATA(ExtensionTile)
END_METADATA

class SitePanelView : public views::View {
  METADATA_HEADER(SitePanelView, views::View)

 public:
  SitePanelView(LocationBarView* location_bar, BrowserWindowInterface* browser)
      : location_bar_(location_bar), browser_(browser) {
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 12));

    // The page.
    auto* actions = AddChildView(std::make_unique<views::BoxLayoutView>());
    actions->SetBetweenChildSpacing(kSpacing);
    AddCommand(actions, IDC_BOOKMARK_THIS_TAB, kStarIcon, L10n("panel.bookmark"));
    AddCommand(actions, IDC_SHARING_HUB_SCREENSHOT, kPhotoCameraIcon,
               L10n("panel.screenshot"));
    AddCommand(actions, IDC_SHOW_READING_MODE_SIDE_PANEL,
               kMenuBookChromeRefreshOldIcon, L10n("panel.reading_mode"));
    AddCommand(actions, IDC_COPY_URL, kLinkChromeRefreshOldIcon,
               L10n("panel.copy_link"));

    // The extensions.
    auto* title = AddChildView(std::make_unique<views::Label>(L10n("panel.extensions")));
    title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title->SetFontList(title->font_list().DeriveWithWeight(gfx::Font::Weight::SEMIBOLD));
    AddExtensions();

    AddChildView(std::make_unique<views::Separator>());

    // The page's security, and the extensions' page.
    auto* footer = AddChildView(std::make_unique<views::BoxLayoutView>());
    footer->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    // As the address bar tells it: its icon, and its chip's text (the product's
    // name on its own pages, "Not secure" on plain HTTP, an extension's name on
    // its pages).
    const LocationBarModel* model = location_bar_->GetLocationBarModel();
    std::u16string security_text = location_bar::GetSecurityChipText(
        model, location_bar_->GetWebContents(), /*is_editing_or_empty=*/false);
    if (security_text.empty()) {
      security_text = L10n(model->GetSecurityLevel() == security_state::SECURE
                               ? "panel.secure"
                               : "panel.page_info");
    }
    auto security = std::make_unique<views::LabelButton>(
        base::BindRepeating(&SitePanelView::ShowPageInfo, base::Unretained(this)),
        security_text);
    security->SetImageModel(
        views::Button::STATE_NORMAL,
        ui::ImageModel::FromVectorIcon(model->GetVectorIcon(), ui::kColorIcon, 16));
    security->SetImageLabelSpacing(6);
    security->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(8, 12)));
    security->SetBackground(
        views::CreateRoundedRectBackground(ui::kColorSysNeutralContainer, kTileRadius));
    footer->SetFlexForView(footer->AddChildView(std::move(security)), 0);
    auto* spacer = footer->AddChildView(std::make_unique<views::View>());
    footer->SetFlexForView(spacer, 1);
    auto more = CreateTile(base::BindRepeating(&SitePanelView::RunCommand,
                                               base::Unretained(this),
                                               IDC_MANAGE_EXTENSIONS),
                           L10n("panel.manage_extensions"));
    more->SetPreferredSize(gfx::Size(kTileSize.height(), kTileSize.height()));
    more->SetImageModel(views::Button::STATE_NORMAL,
                        ui::ImageModel::FromVectorIcon(views::kMoreHorizIcon,
                                                       ui::kColorIcon, kIconSize));
    footer->AddChildView(std::move(more));
  }

 private:
  content::WebContents* contents() {
    return browser_->GetTabStripModel()->GetActiveWebContents();
  }

  void AddCommand(views::View* row,
                  int command_id,
                  const gfx::VectorIcon& icon,
                  const std::u16string& tooltip) {
    auto tile = CreateTile(base::BindRepeating(&SitePanelView::RunCommand,
                                               base::Unretained(this), command_id),
                           tooltip);
    tile->SetImageModel(views::Button::STATE_NORMAL,
                        ui::ImageModel::FromVectorIcon(icon, ui::kColorIcon, kIconSize));
    tile->SetImageModel(
        views::Button::STATE_DISABLED,
        ui::ImageModel::FromVectorIcon(icon, ui::kColorIconDisabled, kIconSize));
    tile->SetEnabled(chrome::IsCommandEnabled(browser_, command_id));
    row->AddChildView(std::move(tile));
  }

  void AddExtensions() {
    Profile* profile = browser_->GetProfile();
    ExtensionsContainer* container = ExtensionsContainer::From(*browser_);
    std::vector<ToolbarActionViewModel*> models;
    if (container) {
      for (const std::string& id : ToolbarActionsModel::Get(profile)->action_ids()) {
        if (ToolbarActionViewModel* model = container->GetActionForId(id)) {
          models.push_back(model);
        }
      }
    }
    std::ranges::sort(models, {}, &ToolbarActionViewModel::GetActionName);

    views::BoxLayoutView* row = nullptr;
    auto add = [&](std::unique_ptr<views::View> tile) {
      if (!row || row->children().size() == kColumns) {
        row = AddChildView(std::make_unique<views::BoxLayoutView>());
        row->SetBetweenChildSpacing(kSpacing);
      }
      row->AddChildView(std::move(tile));
    };
    for (ToolbarActionViewModel* model : models) {
      auto tile = std::make_unique<ExtensionTile>(
          base::BindRepeating(&SitePanelView::RunExtension, base::Unretained(this),
                              model->GetId()),
          model);
      tile->SetPreferredSize(kTileSize);
      tile->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
      tile->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
      tile->SetBackground(views::CreateRoundedRectBackground(
          ui::kColorSysNeutralContainer, kTileRadius));
      views::InstallRoundRectHighlightPathGenerator(tile.get(), gfx::Insets(),
                                                    kTileRadius);
      tile->SetImageModel(
          views::Button::STATE_NORMAL,
          model->GetIcon(contents(), gfx::Size(kExtensionIconSize, kExtensionIconSize)));
      const std::u16string tooltip = model->GetTooltip(contents());
      tile->SetTooltipText(tooltip);
      tile->GetViewAccessibility().SetName(model->GetActionName());
      add(std::move(tile));
    }
    auto plus = CreateTile(
        base::BindRepeating(&SitePanelView::AddExtensionsPage, base::Unretained(this)),
        L10n("panel.add_extensions"));
    plus->SetImageModel(views::Button::STATE_NORMAL,
                        ui::ImageModel::FromVectorIcon(kAddIcon, ui::kColorIcon,
                                                       kIconSize));
    add(std::move(plus));
  }

  // Closes the panel, then, once it's gone, does `action` (e.g. shows a popup from
  // the button).
  void CloseThen(base::OnceClosure action) {
    GetWidget()->Close();  // Its deletion is posted (see OnPanelClosed()).
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
                                                             std::move(action));
  }

  void RunCommand(int command_id) {
    CloseThen(base::BindOnce(
        [](base::WeakPtr<BrowserWindowInterface> browser, int command_id) {
          if (browser) {
            chrome::ExecuteCommand(browser.get(), command_id);
          }
        },
        browser_->GetWeakPtr(), command_id));
  }

  void RunExtension(const std::string& id) {
    CloseThen(base::BindOnce(
        [](base::WeakPtr<BrowserWindowInterface> browser, std::string id) {
          ExtensionsContainer* container =
              browser ? ExtensionsContainer::From(*browser) : nullptr;
          if (ToolbarActionViewModel* model =
                  container ? container->GetActionForId(id) : nullptr) {
            model->ExecuteUserAction(
                ToolbarActionViewModel::InvocationSource::kMenuEntry);
          }
        },
        browser_->GetWeakPtr(), id));
  }

  void AddExtensionsPage() {
    CloseThen(base::BindOnce(
        [](base::WeakPtr<BrowserWindowInterface> browser) {
          if (browser) {
            NavigateParams params(browser.get(), GURL(kAddExtensionsUrl),
                                  ui::PAGE_TRANSITION_LINK);
            params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
            Navigate(&params);
          }
        },
        browser_->GetWeakPtr()));
  }

  void ShowPageInfo() {
    CloseThen(base::BindOnce(
        [](base::WeakPtr<BrowserWindowInterface> browser) {
          content::WebContents* contents =
              browser ? browser->GetTabStripModel()->GetActiveWebContents() : nullptr;
          if (contents) {
            ShowPageInfoDialog(contents, base::DoNothing(),
                               bubble_anchor_util::Anchor::kLocationBar);
          }
        },
        browser_->GetWeakPtr()));
  }

  raw_ptr<LocationBarView> location_bar_;
  raw_ptr<BrowserWindowInterface> browser_;
};

BEGIN_METADATA(SitePanelView)
END_METADATA

class SitePanelButton : public views::ImageButton {
  METADATA_HEADER(SitePanelButton, views::ImageButton)

 public:
  SitePanelButton(LocationBarView* location_bar, BrowserWindowInterface* browser)
      : views::ImageButton(base::BindRepeating(&SitePanelButton::ShowPanel,
                                               base::Unretained(this))),
        location_bar_(location_bar),
        browser_(browser) {
    SetID(kButtonViewId);
    SetTooltipText(L10n("panel.title"));
    GetViewAccessibility().SetName(L10n("panel.title"));
    SetImageHorizontalAlignment(ALIGN_CENTER);
    SetImageVerticalAlignment(ALIGN_MIDDLE);
    SetPreferredSize(gfx::Size(28, 28));
    SetImageModel(STATE_NORMAL,
                  ui::ImageModel::FromVectorIcon(vector_icons::kTuneIcon,
                                                 kColorOmniboxResultsIcon, 16));
    views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::ON);
    views::InstallCircleHighlightPathGenerator(this);
  }

 private:
  void ShowPanel() {
    if (panel_) {
      panel_->Close();
      return;
    }
    panel_delegate_ = std::make_unique<views::BubbleDialogDelegate>(
        this, views::BubbleBorder::TOP_RIGHT, views::BubbleBorder::DIALOG_SHADOW,
        /*autosize=*/true);
    panel_delegate_->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
    panel_delegate_->SetShowCloseButton(false);
    panel_delegate_->set_margins(gfx::Insets(12));
    panel_delegate_->set_corner_radius(16);
    panel_delegate_->SetContentsView(
        std::make_unique<SitePanelView>(location_bar_, browser_));
    panel_ = views::BubbleDialogDelegate::CreateBubble(
        panel_delegate_.get(), base::BindOnce(&SitePanelButton::OnPanelClosed,
                                              weak_factory_.GetWeakPtr()));
    panel_->Show();
  }

  void OnPanelClosed(views::Widget::ClosedReason reason) {
    // Not from within the panel's own notifications (losing focus closes it while
    // they run, e.g. as an extension's popup or dialog opens): once they're over.
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE,
                                                               std::move(panel_));
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
        FROM_HERE, std::move(panel_delegate_));
  }

  raw_ptr<LocationBarView> location_bar_;
  raw_ptr<BrowserWindowInterface> browser_;
  std::unique_ptr<views::BubbleDialogDelegate> panel_delegate_;
  std::unique_ptr<views::Widget> panel_;
  base::WeakPtrFactory<SitePanelButton> weak_factory_{this};
};

BEGIN_METADATA(SitePanelButton)
END_METADATA

}  // namespace

void AddSitePanelButton(views::View* location_bar, BrowserWindowInterface* browser) {
  if (browser && browser->GetType() == BrowserWindowInterface::TYPE_NORMAL) {
    location_bar->AddChildView(std::make_unique<SitePanelButton>(
        static_cast<LocationBarView*>(location_bar), browser));
  }
}

views::View* SitePanelButtonOf(views::View* location_bar) {
  return location_bar->GetViewByID(kButtonViewId);
}

views::View* ExtensionsAnchor(views::View* toolbar) {
  if (!toolbar || sidebar::ToolbarInset(toolbar) < 0) {
    return nullptr;
  }
  views::View* button = toolbar->GetViewByID(kButtonViewId);
  return button && button->GetVisible() ? button : nullptr;
}

}  // namespace oblada::site_panel
