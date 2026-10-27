#include "oblada/browser/barrier/tab_helper.h"

#include <algorithm>
#include <vector>

#include "base/no_destructor.h"

#include "content/public/browser/page.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "oblada/browser/barrier/barrier.h"
#include "oblada/browser/barrier/cosmetic_filters.h"
#include "oblada/browser/barrier/storage.h"

namespace oblada::barrier {

namespace {

// Enough for the panel's list: pages that block more only count them.
constexpr size_t kMaxListed = 500;

std::vector<BarrierTabHelper*>& Helpers() {
  static base::NoDestructor<std::vector<BarrierTabHelper*>> helpers;
  return *helpers;
}

}  // namespace

BarrierTabHelper::BarrierTabHelper(content::WebContents* contents)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<BarrierTabHelper>(*contents) {
  Helpers().push_back(this);
}

BarrierTabHelper::~BarrierTabHelper() {
  std::erase(Helpers(), this);
}

// static
const std::vector<BarrierTabHelper*>& BarrierTabHelper::All() {
  return Helpers();
}

base::CallbackListSubscription BarrierTabHelper::ObserveBlocked(
    base::RepeatingClosure changed) {
  return observers_.Add(std::move(changed));
}

// static
void BarrierTabHelper::OnBlocked(content::GlobalRenderFrameHostId frame,
                                 const GURL& url) {
  AddToBlockedTotal(1);
  content::RenderFrameHost* host = content::RenderFrameHost::FromID(frame);
  if (!host || !host->GetOutermostMainFrame()->IsInPrimaryMainFrame()) {
    return;
  }
  if (auto* helper = FromWebContents(
          content::WebContents::FromRenderFrameHost(host))) {
    helper->AddBlocked(url);
  }
}

void BarrierTabHelper::AddBlocked(const GURL& url) {
  ++blocked_count_;
  if (blocked_.size() < kMaxListed &&
      std::ranges::find(blocked_, url) == blocked_.end()) {
    blocked_.push_back(url);
  }
  observers_.Notify();
}

void BarrierTabHelper::ReadyToCommitNavigation(
    content::NavigationHandle* navigation) {
  SendPageSettings(navigation);
}

void BarrierTabHelper::PrimaryPageChanged(content::Page& page) {
  const GURL left = std::move(page_url_);
  page_url_ = page.GetMainDocument().GetLastCommittedURL();
  if (left.is_valid() && SiteOf(left) != SiteOf(page_url_)) {
    OnPageLeft(web_contents(), left);
  }
  if (blocked_count_) {
    blocked_count_ = 0;
    blocked_.clear();
    observers_.Notify();
  }
}

void BarrierTabHelper::WebContentsDestroyed() {
  std::erase(Helpers(), this);
  if (page_url_.is_valid()) {
    OnPageLeft(web_contents(), page_url_);
  }
}

void AttachTabHelpers(content::WebContents* contents) {
  BarrierTabHelper::CreateForWebContents(contents);
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(BarrierTabHelper);

}  // namespace oblada::barrier
