#ifndef OBLADA_BROWSER_CONTAINERS_CONTAINER_CHIP_H_
#define OBLADA_BROWSER_CONTAINERS_CONTAINER_CHIP_H_

// The container of the current tab at the end of the address bar, as in Firefox: its
// icon (a temporary one's number), in its color, with its name as the tooltip. Clicking it offers to always open the site in this container,
// or to reopen the tab in another one.
//
// The location bar (location_bar_view.cc) creates it, lays it out with its other
// trailing decorations and updates it with the current tab.

class BrowserWindowInterface;

namespace content {
class WebContents;
}  // namespace content

namespace views {
class View;
}  // namespace views

namespace oblada::containers {

void AddContainerChip(views::View* location_bar, BrowserWindowInterface* browser);

// The chip in `location_bar`; null if it has none.
views::View* ContainerChipOf(views::View* location_bar);

void UpdateContainerChip(views::View* location_bar, content::WebContents* contents);

}  // namespace oblada::containers

#endif  // OBLADA_BROWSER_CONTAINERS_CONTAINER_CHIP_H_
