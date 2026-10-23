#ifndef OBLADA_BROWSER_WORKSPACES_WORKSPACE_PILL_H_
#define OBLADA_BROWSER_WORKSPACES_WORKSPACE_PILL_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "oblada/browser/workspaces/workspaces.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/events/event_handler.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/button.h"

class BrowserWindowInterface;

namespace views {
class MenuRunner;
}  // namespace views

namespace oblada::workspaces {

// The workspace a window shows, at the start of its horizontal tabs
// (oblada/browser/ui/horizontal_tabs.h), as the workspaces are at the bottom
// of the sidebar: its icon and name on a veil, and a chevron; in horizon
// compact mode, where room is short, its icon only. A click lists the
// workspaces, to show another or a new one; a right click opens its menu
// (rename, icon, theme, container, delete), as its button in the sidebar
// does. A two-finger swipe across the tabs shows the next or previous one.
class WorkspacePill : public views::Button,
                      public views::ContextMenuController,
                      public ui::SimpleMenuModel::Delegate,
                      public TabStripModelObserver {
  METADATA_HEADER(WorkspacePill, views::Button)

 public:
  explicit WorkspacePill(BrowserWindowInterface* browser);
  WorkspacePill(const WorkspacePill&) = delete;
  WorkspacePill& operator=(const WorkspacePill&) = delete;
  ~WorkspacePill() override;

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void PaintButtonContents(gfx::Canvas* canvas) override;
  void AddedToWidget() override;
  void RemovedFromWidget() override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

  // TabStripModelObserver:
  void OnTabGroupFocusChanged(
      std::optional<tab_groups::TabGroupId> new_focused_group_id,
      std::optional<tab_groups::TabGroupId> old_focused_group_id) override;

 private:
  // Hears the swipes across the tab strip's region.
  class Swipes : public ui::EventHandler {
   public:
    explicit Swipes(WorkspacePill* pill) : pill_(pill) {}
    // ui::EventHandler:
    void OnScrollEvent(ui::ScrollEvent* event) override;

   private:
    raw_ptr<WorkspacePill> pill_;
  };

  // A scroll across the tab strip's region: part of a swipe, maybe.
  void OnSwipe(ui::ScrollEvent* event);
  void Update();
  // The list of workspaces, below the pill.
  void ShowList();
  // Whether only the icon shows (horizon compact mode).
  bool IsIconOnly() const;

  raw_ptr<BrowserWindowInterface> browser_;
  std::optional<Workspace> shown_;
  base::CallbackListSubscription subscription_;
  std::unique_ptr<ui::SimpleMenuModel> list_;
  std::vector<std::string> list_ids_;
  std::unique_ptr<views::MenuRunner> runner_;
  // Where the swipes come from: the tab strip's region.
  raw_ptr<views::View> swipe_source_ = nullptr;
  Swipes swipes_{this};
  // How far the fingers went across the tabs in the swipe going on, if any.
  std::optional<float> swipe_;
};

}  // namespace oblada::workspaces

#endif  // OBLADA_BROWSER_WORKSPACES_WORKSPACE_PILL_H_
