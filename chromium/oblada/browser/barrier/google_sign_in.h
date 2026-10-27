#ifndef OBLADA_BROWSER_BARRIER_GOOGLE_SIGN_IN_H_
#define OBLADA_BROWSER_BARRIER_GOOGLE_SIGN_IN_H_

#include <optional>
#include <string>
#include <string_view>

#include "url/gurl.h"

class Profile;

namespace content {
class WebContents;
}  // namespace content

namespace url {
class Origin;
}  // namespace url

// Google sign-in on other sites, as Brave's permission: the ways of signing in
// with Google that still need Google's cookies in the site's pages, which the
// Barrier blocks as third parties' (Google's OAuth with a "storagerelay"
// redirect, Firebase's sign-in by redirect), opened by a site in a window or
// its own tab. As kGoogleSignInPref says, the Barrier asks first (a page of
// its own, in place of Google's), or blocks them; allowed on a site (its
// settings' google_sign_in), Google's sign-in pages get their cookies in the
// site's pages, and the sign-in goes on. request_filter.h asks here as pages'
// own navigations start.
namespace oblada::barrier {

struct GoogleSignInDecision {
  enum class Action {
    kNone,
    // A page of the Barrier's instead (`page`): the question, or the sign-in
    // blocked.
    kPage,
    // The sign-in goes on as `go_to`, its answer stripped.
    kGo,
  };
  Action action = Action::kNone;
  std::string page;
  GURL go_to;
};

// On the UI thread, for a page's own navigation to `url` in `contents`,
// started by `initiator`.
GoogleSignInDecision CheckGoogleSignIn(
    content::WebContents* contents,
    const GURL& url,
    const std::optional<url::Origin>& initiator);

// Google's sign-in pages' cookies on `site`, as it allows the sign-in or not
// (barrier.cc keeps them in step with the site's settings).
void SyncGoogleSignInCookies(Profile* profile,
                             std::string_view site,
                             bool allowed);

}  // namespace oblada::barrier

#endif  // OBLADA_BROWSER_BARRIER_GOOGLE_SIGN_IN_H_
