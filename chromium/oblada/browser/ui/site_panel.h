#ifndef OBLADA_BROWSER_UI_SITE_PANEL_H_
#define OBLADA_BROWSER_UI_SITE_PANEL_H_

// Zen's site panel: a button at the end of the address bar opens quick actions for
// the page (bookmark, screenshot, reading mode, copy link, install as an app), the
// extensions (as a grid of their icons, with their badges; their popups show from
// the button) and the page's security, which opens the site's information. In the
// sidebar, the address bar leaves the bookmark star, the app's install and the site's
// information (but for a site with warnings) to it.
//
// The location bar (location_bar_view.cc) creates the button and lays it out with its
// other trailing decorations; in Oblada's layouts, it is where extension popups show
// from (extensions_toolbar_desktop.cc), the toolbar keeping its extension buttons out
// of sight.

class BrowserWindowInterface;

namespace page_actions {
class PageActionContainerView;
}  // namespace page_actions

namespace views {
class View;
}  // namespace views

namespace oblada::site_panel {

void AddSitePanelButton(views::View* location_bar, BrowserWindowInterface* browser);

// The button in `location_bar`; null if it has none.
views::View* SitePanelButtonOf(views::View* location_bar);

// Opens the panel of `location_bar` (or closes it), as its button does.
void ShowPanel(views::View* location_bar);

// Leaves the page actions that the panel has (the bookmark star, installing the
// site) out of `container`, the page actions of `location_bar`, while it's in the
// sidebar with the panel's button. To call as the location bar lays out.
void HidePageActionsInPanel(views::View* location_bar,
                            page_actions::PageActionContainerView* container);

// What extension popups should show from, for the extensions container of
// `toolbar` (the button, while the toolbar keeps its extension buttons out of
// sight, see toolbar_view.cc); null for the default.
views::View* ExtensionsAnchor(views::View* toolbar);

}  // namespace oblada::site_panel

#endif  // OBLADA_BROWSER_UI_SITE_PANEL_H_
