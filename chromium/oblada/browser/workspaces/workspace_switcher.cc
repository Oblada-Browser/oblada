#include "oblada/browser/workspaces/workspace_switcher.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "oblada/browser/containers/containers.h"
#include "oblada/browser/l10n.h"
#include "oblada/browser/ui/downloads_button.h"
#include "oblada/browser/ui/icons.h"
#include "oblada/browser/workspaces/workspace_slider.h"
#include "oblada/browser/workspaces/workspaces.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/image/canvas_image_source.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"

namespace oblada::workspaces {

namespace {

constexpr int kButtonSize = 28;
constexpr int kSpacing = 4;
// In a row, as in Zen, the buttons spread over the sidebar's width up to this far
// apart, and get smaller (down to kMinButtonSize) rather than not fit.
constexpr int kMaxSpacing = 10;
constexpr int kMinButtonSize = 20;

// The dot for a workspace without an icon, in its color.
SkColor DotColor(int color) {
  switch (static_cast<tab_groups::TabGroupColorId>(color)) {
    case tab_groups::TabGroupColorId::kBlue:
      return SkColorSetRGB(0x8A, 0xB4, 0xF8);
    case tab_groups::TabGroupColorId::kRed:
      return SkColorSetRGB(0xF2, 0x8B, 0x82);
    case tab_groups::TabGroupColorId::kYellow:
      return SkColorSetRGB(0xFD, 0xD6, 0x63);
    case tab_groups::TabGroupColorId::kGreen:
      return SkColorSetRGB(0x81, 0xC9, 0x95);
    case tab_groups::TabGroupColorId::kPink:
      return SkColorSetRGB(0xFF, 0x8B, 0xCB);
    case tab_groups::TabGroupColorId::kPurple:
      return SkColorSetRGB(0xC5, 0x8A, 0xF9);
    case tab_groups::TabGroupColorId::kCyan:
      return SkColorSetRGB(0x78, 0xD9, 0xEC);
    case tab_groups::TabGroupColorId::kOrange:
      return SkColorSetRGB(0xFC, 0xAD, 0x70);
    default:
      return SkColorSetRGB(0x9A, 0xA0, 0xA6);
  }
}

// The color a window's colors come from while it shows a workspace of `color`:
// its dot's, or pure gray for a gray one.
SkColor PaletteSeed(int color) {
  return static_cast<tab_groups::TabGroupColorId>(color) ==
                 tab_groups::TabGroupColorId::kGrey
             ? SkColorSetRGB(0x80, 0x80, 0x80)
             : DotColor(color);
}

// The colors a workspace (its tab groups) can have, as a picker offers them.
std::vector<icons::PickerColor> PickerColors() {
  std::vector<icons::PickerColor> colors;
  for (tab_groups::TabGroupColorId id :
       {tab_groups::TabGroupColorId::kBlue, tab_groups::TabGroupColorId::kCyan,
        tab_groups::TabGroupColorId::kGreen,
        tab_groups::TabGroupColorId::kYellow,
        tab_groups::TabGroupColorId::kOrange, tab_groups::TabGroupColorId::kRed,
        tab_groups::TabGroupColorId::kPink,
        tab_groups::TabGroupColorId::kPurple,
        tab_groups::TabGroupColorId::kGrey}) {
    colors.push_back(
        {base::NumberToString(static_cast<int>(id)), DotColor(static_cast<int>(id))});
  }
  return colors;
}

// A workspace's icon from before there were flat ones: its emoji, else a dot of
// its color.
class IconSource : public gfx::CanvasImageSource {
 public:
  IconSource(std::u16string emoji, SkColor dot, int size)
      : gfx::CanvasImageSource(gfx::Size(size, size)),
        emoji_(std::move(emoji)),
        dot_(dot) {}

  // gfx::CanvasImageSource:
  void Draw(gfx::Canvas* canvas) override {
    const gfx::Rect bounds(size());
    if (!emoji_.empty()) {
      canvas->DrawStringRectWithFlags(
          emoji_,
          gfx::FontList().DeriveWithSizeDelta(size().height() - 16),
          SK_ColorBLACK, bounds, gfx::Canvas::TEXT_ALIGN_CENTER);
      return;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(dot_);
    canvas->DrawCircle(gfx::RectF(bounds).CenterPoint(), size().width() / 4.f,
                       flags);
  }

 private:
  std::u16string emoji_;
  SkColor dot_;
};

// A workspace's button: its icon (or a dot of its color) on a rounded highlight
// when it's the one shown, or under the pointer; the others are fainter.
class WorkspaceButton : public views::Button, public views::ContextMenuController {
  METADATA_HEADER(WorkspaceButton, views::Button)

 public:
  using MenuCallback = base::RepeatingCallback<void(views::View*)>;

  WorkspaceButton(PressedCallback pressed,
                  MenuCallback menu,
                  const Workspace& workspace,
                  bool shown)
      : views::Button(std::move(pressed)),
        menu_(std::move(menu)),
        icon_(base::UTF8ToUTF16(workspace.icon)),
        color_(workspace.color),
        shown_(shown) {
    SetPreferredSize(gfx::Size(kButtonSize, kButtonSize));
    SetTooltipText(base::UTF8ToUTF16(workspace.name));
    GetViewAccessibility().SetName(base::UTF8ToUTF16(workspace.name));
    set_context_menu_controller(this);
  }

  // views::Button:
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    const SkColor foreground =
        colors->GetColor(kColorTabForegroundInactiveFrameActive);
    const gfx::RectF bounds(GetLocalBounds());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    if (shown_ || GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) {
      flags.setColor(SkColorSetA(foreground, shown_ ? 0x2E : 0x16));
      canvas->DrawRoundRect(bounds, 8, flags);
    }
    const ui::ImageModel image =
        icons::Image(base::UTF16ToUTF8(icon_), DotColor(color_), 16);
    if (!image.IsEmpty() || !icon_.empty()) {
      if (!shown_) {
        canvas->SaveLayerAlpha(GetState() == STATE_HOVERED ? 0xD0 : 0x80);
      }
      if (!image.IsEmpty()) {
        const gfx::ImageSkia skia = image.Rasterize(colors);
        canvas->DrawImageInt(skia, (width() - skia.width()) / 2,
                             (height() - skia.height()) / 2);
      } else {
        canvas->DrawStringRectWithFlags(
            icon_, gfx::FontList().DeriveWithSizeDelta(1), foreground,
            GetLocalBounds(), gfx::Canvas::TEXT_ALIGN_CENTER);
      }
      if (!shown_) {
        canvas->Restore();
      }
      return;
    }
    flags.setColor(SkColorSetA(DotColor(color_), shown_ ? 0xFF : 0x80));
    canvas->DrawCircle(bounds.CenterPoint(), shown_ ? 5 : 4, flags);
  }

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override {
    menu_.Run(this);
  }

 private:
  MenuCallback menu_;
  std::u16string icon_;
  int color_;
  bool shown_;
};

BEGIN_METADATA(WorkspaceButton)
END_METADATA

}  // namespace

// A workspace's menu: rename, icon, container, delete.
class WorkspaceMenu : public ui::SimpleMenuModel::Delegate {
 public:
  WorkspaceMenu(BrowserWindowInterface* browser, std::string id)
      : browser_(browser), id_(std::move(id)) {}
  WorkspaceMenu(const WorkspaceMenu&) = delete;
  WorkspaceMenu& operator=(const WorkspaceMenu&) = delete;
  ~WorkspaceMenu() override = default;

  void Run(views::View* anchor) {
    anchor_ = anchor;
    Profile* profile = browser_->GetProfile();
    model_ = std::make_unique<ui::SimpleMenuModel>(this);
    model_->AddItem(kRename, L10n("workspaces.rename"));

    model_->AddItem(kIconAndColor, L10n("workspaces.icon_color"));
    model_->AddCheckItem(kTintsWindow, L10n("workspaces.tints_window"));

    containers_ = std::make_unique<ui::SimpleMenuModel>(this);
    containers_->AddCheckItem(kContainerNone,
                              L10n("workspaces.no_container"));
    container_ids_.clear();
    for (const containers::Container& container :
         containers::GetContainers(profile)) {
      if (container.temporary) {
        continue;
      }
      containers_->AddCheckItem(kContainerFirst + container_ids_.size(),
                                base::UTF8ToUTF16(container.name));
      container_ids_.push_back(container.id);
    }
    model_->AddSubMenu(kContainerMenu, L10n("workspaces.container"),
                       containers_.get());

    model_->AddSeparator(ui::NORMAL_SEPARATOR);
    model_->AddItem(kDelete, L10n("workspaces.delete"));

    runner_ = std::make_unique<views::MenuRunner>(
        model_.get(), views::MenuRunner::HAS_MNEMONICS |
                          views::MenuRunner::CONTEXT_MENU);
    runner_->RunMenuAt(anchor->GetWidget(), nullptr, anchor->GetBoundsInScreen(),
                       views::MenuAnchorPosition::kTopLeft,
                       ui::mojom::MenuSourceType::kMouse);
  }

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override {
    const std::optional<Workspace> workspace = Get();
    if (!workspace) {
      return false;
    }
    if (command_id == kTintsWindow) {
      return workspace->tints_window;
    }
    if (command_id == kContainerNone) {
      return workspace->container.empty();
    }
    if (command_id >= kContainerFirst &&
        command_id < kContainerFirst + static_cast<int>(container_ids_.size())) {
      return workspace->container == container_ids_[command_id - kContainerFirst];
    }
    return false;
  }

  bool IsCommandIdEnabled(int command_id) const override {
    // There's always a workspace.
    return command_id != kDelete ||
           GetWorkspaces(browser_->GetProfile()).size() > 1;
  }

  void ExecuteCommand(int command_id, int event_flags) override {
    std::optional<Workspace> workspace = Get();
    if (!workspace) {
      return;
    }
    Profile* profile = browser_->GetProfile();
    if (command_id == kRename) {
      // Once the menu is gone.
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(&WorkspaceMenu::ShowRename,
                                    weak_factory_.GetWeakPtr()));
    } else if (command_id == kDelete) {
      RemoveWorkspace(profile, id_);
    } else if (command_id == kIconAndColor) {
      // Once the menu is gone.
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(&WorkspaceMenu::ShowIconPicker,
                                    weak_factory_.GetWeakPtr()));
    } else if (command_id == kTintsWindow) {
      workspace->tints_window = !workspace->tints_window;
      UpdateWorkspace(profile, *workspace);
    } else if (command_id == kContainerNone) {
      workspace->container.clear();
      UpdateWorkspace(profile, *workspace);
    } else if (command_id >= kContainerFirst &&
               command_id < kContainerFirst +
                                static_cast<int>(container_ids_.size())) {
      workspace->container = container_ids_[command_id - kContainerFirst];
      UpdateWorkspace(profile, *workspace);
    }
  }

 private:
  enum Command {
    kRename = 1,
    kDelete,
    kIconAndColor,
    kTintsWindow,
    kContainerMenu,
    kContainerNone = 200,
    kContainerFirst,
  };

  std::optional<Workspace> Get() const {
    return FindWorkspace(browser_->GetProfile(), id_);
  }

  void ShowIconPicker() {
    if (anchor_) {
      ShowWorkspaceIconPicker(browser_, id_, anchor_);
    }
  }

  // A small bubble with the name to edit, over the workspace's button.
  void ShowRename() {
    const std::optional<Workspace> workspace = Get();
    if (!workspace || !anchor_ || !anchor_->GetWidget()) {
      return;
    }
    auto textfield = std::make_unique<views::Textfield>();
    textfield->SetText(base::UTF8ToUTF16(workspace->name));
    textfield->SelectAll(false);
    textfield->SetDefaultWidthInChars(20);
    textfield->GetViewAccessibility().SetName(L10n("workspaces.rename"));
    name_field_ = textfield.get();

    rename_ = std::make_unique<views::BubbleDialogDelegate>(
        anchor_, views::BubbleBorder::BOTTOM_LEFT,
        views::BubbleBorder::DIALOG_SHADOW, /*autosize=*/true);
    rename_->SetTitle(L10n("workspaces.rename"));
    rename_->SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk));
    rename_->SetAcceptCallback(base::BindOnce(
        [](base::WeakPtr<WorkspaceMenu> menu) {
          if (!menu || !menu->name_field_) {
            return;
          }
          std::optional<Workspace> workspace = menu->Get();
          if (workspace && !menu->name_field_->GetText().empty()) {
            workspace->name = base::UTF16ToUTF8(menu->name_field_->GetText());
            UpdateWorkspace(menu->browser_->GetProfile(), *workspace);
          }
        },
        weak_factory_.GetWeakPtr()));
    rename_->SetInitiallyFocusedView(textfield.get());
    rename_->SetContentsView(std::move(textfield));
    rename_widget_ = views::BubbleDialogDelegate::CreateBubble(
        rename_.get(), base::BindOnce(
                           [](base::WeakPtr<WorkspaceMenu> menu,
                              views::Widget::ClosedReason reason) {
                             if (menu) {
                               // Not from within the bubble's own
                               // notifications (losing focus closes it while
                               // they run): once they're over.
                               menu->name_field_ = nullptr;
                               auto* runner = base::SequencedTaskRunner::
                                   GetCurrentDefault().get();
                               runner->DeleteSoon(
                                   FROM_HERE, std::move(menu->rename_widget_));
                               runner->DeleteSoon(FROM_HERE,
                                                  std::move(menu->rename_));
                             }
                           },
                           weak_factory_.GetWeakPtr()));
    rename_widget_->Show();
  }

  raw_ptr<BrowserWindowInterface> browser_;
  std::string id_;
  raw_ptr<views::View> anchor_ = nullptr;
  std::unique_ptr<ui::SimpleMenuModel> model_;
  std::unique_ptr<ui::SimpleMenuModel> containers_;
  std::vector<std::string> container_ids_;
  std::unique_ptr<views::MenuRunner> runner_;
  raw_ptr<views::Textfield> name_field_ = nullptr;
  std::unique_ptr<views::BubbleDialogDelegate> rename_;
  std::unique_ptr<views::Widget> rename_widget_;
  base::WeakPtrFactory<WorkspaceMenu> weak_factory_{this};
};

WorkspaceSwitcher::WorkspaceSwitcher(BrowserWindowInterface* browser)
    : browser_(browser) {
  SetLayoutManager(std::make_unique<views::BoxLayout>(
                       views::BoxLayout::Orientation::kHorizontal,
                       gfx::Insets::VH(6, 0), kSpacing))
      ->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kCenter);
  downloads_ = AddChildView(downloads::CreateButton(browser_));
  downloads_->SetPreferredSize(gfx::Size(kButtonSize, kButtonSize));
  browser_->GetTabStripModel()->AddObserver(this);
  subscription_ = ObserveWorkspaces(
      browser_->GetProfile(),
      base::BindRepeating(&WorkspaceSwitcher::Update, base::Unretained(this)));
  Update();
}

WorkspaceSwitcher::~WorkspaceSwitcher() = default;

void WorkspaceSwitcher::AddedToWidget() {
  Update();
  // It's in the sidebar, whose swipes and slides between workspaces are this'.
  if (!slider_ && parent()) {
    slider_ = std::make_unique<WorkspaceSlider>(browser_, parent());
  }
}

void WorkspaceSwitcher::RemovedFromWidget() {
  slider_.reset();
}

void WorkspaceSwitcher::Layout(PassKey) {
  if (compact_ || !downloads_ || children().size() < 2) {
    LayoutSuperclass<views::View>(this);
    return;
  }
  const gfx::Rect area = GetContentsBounds();
  const int count = static_cast<int>(children().size());
  const int size = std::clamp((area.width() - (count - 1) * kSpacing / 2) / count,
                              kMinButtonSize, kButtonSize);
  const int y = area.y() + (area.height() - size) / 2;
  // The downloads button at the start; the workspaces in the middle, or as near
  // as they fit past it.
  downloads_->SetBounds(area.x(), y, size, size);
  const int start = area.x() + size + kSpacing;
  const int buttons = count - 1;
  const int spacing =
      buttons > 1 ? std::clamp(
                        (area.right() - start - buttons * size) / (buttons - 1),
                        0, kMaxSpacing)
                  : 0;
  const int width = buttons * size + (buttons - 1) * spacing;
  int x = std::max(start, area.x() + (area.width() - width) / 2);
  for (views::View* child : children()) {
    if (child == downloads_) {
      continue;
    }
    child->SetBounds(x, y, size, size);
    x += size + spacing;
  }
}

void WorkspaceSwitcher::SetCompact(bool compact) {
  if (compact_ == compact) {
    return;
  }
  compact_ = compact;
  static_cast<views::BoxLayout*>(GetLayoutManager())
      ->SetOrientation(compact ? views::BoxLayout::Orientation::kVertical
                               : views::BoxLayout::Orientation::kHorizontal);
  PreferredSizeChanged();
}

void WorkspaceSwitcher::OnTabGroupFocusChanged(
    std::optional<tab_groups::TabGroupId> new_focused_group_id,
    std::optional<tab_groups::TabGroupId> old_focused_group_id) {
  Update();
}

void WorkspaceSwitcher::Update() {
  if (update_pending_) {
    return;
  }
  update_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WorkspaceSwitcher::Rebuild,
                                weak_factory_.GetWeakPtr()));
}

void WorkspaceSwitcher::Rebuild() {
  update_pending_ = false;
  const std::optional<Workspace> shown = CurrentWorkspace(browser_);
  // The window takes its colors from the workspace shown, as in Arc and Zen (a
  // gray one gives colors without a hue; see BrowserWidget), unless it has a
  // theme from an extension.
  const ThemeService* theme =
      ThemeServiceFactory::GetForProfile(browser_->GetProfile());
  if (views::Widget* widget = GetWidget();
      widget && !browser_->GetProfile()->IsOffTheRecord() &&
      !(theme && theme->UsingExtensionTheme())) {
    // Gray, without a hue, for a workspace that keeps the window's colors.
    widget->GetTopLevelWidget()->SetUserColorOverride(
        shown ? std::optional<SkColor>(PaletteSeed(
                    shown->tints_window
                        ? shown->color
                        : static_cast<int>(tab_groups::TabGroupColorId::kGrey)))
              : std::nullopt);
  }
  // All but the downloads button, which stays.
  for (views::View* child :
       std::vector<views::View*>(children().begin(), children().end())) {
    if (child != downloads_) {
      RemoveChildViewT(child);
    }
  }
  for (const Workspace& workspace : GetWorkspaces(browser_->GetProfile())) {
    AddChildView(std::make_unique<WorkspaceButton>(
        base::BindRepeating(&SwitchTo, browser_, workspace.id),
        base::BindRepeating(&WorkspaceSwitcher::ShowMenu,
                            weak_factory_.GetWeakPtr(), workspace.id),
        workspace, shown && shown->id == workspace.id));
  }
  auto* add = AddChildView(std::make_unique<views::ImageButton>(
      base::BindRepeating(&AddAndSwitch, browser_)));
  add->SetImageModel(views::Button::STATE_NORMAL,
                     ui::ImageModel::FromVectorIcon(
                         kAddIcon,
                         kColorTabForegroundInactiveFrameActive, 16));
  add->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  add->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  add->SetPreferredSize(gfx::Size(kButtonSize, kButtonSize));
  add->SetTooltipText(L10n("workspaces.add"));
  PreferredSizeChanged();
  SchedulePaint();
}

void WorkspaceSwitcher::ShowMenu(const std::string& id, views::View* anchor) {
  menu_ = std::make_unique<WorkspaceMenu>(browser_, id);
  menu_->Run(anchor);
}

namespace {

class MoveToWorkspaceMenu : public ui::SimpleMenuModel,
                            public ui::SimpleMenuModel::Delegate {
 public:
  MoveToWorkspaceMenu(BrowserWindowInterface* browser,
                      const std::vector<tabs::TabInterface*>& tabs)
      : ui::SimpleMenuModel(this), browser_(browser) {
    for (tabs::TabInterface* tab : tabs) {
      tabs_.push_back(tab->GetWeakPtr());
    }
    const std::optional<Workspace> shown = CurrentWorkspace(browser);
    for (const Workspace& workspace : GetWorkspaces(browser->GetProfile())) {
      const std::u16string name = base::UTF8ToUTF16(workspace.name);
      AddCheckItem(kFirst + ids_.size(),
                   workspace.icon.empty()
                       ? name
                       : base::StrCat({base::UTF8ToUTF16(workspace.icon), u"  ",
                                       name}));
      ids_.push_back(workspace.id);
      if (shown && shown->id == workspace.id) {
        shown_ = workspace.id;
      }
    }
    AddSeparator(ui::NORMAL_SEPARATOR);
    AddItem(kNew, L10n("workspaces.add"));
  }

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override {
    const size_t i = command_id - kFirst;
    return i < ids_.size() && ids_[i] == shown_;
  }

  void ExecuteCommand(int command_id, int event_flags) override {
    std::string id;
    if (command_id == kNew) {
      id = AddWorkspace(browser_->GetProfile()).id;
    } else if (const size_t i = command_id - kFirst; i < ids_.size()) {
      id = ids_[i];
    }
    if (id.empty()) {
      return;
    }
    for (const base::WeakPtr<tabs::TabInterface>& tab : tabs_) {
      if (tab) {
        MoveTab(browser_, tab.get(), id);
      }
    }
  }

 private:
  static constexpr int kNew = 1;
  static constexpr int kFirst = 10;

  raw_ptr<BrowserWindowInterface> browser_;
  std::vector<base::WeakPtr<tabs::TabInterface>> tabs_;
  std::vector<std::string> ids_;
  std::string shown_;
};

}  // namespace

std::unique_ptr<ui::SimpleMenuModel> CreateMoveToWorkspaceMenu(
    BrowserWindowInterface* browser,
    const std::vector<tabs::TabInterface*>& tabs) {
  return std::make_unique<MoveToWorkspaceMenu>(browser, tabs);
}

ui::ImageModel WorkspaceIcon(BrowserWindowInterface* browser,
                             const std::u16string& group_title,
                             int size) {
  const std::string name = base::UTF16ToUTF8(group_title);
  for (const Workspace& workspace : GetWorkspaces(browser->GetProfile())) {
    if (workspace.name == name) {
      if (ui::ImageModel icon =
              icons::Image(workspace.icon, DotColor(workspace.color), size);
          !icon.IsEmpty()) {
        return icon;
      }
      return ui::ImageModel::FromImageSkia(
          gfx::CanvasImageSource::MakeImageSkia<IconSource>(
              base::UTF8ToUTF16(workspace.icon), DotColor(workspace.color),
              size));
    }
  }
  return ui::ImageModel();
}

void ShowWorkspaceIconPicker(BrowserWindowInterface* browser,
                             const std::string& id,
                             views::View* anchor) {
  const std::optional<Workspace> workspace =
      FindWorkspace(browser->GetProfile(), id);
  if (!workspace || !anchor || !anchor->GetWidget()) {
    return;
  }
  auto save = [](BrowserWindowInterface* browser, std::string id, bool icon,
                 const std::string& value) {
    std::optional<Workspace> workspace =
        FindWorkspace(browser->GetProfile(), id);
    if (!workspace) {
      return;
    }
    if (icon) {
      workspace->icon = value;
    } else if (int color = 0; base::StringToInt(value, &color)) {
      workspace->color = color;
    }
    UpdateWorkspace(browser->GetProfile(), *workspace);
  };
  icons::ShowPicker(anchor, workspace->icon, PickerColors(),
                    base::NumberToString(workspace->color),
                    base::BindRepeating(save, browser, id, /*icon=*/true),
                    base::BindRepeating(save, browser, id, /*icon=*/false));
}

void ShowWorkspaceMenu(BrowserWindowInterface* browser,
                       const std::u16string& group_title,
                       views::View* anchor) {
  static base::NoDestructor<std::unique_ptr<WorkspaceMenu>> menu;
  const std::string name = base::UTF16ToUTF8(group_title);
  for (const Workspace& workspace : GetWorkspaces(browser->GetProfile())) {
    if (workspace.name == name) {
      *menu = std::make_unique<WorkspaceMenu>(browser, workspace.id);
      (*menu)->Run(anchor);
      return;
    }
  }
}

BEGIN_METADATA(WorkspaceSwitcher)
END_METADATA

}  // namespace oblada::workspaces
