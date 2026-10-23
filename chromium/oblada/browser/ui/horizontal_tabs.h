#ifndef OBLADA_BROWSER_UI_HORIZONTAL_TABS_H_
#define OBLADA_BROWSER_UI_HORIZONTAL_TABS_H_

#include <optional>

#include "oblada/browser/ui/sidebar_mode.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

class BrowserWindowInterface;

namespace views {
class View;
}  // namespace views

// The window layouts with horizontal tabs (sidebar_mode.h): Chromium's
// horizontal tab strip, made as plain as the sidebar is. Its tabs are veils
// over the window's background, as the sidebar's are, without Chrome's curved
// shapes or the lines between them; the page floats below them, with the
// margins and rounded corners it has next to the sidebar.
// - Horizon: the toolbar is a row at the top of the window, next to the window
//   controls, its address bar in the middle, and the tabs share a row below it
//   evenly, as Safari's.
// - Horizon compact: one row for both, as Safari's compact layout: the tabs
//   between back and forward and the menu, sharing their room as horizon's
//   share their row, the address bar over the active one, beside its close
//   button: quiet (the site it shows) until hovered, floating over the page
//   while it has the focus, as the sidebar's does (floating_location_bar.h).
// - Classic: the tabs in a row at the top, next to the window controls, the
//   toolbar a row below them.
// The workspace shown is a pill at the start of the tabs (workspace_pill.h).
// The browser layout (browser_view_tabbed_layout_impl.cc) lays them out and
// marks the tab strip's region with the layout, which the tabs follow.
namespace oblada::horizontal {

// Marks `region`, the horizontal tab strip's region, with the window's layout,
// or with none outside of these layouts.
void SetLayout(views::View* region, std::optional<sidebar::Mode> mode);

// The layout marked on `view` or on the nearest of its ancestors (e.g. a tab):
// none outside of these layouts.
std::optional<sidebar::Mode> LayoutOf(const views::View* view);

// Whether `view`'s tabs share their row evenly, however wide, their icons and
// titles in the middle, as Safari's: in horizon mode, compact or not.
bool SharesRow(const views::View* view);

// How wide a tab can grow then, sharing its row with the others.
inline constexpr int kHorizonTabMaxWidth = 1000;

// Space between the rows: between the tabs' veils and the address bar, and
// below them, above the page.
inline constexpr int kRowGap = 6;

// In horizon mode, puts `toolbar`'s address bar in the middle of the window,
// narrower, as Safari's, within the room the toolbar's layout gave it.
void CenterLocationBar(views::View* toolbar, views::View* location_bar);

// In horizon compact mode, the active tab is never narrower than this, for the
// address bar over it.
inline constexpr int kCompactActiveTabMinWidth = 140;

// In horizon compact mode, how narrow the active tab can get, as the tabs of
// `browser` tell it (`tab` one of them): kCompactActiveTabMinWidth, unless the
// other tabs shown wouldn't fit beside it then, at their narrowest
// (`narrowest`, overlapping by `overlap`), which would hide some, maybe the
// active one; down to `fallback`. All of them tell the same, for them to stay
// as wide as each other when there's room (Chromium only repaints them when
// another one becomes active then).
int CompactActiveTabMinWidth(const views::View* tab,
                             BrowserWindowInterface* browser,
                             int narrowest,
                             int overlap,
                             int fallback);

// In horizon compact mode, where in `toolbar` (`toolbar_size` big) the tabs
// go: where its layout would put the address bar, between back and forward
// and the menu.
gfx::Rect CompactTabsRoom(const views::View* toolbar,
                          const gfx::Size& toolbar_size);

// In horizon compact mode, puts `toolbar`'s address bar over the active tab's
// veil (the `active_index`th tab), short of its close button, following it as
// it moves; floating, its place stays there (see sidebar::LayOutLocationBar()).
// Where it is, it gets the events, rather than the tab under it.
void LayOutCompactLocationBar(views::View* toolbar,
                              views::View* location_bar,
                              int active_index);

}  // namespace oblada::horizontal

#endif  // OBLADA_BROWSER_UI_HORIZONTAL_TABS_H_
