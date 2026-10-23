#ifndef OBLADA_BROWSER_UI_SIDEBAR_LAYOUT_H_
#define OBLADA_BROWSER_UI_SIDEBAR_LAYOUT_H_

#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"

namespace views {
class View;
}  // namespace views

// Zen-style layout: with vertical tabs, everything lives in the sidebar. The toolbar
// moves from above the page to the top of the tab strip; its buttons share a row with
// the window controls and the address bar gets a full-width row below them. The
// sidebar is all one color, without lines between its parts, and the page floats
// next to it with rounded corners.
//
// Collapsed, the window is in scaffold or compact mode (sidebar_mode.h): the tabs'
// icons in a column, the toolbar a row at the top of the window; or only the page,
// the sidebar coming back over it as the tab strip expands on hover (or while the
// address bar has focus), a card widening from the window's edge (sidebar_card.h).
//
// The browser layout (browser_view_tabbed_layout_impl.cc) decides when the toolbar is
// in the sidebar and tells the toolbar (toolbar_view.cc) through these functions.
namespace oblada::sidebar {

// In the sidebar, the toolbar's buttons (back, forward, the menu) have this much room
// around their icons: as in Zen, they fit next to the window controls in a sidebar
// 170 wide.
inline constexpr int kButtonInset = 4;

// Puts `toolbar` in the sidebar, its buttons starting `leading_inset` from the leading
// edge (past the window controls); a negative inset takes it out. `compact`: the
// sidebar is compact (see above).
void SetToolbarInSidebar(views::View* toolbar, int leading_inset, bool compact);

// The toolbar's leading inset in the sidebar, or -1 when it is laid out as usual.
int ToolbarInset(const views::View* toolbar);

// In scaffold mode (sidebar_mode.h) the toolbar is a row at the top of the
// window, as Chromium's, without the profile and extensions buttons, which are
// in the app menu and the address bar's site panel (site_panel.h).
void SetScaffoldToolbar(views::View* toolbar, bool scaffold);
bool IsScaffoldToolbar(const views::View* toolbar);

// Whether the sidebar is compact (see above).
bool IsCompact(const views::View* toolbar);

// Whether `toolbar`'s address bar is the sidebar's kind: a veil, quiet (the
// site it shows) until hovered, floating over the page while it has the focus
// (floating_location_bar.h). In the sidebar, and over the active tab in
// horizon compact mode (horizontal_tabs.h).
bool HasSidebarLocationBar(const views::View* toolbar);

// Whether the sidebar is compact, and its toolbar is in use: hovered, or holding the
// focus (e.g. typing in the address bar). The tab strip stays expanded meanwhile.
bool IsCompactToolbarInUse(const views::View* toolbar);

// For compact mode: keeps the toolbar's container `top_container` above the page
// `page` (its sibling) when `above`, so the toolbar can slide in over it, and tells
// `tab_strip` when the mouse leaves the toolbar (as if it left the tab strip).
// `clip`: nothing of it shows outside of its bounds (e.g. the address bar, which
// has a layer of its own, when the container is hidden), unless the address bar
// floats out of it. Above everything else too, but `tabs` (another sibling, e.g.
// horizontal tabs in the toolbar's row), which keep their events.
void SetToolbarAbovePage(views::View* top_container,
                         views::View* page,
                         views::View* tab_strip,
                         bool above,
                         bool clip,
                         views::View* tabs = nullptr);

// How wide the tab strip looks, from 0 (narrow, in scaffold mode) to 1 (the full
// sidebar's). The browser layout sets it on the tab strip's region view, whose row
// of workspaces follows it (workspace_switcher.h).
void SetExpansion(views::View* tab_strip_region, float expansion);
float Expansion(const views::View* tab_strip_region);

// Hides `view` (a toolbar button) while the toolbar is in the sidebar, and shows it
// again after.
void HideInSidebar(views::View* view, bool in_sidebar);

// Height of the address bar's row, below the buttons.
int LocationRowHeight(const views::View* location_bar);

// Height of the row of buttons (and window controls), from the toolbar's height.
int ButtonRowHeight(int toolbar_height, const views::View* location_bar);

// Where the address bar goes in a toolbar of `toolbar_size`.
gfx::Rect LocationBarBounds(const gfx::Size& toolbar_size,
                            const views::View* location_bar);

// Once the toolbar has laid its buttons out at the end of their row, moves those
// before the address bar (back, forward, reload) to its start, next to the window
// controls; the others (extensions, menu) stay at the end.
void ArrangeButtonRow(views::View* toolbar, const views::View* location_bar);

// Space between the page and the window's edges, and the page's corner radius.
gfx::Insets ContentInsets();
float ContentCornerRadius();

// The width of the suggestions under the address bar at `location_bar_in_screen`:
// from the sidebar's narrow one, they open wider, over the page (as far as the
// screen allows); under a floating one (see floating_location_bar.h), they are as
// wide as it.
int SuggestionsWidth(const gfx::Rect& location_bar_in_screen);

// The address bar's corner radius in the sidebar: a rounded rectangle, as Arc's,
// rather than a pill.
inline constexpr int kLocationBarCornerRadius = 10;

}  // namespace oblada::sidebar

#endif  // OBLADA_BROWSER_UI_SIDEBAR_LAYOUT_H_
