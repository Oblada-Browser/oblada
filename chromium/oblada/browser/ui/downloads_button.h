#ifndef OBLADA_BROWSER_UI_DOWNLOADS_BUTTON_H_
#define OBLADA_BROWSER_UI_DOWNLOADS_BUTTON_H_

#include <memory>

#include "ui/views/bubble/bubble_border.h"

class BrowserView;
class BrowserWindowInterface;
class ToolbarButton;

namespace views {
class View;
}  // namespace views

// Zen's downloads button, at the bottom of the sidebar, before the workspaces (see
// workspace_switcher.h). It is Chromium's toolbar button (the kActionShowDownloads
// action) moved there: the same icon, progress ring and badge, and the same bubble
// of recent downloads (download_toolbar_ui_controller.cc), which opens above it.
// Unlike the toolbar's, it is always there; the toolbar doesn't show its own.
namespace oblada::downloads {

std::unique_ptr<views::View> CreateButton(BrowserWindowInterface* browser);

// The button in `browser_view`'s sidebar; null if it has none.
ToolbarButton* SidebarButton(BrowserView* browser_view);

// The downloads bubble's arrow when it opens from `anchor`: `arrow`, unless that is
// the sidebar's button, at the bottom of the window, which it opens above.
views::BubbleBorder::Arrow BubbleArrow(const views::View* anchor,
                                       views::BubbleBorder::Arrow arrow);

}  // namespace oblada::downloads

#endif  // OBLADA_BROWSER_UI_DOWNLOADS_BUTTON_H_
