#ifndef OBLADA_BROWSER_WORKSPACES_WORKSPACE_SWITCHER_H_
#define OBLADA_BROWSER_WORKSPACES_WORKSPACE_SWITCHER_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/views/view.h"

class BrowserWindowInterface;

namespace tabs {
class TabInterface;
}  // namespace tabs

namespace oblada::workspaces {

// The tab menu's "Move to workspace" item.
inline constexpr int kMoveToWorkspaceMenuId = 55640;

// Its submenu: the workspaces to move `tabs` to, and a new one.
std::unique_ptr<ui::SimpleMenuModel> CreateMoveToWorkspaceMenu(
    BrowserWindowInterface* browser,
    const std::vector<tabs::TabInterface*>& tabs);

class WorkspaceMenu;
class WorkspaceSlider;

// The workspaces, at the bottom of the sidebar as in Zen: a button each (its icon,
// else a dot of its color; the one shown stands out) and "+" to add one. A
// workspace's menu (right click) renames it, changes its icon or the container its
// new tabs open in, or deletes it.
class WorkspaceSwitcher : public views::View, public TabStripModelObserver {
  METADATA_HEADER(WorkspaceSwitcher, views::View)

 public:
  explicit WorkspaceSwitcher(BrowserWindowInterface* browser);
  WorkspaceSwitcher(const WorkspaceSwitcher&) = delete;
  WorkspaceSwitcher& operator=(const WorkspaceSwitcher&) = delete;
  ~WorkspaceSwitcher() override;

  // In the compact sidebar, one above the other.
  void SetCompact(bool compact);

  // views::View:
  void Layout(PassKey) override;
  void AddedToWidget() override;
  void RemovedFromWidget() override;

  // TabStripModelObserver:
  void OnTabGroupFocusChanged(
      std::optional<tab_groups::TabGroupId> new_focused_group_id,
      std::optional<tab_groups::TabGroupId> old_focused_group_id) override;

 private:
  // Soon: not while one of the buttons it replaces is being pressed.
  void Update();
  void Rebuild();
  void ShowMenu(const std::string& id, views::View* anchor);

  raw_ptr<BrowserWindowInterface> browser_;
  bool compact_ = false;
  bool update_pending_ = false;
  std::unique_ptr<WorkspaceMenu> menu_;
  std::unique_ptr<WorkspaceSlider> slider_;
  base::CallbackListSubscription subscription_;
  base::WeakPtrFactory<WorkspaceSwitcher> weak_factory_{this};
};

// A workspace's icon, `size` wide: its emoji, else a dot of its color (that of
// `group_title`, a window's group).
ui::ImageModel WorkspaceIcon(BrowserWindowInterface* browser,
                             const std::u16string& group_title,
                             int size);

// Opens a bubble next to `anchor` to pick workspace `id`'s icon and color.
void ShowWorkspaceIconPicker(BrowserWindowInterface* browser,
                             const std::string& id,
                             views::View* anchor);

// Opens the menu of the workspace that a window's group `group_title` is, e.g.
// from the group's header.
void ShowWorkspaceMenu(BrowserWindowInterface* browser,
                       const std::u16string& group_title,
                       views::View* anchor);

}  // namespace oblada::workspaces

#endif  // OBLADA_BROWSER_WORKSPACES_WORKSPACE_SWITCHER_H_
