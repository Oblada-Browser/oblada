#ifndef OBLADA_BROWSER_UI_MEDIA_CONTROLS_H_
#define OBLADA_BROWSER_UI_MEDIA_CONTROLS_H_

#include <memory>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/views/view.h"

class BrowserWindowInterface;

namespace tabs {
class TabInterface;
}  // namespace tabs

namespace oblada::media {

class MediaCard;
class Session;

// The bottom of the sidebar, above the workspaces, as in Zen: a card for each tab
// of the window playing media (but the one shown, which has its own controls), with
// buttons for the previous track, play or pause, the next track and the tab's
// sound. The newest card is in front, up to two others peek out above it; under
// the pointer they spread out, animated, and each shows what's playing (title,
// artist, how far along, which can be dragged), picture in picture, and a button
// that pauses it and puts the card away until it plays again. Clicking a card shows
// its tab (and its workspace).
//
// It replaces Chromium's media button in the toolbar (see toolbar_view.cc).
class MediaControls : public views::View,
                      public TabStripModelObserver,
                      public gfx::AnimationDelegate {
  METADATA_HEADER(MediaControls, views::View)

 public:
  explicit MediaControls(BrowserWindowInterface* browser);
  MediaControls(const MediaControls&) = delete;
  MediaControls& operator=(const MediaControls&) = delete;
  ~MediaControls() override;

  // Hidden in the compact sidebar.
  void SetCompact(bool compact);

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

 private:
  // Follows the window's tabs: a Session for each.
  void SyncSessions();
  // Shows the cards of the sessions that want one, and puts the others away.
  void Update();
  // Removes the cards put away, once out of sight.
  void RemoveGoneCards();
  // Stacks the cards back at once (e.g. hidden, the pointer can't leave them).
  void ResetExpansion();
  MediaCard* CardFor(const tabs::TabInterface* tab) const;
  std::vector<MediaCard*> Cards() const;
  // The height of the cards with the pointer over them (`expansion` 1) or not (0).
  int HeightAt(double expansion) const;

  raw_ptr<BrowserWindowInterface> browser_;
  std::vector<std::unique_ptr<Session>> sessions_;
  bool compact_ = false;
  // How spread out the cards are, from 0 (stacked) to 1 (under the pointer).
  gfx::SlideAnimation expansion_{this};
  // For the card of the tab shown before, which waits a little (see Session).
  base::OneShotTimer reveal_timer_;
  base::WeakPtrFactory<MediaControls> weak_factory_{this};
};

}  // namespace oblada::media

#endif  // OBLADA_BROWSER_UI_MEDIA_CONTROLS_H_
