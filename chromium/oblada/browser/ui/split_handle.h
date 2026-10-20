#ifndef OBLADA_BROWSER_UI_SPLIT_HANDLE_H_
#define OBLADA_BROWSER_UI_SPLIT_HANDLE_H_

#include <memory>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/view.h"

class BrowserWindowInterface;

namespace oblada {
class DragGhost;
}  // namespace oblada

namespace views {
class MenuRunner;
class WebView;
}  // namespace views

namespace oblada::split {

// A small grip above a pane of a split view, Zen-like. Dragged onto the other pane
// it swaps them; dragged out of the panes (onto the sidebar) it turns them back into
// whole pages; clicked, it offers that and closing the pane's tab.
class SplitHandle : public views::View, public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(SplitHandle, views::View)

 public:
  // The room the handles take above the panes (see MultiContentsView).
  static constexpr int kBandHeight = 12;
  static constexpr int kWidth = 64;

  // Returns the view showing the handle's pane, within `panes` (swapping panes
  // swaps their views).
  using PaneGetter = base::RepeatingCallback<views::WebView*()>;

  SplitHandle(BrowserWindowInterface* browser,
              views::View* panes,
              PaneGetter pane);
  SplitHandle(const SplitHandle&) = delete;
  SplitHandle& operator=(const SplitHandle&) = delete;
  ~SplitHandle() override;

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;
  ui::Cursor GetCursor(const ui::MouseEvent& event) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseCaptureLost() override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  enum Command { kSwap = 1, kSeparate, kClose };

  void ShowMenu();

  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<views::View> panes_;
  PaneGetter pane_;
  bool hovered_ = false;
  bool dragging_ = false;
  gfx::Point press_point_;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
  // The pane's tab, following the pointer while the grip is dragged.
  std::unique_ptr<DragGhost> ghost_;
};

}  // namespace oblada::split

#endif  // OBLADA_BROWSER_UI_SPLIT_HANDLE_H_
