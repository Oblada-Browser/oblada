#ifndef OBLADA_BROWSER_UI_SIDEBAR_CARD_H_
#define OBLADA_BROWSER_UI_SIDEBAR_CARD_H_

#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/vector2d.h"

namespace gfx {
class Canvas;
}  // namespace gfx

namespace views {
class View;
}  // namespace views

// The compact sidebar (sidebar_mode.h) is a card that widens over the page from
// the window's left edge as it expands on hover: as wide as the full sidebar,
// with margins around it as around the page, the toolbar at its top and the
// window controls in its corner. It has the window's look, its workspace's
// theme (window_background.h), opaque, on a gray standing for the glass, which
// can't be over the page.
//
// The card is drawn by the view Chromium puts behind an expanded tab strip to
// blur what's under it (VerticalTabStripBackgroundBlurBackdrop): the browser
// layout (browser_view_tabbed_layout_impl.cc) places it and sets it up with these
// functions, and leaves the tab strip and the toolbar over it see-through,
// clipped to it.
namespace oblada::sidebar {

enum class Card {
  kNone,
  kPanel,
};

// Space between the card and the window's edges, as between the page and them.
int CardMargin();

// Has `view` draw `card` (none: what it draws otherwise), with the window's look
// if `glass`, else its frame color.
void SetCard(views::View* view, Card card, bool glass);

// Clips `view` to the panel that `card_view` (its sibling) draws, its corners
// rounded as the panel's; without a panel, unclips it.
void ClipToCard(views::View* view, const views::View* card_view);

// For the view's OnPaint(): draws its card, if it has one (else false).
bool PaintCard(gfx::Canvas* canvas, const views::View* view);

// Where the window controls are in the panel: moved from the window's corner by
// the card's margin, as far from the panel's corner as they are from the window's
// in the full sidebar.
gfx::Vector2d WindowControlsOffset();

}  // namespace oblada::sidebar

#endif  // OBLADA_BROWSER_UI_SIDEBAR_CARD_H_
