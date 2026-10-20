#ifndef OBLADA_BROWSER_UI_DRAG_GHOST_H_
#define OBLADA_BROWSER_UI_DRAG_GHOST_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/native_ui_types.h"

namespace content {
class WebContents;
}  // namespace content

namespace views {
class ImageView;
class Label;
class Widget;
}  // namespace views

namespace oblada {

// A translucent copy of a tab, its icon and title, following the pointer while
// the tab is dragged over the page (or a split pane by its grip), as in Zen.
class DragGhost {
 public:
  // Over `parent`, the window the tab is dragged in.
  explicit DragGhost(views::Widget* parent);
  DragGhost(const DragGhost&) = delete;
  DragGhost& operator=(const DragGhost&) = delete;
  ~DragGhost();

  // Shows `contents`' tab under the pointer, at `point_in_screen`, held where
  // `grab` says (fractions of its width and height).
  void Show(content::WebContents* contents,
            const gfx::Point& point_in_screen,
            const gfx::PointF& grab = gfx::PointF(0.15f, 0.5f));
  void Hide();

  // Its window, which isn't one to drop tabs on.
  gfx::NativeWindow GetNativeWindow() const;

 private:
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<views::ImageView> icon_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
};

}  // namespace oblada

#endif  // OBLADA_BROWSER_UI_DRAG_GHOST_H_
