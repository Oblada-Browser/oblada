#ifndef OBLADA_BROWSER_UI_FLOATING_LOCATION_BAR_H_
#define OBLADA_BROWSER_UI_FLOATING_LOCATION_BAR_H_

#include <string>

#include "base/functional/callback_forward.h"
#include "ui/gfx/geometry/rect.h"

namespace views {
class View;
}  // namespace views

// While it has the focus, the sidebar's address bar floats over the page, as Arc's
// command bar and Zen's floating address bar: centered in the upper part of the
// page, larger, over the page dimmed, its suggestions right under it.
// Its place in the sidebar stays, grayed out, and the bar's shape flies from there
// to the page as it floats out, and back as it returns, to lead the eye. So does
// the address bar over the active tab in horizon compact mode (horizontal_tabs.h,
// sidebar_layout.h's HasSidebarLocationBar()).
//
// The bar stays its toolbar's child: the toolbar lays it out over the page (see
// LayOutLocationBar()), and its container is above the page meanwhile (see
// oblada/browser/ui/sidebar_layout.h).
namespace oblada::sidebar {

// A floating address bar is this much taller than in the sidebar, its text this
// much larger, its corners this round.
inline constexpr int kFloatingLocationBarExtraHeight = 10;
inline constexpr int kFloatingLocationBarFontDelta = 2;
inline constexpr int kFloatingLocationBarCornerRadius = 12;

// Makes `location_bar` float over `page`, or brings it back to the sidebar.
// `placeholder` is what its grayed-out place in the sidebar shows (the site).
void SetLocationBarFloating(views::View* location_bar,
                            views::View* page,
                            bool floating,
                            const std::u16string& placeholder = std::u16string());
bool IsLocationBarFloating(const views::View* location_bar);

// Whether one of `toolbar`'s children is a floating address bar.
bool HasFloatingLocationBar(const views::View* toolbar);

// Whether a floating address bar is at `bounds_in_screen`.
bool IsFloatingLocationBarAt(const gfx::Rect& bounds_in_screen);

// Whether the floating address bar at `bounds_in_screen` is still on its way over
// the page; and, once it has landed (its shape faded out over it), runs
// `callback`. Its suggestions wait for that: so as not to show around a bar that
// isn't there yet, nor to take frames from its animations (showing them blocks
// the window a moment).
bool IsFloatingLocationBarLanding(const gfx::Rect& bounds_in_screen);
void WhenFloatingLocationBarLands(const gfx::Rect& bounds_in_screen,
                                  base::OnceClosure callback);

// Lays out `toolbar`'s address bar, in the sidebar: in its row (see
// LocationBarBounds()), or floating, with its grayed-out place in the row.
void LayOutLocationBar(views::View* toolbar, views::View* location_bar);

// The same with its place at `slot` (in `toolbar`), e.g. over the active tab in
// horizon compact mode (horizontal_tabs.h), where its shape flies from and
// back to.
void LayOutLocationBar(views::View* toolbar,
                       views::View* location_bar,
                       const gfx::Rect& slot);

}  // namespace oblada::sidebar

#endif  // OBLADA_BROWSER_UI_FLOATING_LOCATION_BAR_H_
