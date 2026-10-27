#ifndef OBLADA_BROWSER_BARRIER_SENSITIVE_SITES_H_
#define OBLADA_BROWSER_BARRIER_SENSITIVE_SITES_H_

#include <string>

#include "url/gurl.h"

namespace content {
class WebContents;
}  // namespace content

// Sensitive sites (about domestic violence and the like, Brave's list:
// lists/request-otr.json) in a temporary container, which keeps nothing once
// closed, where Brave offers its "off the record" tabs: as the setting says
// (kSensitiveSitesPref), the Barrier asks first (a page of its own, before the
// site), always reopens them there, or never. request_filter.h asks here as
// pages' own navigations start.
namespace oblada::barrier {

struct SensitiveSiteDecision {
  enum class Action {
    kNone,
    // The Barrier's question instead of the page (SensitiveSitePage()).
    kAsk,
    // The page goes on as `go_to`, its answer stripped.
    kGo,
    // The page reopens in a temporary container: this navigation stops.
    kStop,
  };
  Action action = Action::kNone;
  GURL go_to;
};

// On the UI thread, for a page's own navigation to `url` in `contents`.
SensitiveSiteDecision CheckSensitiveSite(content::WebContents* contents,
                                         const GURL& url);

// The question's page, for `url`.
std::string SensitiveSitePage(const GURL& url);

}  // namespace oblada::barrier

#endif  // OBLADA_BROWSER_BARRIER_SENSITIVE_SITES_H_
