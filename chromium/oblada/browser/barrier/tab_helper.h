#ifndef OBLADA_BROWSER_BARRIER_TAB_HELPER_H_
#define OBLADA_BROWSER_BARRIER_TAB_HELPER_H_

#include <vector>

#include "base/callback_list.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "url/gurl.h"

namespace oblada::barrier {

// A tab's Barrier: what it blocked on the page it shows (the button's number,
// the panel's list).
class BarrierTabHelper : public content::WebContentsObserver,
                         public content::WebContentsUserData<BarrierTabHelper> {
 public:
  ~BarrierTabHelper() override;

  // How many requests for trackers and ads the page made that were blocked.
  int blocked_count() const { return blocked_count_; }
  // Their addresses, in order, each once (as many as the panel lists).
  const std::vector<GURL>& blocked() const { return blocked_; }

  // Notifies of each change to blocked() (more blocked, a new page).
  base::CallbackListSubscription ObserveBlocked(base::RepeatingClosure changed);

  // A request of `frame`'s page was blocked (request_filter.h).
  static void OnBlocked(content::GlobalRenderFrameHostId frame, const GURL& url);

  // Every tab's (storage.h tells by them which sites are open).
  static const std::vector<BarrierTabHelper*>& All();

  // content::WebContentsObserver:
  void ReadyToCommitNavigation(content::NavigationHandle* navigation) override;
  void PrimaryPageChanged(content::Page& page) override;
  void WebContentsDestroyed() override;

 private:
  friend class content::WebContentsUserData<BarrierTabHelper>;

  explicit BarrierTabHelper(content::WebContents* contents);

  void AddBlocked(const GURL& url);

  // The page the tab shows, which it leaves next.
  GURL page_url_;
  int blocked_count_ = 0;
  std::vector<GURL> blocked_;
  base::RepeatingClosureList observers_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

// tab_helpers.cc
void AttachTabHelpers(content::WebContents* contents);

}  // namespace oblada::barrier

#endif  // OBLADA_BROWSER_BARRIER_TAB_HELPER_H_
