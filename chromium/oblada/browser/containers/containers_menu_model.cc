#include "oblada/browser/containers/containers_menu_model.h"

#include "base/functional/bind.h"
#include "base/memory/ptr_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "content/public/browser/web_contents.h"
#include "oblada/browser/containers/containers.h"
#include "oblada/browser/containers/containers_ui.h"
#include "oblada/browser/l10n.h"
#include "oblada/browser/ui/icons.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/image/canvas_image_source.h"

namespace oblada::containers {

namespace {

// Command ids inside the submenu (the range of containers.h's submenu ids).
constexpr int kTemporaryId = 55609;
constexpr int kManageId = 55610;
constexpr int kFirstItemId = 55611;
constexpr int kMaxItems = 80;

// The container's color, as the dot shown next to its name.
class DotSource : public gfx::CanvasImageSource {
 public:
  explicit DotSource(SkColor color)
      : gfx::CanvasImageSource(gfx::Size(ui::SimpleMenuModel::kDefaultIconSize, ui::SimpleMenuModel::kDefaultIconSize)),
        color_(color) {}

  void Draw(gfx::Canvas* canvas) override {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(color_);
    canvas->DrawCircle(gfx::PointF(ui::SimpleMenuModel::kDefaultIconSize / 2.f, ui::SimpleMenuModel::kDefaultIconSize / 2.f),
                       ui::SimpleMenuModel::kDefaultIconSize / 2.f - 2, flags);
  }

 private:
  const SkColor color_;
};

}  // namespace

// static
std::unique_ptr<ContainersMenuModel> ContainersMenuModel::ForNewTab(
    BrowserWindowInterface* browser) {
  return base::WrapUnique(new ContainersMenuModel(
      browser, GURL(), WindowOpenDisposition::NEW_FOREGROUND_TAB,
      /*offer_no_container=*/false, std::string()));
}

// static
std::unique_ptr<ContainersMenuModel> ContainersMenuModel::ForLink(
    BrowserWindowInterface* browser,
    const GURL& url,
    const std::string& source_container) {
  // From a container tab, also offer to leave containers (as Firefox does).
  return base::WrapUnique(new ContainersMenuModel(
      browser, url, WindowOpenDisposition::NEW_BACKGROUND_TAB,
      /*offer_no_container=*/!source_container.empty(), std::string()));
}

// static
std::unique_ptr<ContainersMenuModel> ContainersMenuModel::ForReopen(
    BrowserWindowInterface* browser,
    content::WebContents* contents) {
  const std::string current = GetContainerId(contents);
  auto model = base::WrapUnique(new ContainersMenuModel(
      browser, contents->GetLastCommittedURL(),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      /*offer_no_container=*/!current.empty(), current));
  model->reopen_ = contents->GetWeakPtr();
  return model;
}

ContainersMenuModel::ContainersMenuModel(BrowserWindowInterface* browser,
                                         const GURL& url,
                                         WindowOpenDisposition disposition,
                                         bool offer_no_container,
                                         const std::string& current_container)
    : ui::SimpleMenuModel(this),
      browser_(browser),
      url_(url),
      disposition_(disposition) {
  if (offer_no_container) {
    AddItem(kFirstItemId, L10n("containers.none"));
    ids_.emplace_back();
  }
  for (const Container& container : GetContainers(browser->GetProfile())) {
    if (ids_.size() == kMaxItems) {
      break;
    }
    if (container.id == current_container) {
      continue;
    }
    // Its icon in its color, else a dot of it.
    ui::ImageModel icon =
        icons::Image(container.icon, ColorFor(container.color),
                     ui::SimpleMenuModel::kDefaultIconSize);
    if (icon.IsEmpty()) {
      icon = ui::ImageModel::FromImageSkia(
          gfx::CanvasImageSource::MakeImageSkia<DotSource>(
              ColorFor(container.color)));
    }
    AddItemWithIcon(kFirstItemId + static_cast<int>(ids_.size()),
                    base::UTF8ToUTF16(container.name), std::move(icon));
    ids_.push_back(container.id);
  }
  AddSeparator(ui::NORMAL_SEPARATOR);
  AddItem(kTemporaryId, L10n("containers.new_temporary"));
  AddItem(kManageId, L10n("containers.manage"));
}

void ContainersMenuModel::Open(const std::string& id) {
  if (reopen_) {
    // Once the menu is done: closing the tab may delete this menu.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::WeakPtr<content::WebContents> contents,
                          std::string id, GURL url) {
                         if (contents) {
                           ReopenInContainer(contents.get(), id, url,
                                             /*keep_original=*/false);
                         }
                       },
                       reopen_, id, url_));
    return;
  }
  OpenInContainer(browser_, id, url_, disposition_);
}

ContainersMenuModel::~ContainersMenuModel() = default;

void ContainersMenuModel::ExecuteCommand(int command_id, int event_flags) {
  if (command_id == kManageId) {
    NavigateParams params(browser_, GURL(kContainersURL),
                          ui::PAGE_TRANSITION_AUTO_BOOKMARK);
    params.disposition = WindowOpenDisposition::SINGLETON_TAB;
    Navigate(&params);
    return;
  }
  if (command_id == kTemporaryId) {
    Open(AddTemporaryContainer(browser_->GetProfile()).id);
    return;
  }
  const size_t index = command_id - kFirstItemId;
  if (command_id < kFirstItemId || index >= ids_.size()) {
    return;
  }
  Open(ids_[index]);
}

}  // namespace oblada::containers
