#ifndef OBLADA_BROWSER_BARRIER_BARRIER_UI_H_
#define OBLADA_BROWSER_BARRIER_BARRIER_UI_H_

#include <memory>

#include "base/functional/callback_forward.h"

class BrowserWindowInterface;

namespace views {
class View;
}  // namespace views

// The Barrier's controls for the site the tab shows (barrier.h): whether it's
// up, what it blocked, and the site's settings. A button at the end of the
// address bar, its number the items blocked, opens them as a panel; where the
// address bar is in the sidebar (no room for another button), they're at the
// top of the site panel instead (site_panel.h).
//
// Changing a setting reloads the page, for it to take effect.
namespace oblada::barrier {

// location_bar_view.cc: adds the button to `location_bar`.
void AddBarrierButton(views::View* location_bar,
                      BrowserWindowInterface* browser);
// The button of `location_bar`, shown or not; null if it has none.
views::View* BarrierButtonOf(views::View* location_bar);
// Updates the button as `location_bar` lays out (the tab may have changed):
// whether it shows (on web pages, but in the sidebar), and its number.
void UpdateBarrierButton(views::View* location_bar);

// The controls, for the site panel; `close` closes it (before the controls
// open Settings, or the element picker).
std::unique_ptr<views::View> CreateBarrierSection(
    BrowserWindowInterface* browser,
    base::RepeatingClosure close);

}  // namespace oblada::barrier

#endif  // OBLADA_BROWSER_BARRIER_BARRIER_UI_H_
