#ifndef OBLADA_BROWSER_WORKSPACES_WORKSPACE_SLIDER_H_
#define OBLADA_BROWSER_WORKSPACES_WORKSPACE_SLIDER_H_

#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "ui/events/event_handler.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/rect.h"

class BrowserWindowInterface;

namespace ui {
class Event;
}  // namespace ui

namespace views {
class View;
}  // namespace views

namespace oblada::workspaces {

class WorkspacePreview;

// Moves between workspaces the way Zen does: a two-finger swipe on the sidebar
// drags the workspace's tabs 1:1, the next (or previous) workspace's coming in
// beside them; let go, they snap to one or the other, carrying on at the fingers'
// speed. Switching some other way (a workspace's button, the keyboard) slides them
// the same way, as Zen's spring does. The snapping runs on the compositor, frames
// unaffected by what the browser is doing meanwhile.
class WorkspaceSlider : public ui::EventHandler {
 public:
  WorkspaceSlider(BrowserWindowInterface* browser, views::View* sidebar);
  WorkspaceSlider(const WorkspaceSlider&) = delete;
  WorkspaceSlider& operator=(const WorkspaceSlider&) = delete;
  ~WorkspaceSlider() override;

  // Slides to workspace `id`, then shows it (the window's SwitchAnimator).
  void SlideTo(const std::string& id, bool forward);

  // ui::EventHandler:
  void OnScrollEvent(ui::ScrollEvent* event) override;

 private:
  enum class Axis { kNone, kHorizontal, kVertical };

  // How the tabs snap: along `tween`, over `duration`.
  struct Snap {
    gfx::Tween::Type tween = gfx::Tween::LINEAR;
    base::TimeDelta duration;
  };

  // Gives what moves its layer beforehand, not to hitch at a swipe's start.
  void Prepare();
  // Gets the tabs ready to move: false if there's nothing to move.
  bool Begin();
  // Follows the fingers, `distance` from where they started.
  void Track(float distance);
  // Puts the tabs `offset` from their place, the workspace coming in beside them.
  void Place(float offset);
  void ShowPreview(std::optional<std::string> id, bool forward);
  // The fingers are off: the tabs snap to a workspace.
  void Release();
  // Animates the tabs to `offset`, then shows workspace `target_` (if `commit`)
  // or stays.
  void Settle(float offset, bool commit, Snap snap);
  void OnSettled();
  void Finish();
  // After a switch, the preview stays over the tabs (now the workspace's) until
  // they've settled into their places, then fades.
  void HandOff();
  void CheckHandOff();
  void FadePreview();
  void RemoveFaded();
  // Takes the preview away at once, e.g. as another swipe starts.
  void EndHandOff();
  void Reset();

  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<views::View> sidebar_;

  // During a slide: what moves (the workspace's tabs, and "New tab" below them).
  std::vector<raw_ptr<views::View>> moving_;
  raw_ptr<views::View> tabs_ = nullptr;
  raw_ptr<WorkspacePreview> preview_ = nullptr;
  gfx::Rect tabs_bounds_;  // in the sidebar
  std::optional<std::string> target_;
  bool forward_ = true;
  float offset_ = 0;
  bool masked_before_ = false;

  // While snapping.
  bool settling_ = false;
  bool commit_ = false;
  // Finishes a slide whose frames don't come (e.g. without a display).
  base::OneShotTimer finish_timer_;

  // After a switch, until the tabs under the preview stop moving.
  base::RepeatingTimer hand_off_timer_;
  base::TimeTicks hand_off_start_;
  std::string tabs_signature_;
  int stable_checks_ = 0;
  raw_ptr<WorkspacePreview> fading_ = nullptr;

  // During a swipe.
  Axis axis_ = Axis::kNone;
  float x_ = 0;
  float y_ = 0;
  float velocity_ = 0;  // DIPs per millisecond
  base::TimeTicks last_move_time_;
  bool swiping_ = false;
  raw_ptr<const ui::Event> last_event_ = nullptr;
  base::TimeTicks last_event_time_;
  // Lets go of a swipe whose end doesn't come.
  base::OneShotTimer release_timer_;

  base::WeakPtrFactory<WorkspaceSlider> weak_factory_{this};
};

}  // namespace oblada::workspaces

#endif  // OBLADA_BROWSER_WORKSPACES_WORKSPACE_SLIDER_H_
