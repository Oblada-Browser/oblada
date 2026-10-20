#ifndef OBLADA_BROWSER_UI_SITE_PANEL_H_
#define OBLADA_BROWSER_UI_SITE_PANEL_H_

// Zen's site panel: a button at the end of the address bar opens quick actions for
// the page (bookmark, screenshot, reading mode, copy link), the extensions (as a grid
// of their icons, with their badges; their popups show from the button) and the
// page's security.
//
// The location bar (location_bar_view.cc) creates the button and lays it out with its
// other trailing decorations; with the toolbar in the sidebar, it is where extension
// popups show from (extensions_toolbar_desktop.cc), the toolbar keeping its
// extension buttons out of sight.

class BrowserWindowInterface;

namespace views {
class View;
}  // namespace views

namespace oblada::site_panel {

void AddSitePanelButton(views::View* location_bar, BrowserWindowInterface* browser);

// The button in `location_bar`; null if it has none.
views::View* SitePanelButtonOf(views::View* location_bar);

// What extension popups should show from, for the extensions container of
// `toolbar` (the button, while the toolbar is in the sidebar); null for the default.
views::View* ExtensionsAnchor(views::View* toolbar);

}  // namespace oblada::site_panel

#endif  // OBLADA_BROWSER_UI_SITE_PANEL_H_
