#include "oblada/browser/barrier/barrier_ui.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/web_contents.h"
#include "oblada/browser/barrier/barrier.h"
#include "oblada/browser/barrier/cosmetic_filters.h"
#include "oblada/browser/barrier/tab_helper.h"
#include "oblada/browser/l10n.h"
#include "oblada/browser/ui/sidebar_layout.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/models/simple_combobox_model.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/text_constants.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/background.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/separator.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/vector_icons.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace oblada::barrier {

namespace {

// The button's view id in the location bar (one Chromium's view_ids.h leaves
// unused).
constexpr int kButtonViewId = 55631;
constexpr int kWidth = 320;
constexpr int kRadius = 10;
constexpr int kListHeight = 140;

constexpr char kSettingsUrl[] = "chrome://settings/oblada";
constexpr char kFiltersUrl[] = "chrome://settings/oblada/filters";

// Whether the advanced controls and the lists are shown: as last left, this
// session.
bool g_advanced = false;
bool g_blocked_list = false;
bool g_fingerprinting_list = false;

std::u16string Count(int count) {
  return count > 99 ? u"99+" : base::NumberToString16(count);
}

std::unique_ptr<views::Label> BoldLabel(const std::u16string& text) {
  auto label = std::make_unique<views::Label>(text);
  label->SetFontList(
      label->font_list().DeriveWithWeight(gfx::Font::Weight::SEMIBOLD));
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  return label;
}

std::unique_ptr<views::Label> SecondaryLabel(const std::u16string& text) {
  auto label = std::make_unique<views::Label>(text);
  label->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetMultiLine(true);
  label->SizeToFit(kWidth);
  return label;
}

// A row: `label` on the left, `control` on the right.
views::BoxLayoutView* AddRow(views::View* parent,
                             const std::u16string& label,
                             std::unique_ptr<views::View> control) {
  auto* row = parent->AddChildView(std::make_unique<views::BoxLayoutView>());
  row->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  row->SetBetweenChildSpacing(12);
  auto* text = row->AddChildView(std::make_unique<views::Label>(label));
  text->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  text->SetMultiLine(true);
  row->SetFlexForView(text, 1);
  control->GetViewAccessibility().SetName(label);
  row->AddChildView(std::move(control));
  return row;
}

// A button that shows or hides what's below it.
std::unique_ptr<views::LabelButton> Disclosure(views::Button::PressedCallback callback,
                                               const std::u16string& text,
                                               bool open) {
  auto button = std::make_unique<views::LabelButton>(std::move(callback), text);
  button->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(
          open ? vector_icons::kCaretUpOldIcon : vector_icons::kCaretDownOldIcon,
          ui::kColorIcon, 16));
  button->SetImageLabelSpacing(6);
  button->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  return button;
}

// The controls themselves, rebuilt as their state changes.
class BarrierPanelView : public views::View {
  METADATA_HEADER(BarrierPanelView, views::View)

 public:
  BarrierPanelView(BrowserWindowInterface* browser, base::RepeatingClosure close)
      : browser_(browser), close_(std::move(close)) {
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 10));
    settings_subscription_ = ObserveSettings(
        profile(), base::BindRepeating(&BarrierPanelView::Rebuild,
                                       base::Unretained(this)));
    if (BarrierTabHelper* helper = tab_helper()) {
      blocked_subscription_ = helper->ObserveBlocked(base::BindRepeating(
          &BarrierPanelView::Rebuild, base::Unretained(this)));
    }
    Build();
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kWidth, GetLayoutManager()->GetPreferredHeightForWidth(
                                 this, kWidth));
  }

 private:
  Profile* profile() { return browser_->GetProfile(); }

  content::WebContents* contents() {
    return browser_->GetTabStripModel()->GetActiveWebContents();
  }

  BarrierTabHelper* tab_helper() {
    return contents() ? BarrierTabHelper::FromWebContents(contents()) : nullptr;
  }

  GURL page_url() {
    return contents() ? contents()->GetLastCommittedURL() : GURL();
  }

  void Rebuild() {
    RemoveAllChildViews();
    Build();
    PreferredSizeChanged();
  }

  void Build() {
    const GURL url = page_url();
    const std::string site = SiteOf(url);
    if (!IsOn()) {
      AddChildView(BoldLabel(L10n("barrier.title")));
      AddChildView(SecondaryLabel(L10n("barrier.off_everywhere")));
      AddFooter(/*element_picker=*/false);
      return;
    }
    if (site.empty()) {
      AddChildView(BoldLabel(L10n("barrier.title")));
      AddChildView(SecondaryLabel(L10n("barrier.not_a_site")));
      AddFooter(/*element_picker=*/false);
      return;
    }
    const Settings settings = GetSettings(profile(), url);

    // The site, and the Barrier for it.
    auto* header = AddChildView(std::make_unique<views::BoxLayoutView>());
    header->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    header->SetBetweenChildSpacing(10);
    auto* icon = header->AddChildView(std::make_unique<views::ImageView>(
        ui::ImageModel::FromVectorIcon(
            vector_icons::kShieldIcon,
            settings.enabled ? ui::kColorSysPrimary : ui::kColorIconDisabled,
            24)));
    icon->SetCanProcessEventsWithinSubtree(false);
    auto* titles = header->AddChildView(std::make_unique<views::BoxLayoutView>());
    titles->SetOrientation(views::BoxLayout::Orientation::kVertical);
    titles->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStart);
    titles->AddChildView(BoldLabel(base::UTF8ToUTF16(site)));
    titles->AddChildView(SecondaryLabel(
        L10n(settings.enabled ? "barrier.up" : "barrier.down")));
    header->SetFlexForView(titles, 1);
    auto toggle = std::make_unique<views::ToggleButton>();
    toggle->SetIsOn(settings.enabled);
    toggle->GetViewAccessibility().SetName(L10n("barrier.toggle"));
    views::ToggleButton* raw_toggle = toggle.get();
    raw_toggle->SetCallback(base::BindRepeating(
        [](BarrierPanelView* view, views::ToggleButton* toggle) {
          view->Change(base::BindRepeating([](SiteOverrides& overrides, bool on) {
                         overrides.enabled = on;
                       }),
                       toggle->GetIsOn());
        },
        base::Unretained(this), base::Unretained(raw_toggle)));
    header->AddChildView(std::move(toggle));

    if (!settings.enabled) {
      AddChildView(SecondaryLabel(L10n("barrier.down_note")));
      AddFooter(/*element_picker=*/false);
      return;
    }

    // What it blocked.
    BarrierTabHelper* helper = tab_helper();
    const int count = helper ? helper->blocked_count() : 0;
    auto* blocked = AddChildView(std::make_unique<views::BoxLayoutView>());
    blocked->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    blocked->SetInsideBorderInsets(gfx::Insets::VH(4, 0));
    auto* disclosure = blocked->AddChildView(Disclosure(
        base::BindRepeating(&BarrierPanelView::Toggle, base::Unretained(this),
                            &g_blocked_list),
        L10n("barrier.blocked"), g_blocked_list && count));
    disclosure->SetEnabled(count > 0);
    blocked->SetFlexForView(disclosure, 1);
    blocked->AddChildView(BoldLabel(base::NumberToString16(count)));
    if (g_blocked_list && helper && count) {
      auto list = std::make_unique<views::BoxLayoutView>();
      list->SetOrientation(views::BoxLayout::Orientation::kVertical);
      list->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStart);
      for (const GURL& item : helper->blocked()) {
        auto* label = list->AddChildView(
            std::make_unique<views::Label>(base::UTF8ToUTF16(item.spec())));
        label->SetElideBehavior(gfx::ELIDE_MIDDLE);
        label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
        label->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
        label->SetMaximumWidthSingleLine(kWidth - 24);
        label->SetTooltipText(base::UTF8ToUTF16(item.spec()));
      }
      auto* scroll = AddChildView(std::make_unique<views::ScrollView>());
      scroll->SetContents(std::move(list));
      scroll->ClipHeightTo(0, kListHeight);
      scroll->SetBackground(views::CreateRoundedRectBackground(
          ui::kColorSysNeutralContainer, kRadius));
    }

    AddChildView(std::make_unique<views::Separator>());
    AddChildView(Disclosure(base::BindRepeating(&BarrierPanelView::Toggle,
                                                base::Unretained(this),
                                                &g_advanced),
                            L10n("barrier.advanced"), g_advanced));
    if (g_advanced) {
      AddControls(settings);
    }
    AddFooter(/*element_picker=*/true);
  }

  void AddControls(const Settings& settings) {
    auto* controls = AddChildView(std::make_unique<views::BoxLayoutView>());
    controls->SetOrientation(views::BoxLayout::Orientation::kVertical);
    controls->SetBetweenChildSpacing(8);

    AddChoice(controls, L10n("barrier.ads"),
              {L10n("barrier.ads.aggressive"), L10n("barrier.ads.standard"),
               L10n("barrier.ads.disabled")},
              settings.ads == AdBlocking::kAggressive  ? 0
              : settings.ads == AdBlocking::kStandard ? 1
                                                      : 2,
              base::BindRepeating([](SiteOverrides& overrides, size_t index) {
                overrides.ads = index == 0   ? AdBlocking::kAggressive
                                : index == 1 ? AdBlocking::kStandard
                                             : AdBlocking::kDisabled;
              }));
    AddChoice(controls, L10n("barrier.https"),
              {L10n("barrier.https.strict"), L10n("barrier.https.standard"),
               L10n("barrier.https.disabled")},
              settings.https == HttpsUpgrade::kStrict     ? 0
              : settings.https == HttpsUpgrade::kStandard ? 1
                                                          : 2,
              base::BindRepeating([](SiteOverrides& overrides, size_t index) {
                overrides.https = index == 0   ? HttpsUpgrade::kStrict
                                  : index == 1 ? HttpsUpgrade::kStandard
                                               : HttpsUpgrade::kDisabled;
              }));
    AddSwitch(controls, L10n("barrier.scripts"), settings.block_scripts,
              base::BindRepeating([](SiteOverrides& overrides, bool on) {
                overrides.block_scripts = on;
              }));
    AddSwitch(controls, L10n("barrier.fingerprinting"), settings.fingerprinting,
              base::BindRepeating([](SiteOverrides& overrides, bool on) {
                overrides.fingerprinting = on;
              }));
    if (settings.fingerprinting) {
      int protected_apis = 0;
      for (const auto& api : FingerprintingApis()) {
        protected_apis += !(settings.fingerprinting_exceptions & api.api);
      }
      controls->AddChildView(Disclosure(
          base::BindRepeating(&BarrierPanelView::Toggle,
                              base::Unretained(this), &g_fingerprinting_list),
          base::ReplaceStringPlaceholders(
              L10n("barrier.fingerprinting_apis"),
              std::vector<std::u16string>{base::NumberToString16(protected_apis)},
              nullptr),
          g_fingerprinting_list));
      if (g_fingerprinting_list) {
        for (const auto& api : FingerprintingApis()) {
          AddSwitch(controls,
                    L10n(base::StrCat({"barrier.api.", api.name})),
                    !(settings.fingerprinting_exceptions & api.api),
                    base::BindRepeating(
                        [](uint32_t current, FingerprintingApi api,
                           SiteOverrides& overrides, bool on) {
                          overrides.fingerprinting_exceptions =
                              on ? current & ~api : current | api;
                        },
                        settings.fingerprinting_exceptions, api.api));
        }
      }
    }
    AddChoice(controls, L10n("barrier.cookies"),
              {L10n("barrier.cookies.block_third_party"),
               L10n("barrier.cookies.allow_all")},
              settings.cookies == Cookies::kBlockThirdParty ? 0 : 1,
              base::BindRepeating([](SiteOverrides& overrides, size_t index) {
                overrides.cookies =
                    index == 0 ? Cookies::kBlockThirdParty : Cookies::kAllowAll;
              }));
    AddSwitch(controls, L10n("barrier.forget"), settings.forget,
              base::BindRepeating([](SiteOverrides& overrides, bool on) {
                overrides.forget = on;
              }));
  }

  void AddSwitch(views::View* parent,
                 const std::u16string& label,
                 bool on,
                 base::RepeatingCallback<void(SiteOverrides&, bool)> change) {
    auto toggle = std::make_unique<views::ToggleButton>();
    toggle->SetIsOn(on);
    views::ToggleButton* raw = toggle.get();
    raw->SetCallback(base::BindRepeating(
        [](BarrierPanelView* view, views::ToggleButton* toggle,
           base::RepeatingCallback<void(SiteOverrides&, bool)> change) {
          view->Change(change, toggle->GetIsOn());
        },
        base::Unretained(this), base::Unretained(raw), std::move(change)));
    AddRow(parent, label, std::move(toggle));
  }

  void AddChoice(views::View* parent,
                 const std::u16string& label,
                 std::vector<std::u16string> options,
                 size_t selected,
                 base::RepeatingCallback<void(SiteOverrides&, size_t)> change) {
    std::vector<ui::SimpleComboboxModel::Item> items;
    for (std::u16string& option : options) {
      items.emplace_back(std::move(option));
    }
    auto combobox = std::make_unique<views::Combobox>(
        std::make_unique<ui::SimpleComboboxModel>(std::move(items)));
    combobox->SetSelectedIndex(selected);
    views::Combobox* raw = combobox.get();
    raw->SetCallback(base::BindRepeating(
        [](BarrierPanelView* view, views::Combobox* combobox,
           base::RepeatingCallback<void(SiteOverrides&, size_t)> change) {
          if (std::optional<size_t> index = combobox->GetSelectedIndex()) {
            view->ChangeIndex(change, *index);
          }
        },
        base::Unretained(this), base::Unretained(raw), std::move(change)));
    AddRow(parent, label, std::move(combobox));
  }

  void AddFooter(bool element_picker) {
    AddChildView(std::make_unique<views::Separator>());
    auto* footer = AddChildView(std::make_unique<views::BoxLayoutView>());
    footer->SetBetweenChildSpacing(4);
    auto add = [&](const std::u16string& text, views::Button::PressedCallback callback) {
      auto* button = footer->AddChildView(
          std::make_unique<views::LabelButton>(std::move(callback), text));
      button->SetBackground(views::CreateRoundedRectBackground(
          ui::kColorSysNeutralContainer, kRadius));
      button->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(6, 9)));
    };
    if (element_picker) {
      add(L10n("barrier.block_element"),
          base::BindRepeating(&BarrierPanelView::BlockElement,
                              base::Unretained(this)));
    }
    add(L10n("barrier.filters"),
        base::BindRepeating(&BarrierPanelView::Open, base::Unretained(this),
                            GURL(kFiltersUrl)));
    add(L10n("barrier.global"),
        base::BindRepeating(&BarrierPanelView::Open, base::Unretained(this),
                            GURL(kSettingsUrl)));
  }

  void Toggle(bool* shown) {
    *shown = !*shown;
    Rebuild();
  }

  // The site's settings change, then the page reloads with them.
  void Change(base::RepeatingCallback<void(SiteOverrides&, bool)> change,
              bool value) {
    const std::string site = SiteOf(page_url());
    SiteOverrides overrides = GetSiteOverrides(profile(), site);
    change.Run(overrides, value);
    Apply(site, overrides);
  }

  void ChangeIndex(base::RepeatingCallback<void(SiteOverrides&, size_t)> change,
                   size_t index) {
    const std::string site = SiteOf(page_url());
    SiteOverrides overrides = GetSiteOverrides(profile(), site);
    change.Run(overrides, index);
    Apply(site, overrides);
  }

  void Apply(const std::string& site, const SiteOverrides& overrides) {
    SetSiteOverrides(profile(), site, overrides);
    if (content::WebContents* web_contents = contents()) {
      web_contents->GetController().Reload(content::ReloadType::NORMAL,
                                           /*check_for_repost=*/true);
    }
  }

  void BlockElement() {
    content::WebContents* web_contents = contents();
    close_.Run();
    if (web_contents) {
      StartElementPicker(web_contents);
    }
  }

  void Open(const GURL& url) {
    base::WeakPtr<BrowserWindowInterface> browser = browser_->GetWeakPtr();
    close_.Run();
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::WeakPtr<BrowserWindowInterface> browser,
                          const GURL& url) {
                         if (browser) {
                           NavigateParams params(browser.get(), url,
                                                 ui::PAGE_TRANSITION_LINK);
                           params.disposition =
                               WindowOpenDisposition::SINGLETON_TAB;
                           Navigate(&params);
                         }
                       },
                       browser, url));
  }

  raw_ptr<BrowserWindowInterface> browser_;
  base::RepeatingClosure close_;
  base::CallbackListSubscription settings_subscription_;
  base::CallbackListSubscription blocked_subscription_;
};

BEGIN_METADATA(BarrierPanelView)
END_METADATA

class BarrierButton : public views::ImageButton {
  METADATA_HEADER(BarrierButton, views::ImageButton)

 public:
  BarrierButton(LocationBarView* location_bar, BrowserWindowInterface* browser)
      : views::ImageButton(base::BindRepeating(&BarrierButton::ShowPanel,
                                               base::Unretained(this))),
        location_bar_(location_bar),
        browser_(browser) {
    SetID(kButtonViewId);
    SetTooltipText(L10n("barrier.title"));
    GetViewAccessibility().SetName(L10n("barrier.title"));
    SetImageHorizontalAlignment(ALIGN_CENTER);
    SetImageVerticalAlignment(ALIGN_MIDDLE);
    SetPreferredSize(gfx::Size(28, 28));
    views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::ON);
    views::InstallCircleHighlightPathGenerator(this);
    pref_registrar_.Init(browser->GetProfile()->GetPrefs());
    pref_registrar_.Add(kShowCountPref,
                        base::BindRepeating(&BarrierButton::SchedulePaint,
                                            base::Unretained(this)));
    settings_subscription_ = ObserveSettings(
        browser->GetProfile(),
        base::BindRepeating(&BarrierButton::Update, base::Unretained(this)));
    Update();
  }

  // As the location bar lays out, or the site's settings change.
  void Update() {
    content::WebContents* contents = location_bar_->GetWebContents();
    if (contents != contents_) {
      contents_ = contents;
      blocked_subscription_ = {};
      if (auto* helper =
              contents ? BarrierTabHelper::FromWebContents(contents) : nullptr) {
        blocked_subscription_ = helper->ObserveBlocked(base::BindRepeating(
            &BarrierButton::OnBlockedChanged, base::Unretained(this)));
      }
    }
    const GURL url = contents ? contents->GetLastCommittedURL() : GURL();
    const bool web_page = !SiteOf(url).empty();
    SetVisible(web_page &&
               !oblada::sidebar::HasSidebarLocationBar(location_bar_->parent()));
    // (The site panel has the controls in the sidebar.)
    if (!GetVisible()) {
      ClosePanel();
    }
    on_ = web_page && GetSettings(browser_->GetProfile(), url).enabled;
    SetImageModel(STATE_NORMAL,
                  ui::ImageModel::FromVectorIcon(
                      vector_icons::kShieldIcon,
                      on_ ? ui::ColorId{kColorOmniboxResultsIcon}
                          : ui::ColorId{ui::kColorIconDisabled},
                      16));
    OnBlockedChanged();
  }

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
    panel_delegate_->set_margins(gfx::Insets(14));
    panel_delegate_->set_corner_radius(16);
    panel_delegate_->SetContentsView(std::make_unique<BarrierPanelView>(
        browser_, base::BindRepeating(&BarrierButton::ClosePanel,
                                      weak_factory_.GetWeakPtr())));
    panel_ = views::BubbleDialogDelegate::CreateBubble(
        panel_delegate_.get(), base::BindOnce(&BarrierButton::OnPanelClosed,
                                              weak_factory_.GetWeakPtr()));
    panel_->Show();
  }

  // views::ImageButton:
  void PaintButtonContents(gfx::Canvas* canvas) override {
    views::ImageButton::PaintButtonContents(canvas);
    if (!on_ || !count_ ||
        !browser_->GetProfile()->GetPrefs()->GetBoolean(kShowCountPref)) {
      return;
    }
    // The number, on a badge at the top right.
    const std::u16string text = Count(count_);
    const gfx::FontList font = gfx::FontList().DeriveWithSizeDelta(-3).DeriveWithWeight(
        gfx::Font::Weight::BOLD);
    const int width = std::max(12, gfx::GetStringWidth(text, font) + 6);
    const gfx::Rect badge(this->width() - width, 1, width, 12);
    const ui::ColorProvider* colors = GetColorProvider();
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(ui::kColorSysPrimary));
    canvas->DrawRoundRect(badge, 6, flags);
    canvas->DrawStringRectWithFlags(text, font,
                                    colors->GetColor(ui::kColorSysOnPrimary),
                                    badge, gfx::Canvas::TEXT_ALIGN_CENTER);
  }

 private:
  void OnBlockedChanged() {
    auto* helper = contents_ ? BarrierTabHelper::FromWebContents(contents_) : nullptr;
    const int count = helper ? helper->blocked_count() : 0;
    if (count != count_) {
      count_ = count;
      SchedulePaint();
    }
  }

  void ClosePanel() {
    if (panel_) {
      panel_->Close();
    }
  }

  void OnPanelClosed(views::Widget::ClosedReason reason) {
    // Not from within the panel's own notifications: once they're over.
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
        FROM_HERE, std::move(panel_));
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
        FROM_HERE, std::move(panel_delegate_));
  }

  raw_ptr<LocationBarView> location_bar_;
  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<content::WebContents> contents_ = nullptr;
  bool on_ = false;
  int count_ = 0;
  PrefChangeRegistrar pref_registrar_;
  base::CallbackListSubscription settings_subscription_;
  base::CallbackListSubscription blocked_subscription_;
  std::unique_ptr<views::BubbleDialogDelegate> panel_delegate_;
  std::unique_ptr<views::Widget> panel_;
  base::WeakPtrFactory<BarrierButton> weak_factory_{this};
};

BEGIN_METADATA(BarrierButton)
END_METADATA

}  // namespace

void AddBarrierButton(views::View* location_bar,
                      BrowserWindowInterface* browser) {
  if (browser && browser->GetType() == BrowserWindowInterface::TYPE_NORMAL) {
    location_bar->AddChildView(std::make_unique<BarrierButton>(
        static_cast<LocationBarView*>(location_bar), browser));
  }
}

views::View* BarrierButtonOf(views::View* location_bar) {
  return location_bar->GetViewByID(kButtonViewId);
}

void UpdateBarrierButton(views::View* location_bar) {
  if (views::View* button = BarrierButtonOf(location_bar)) {
    static_cast<BarrierButton*>(button)->Update();
  }
}

std::unique_ptr<views::View> CreateBarrierSection(
    BrowserWindowInterface* browser,
    base::RepeatingClosure close) {
  return std::make_unique<BarrierPanelView>(browser, std::move(close));
}

}  // namespace oblada::barrier
