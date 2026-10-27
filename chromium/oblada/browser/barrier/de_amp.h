#ifndef OBLADA_BROWSER_BARRIER_DE_AMP_H_
#define OBLADA_BROWSER_BARRIER_DE_AMP_H_

#include <optional>
#include <string_view>

class GURL;

namespace content {
class NavigationThrottleRegistry;
}  // namespace content

// AMP pages left for the original, as Brave does (kDeAmpPref, barrier.h).
// AMP caches' copies are known by their address, and skipped before they're
// requested (url_cleaning.h's debounce rules); pages a publisher serves as AMP
// on its own site are known by their HTML: an <html amp> (or <html ⚡>) tag,
// with a canonical link to the original. Pages' responses are read for it as
// they arrive, the first part the network gives, before they commit; an AMP
// page is then dropped for the original, in the same tab.
namespace oblada::barrier {

// chrome_content_browser_client_navigation_throttles.cc.
void MaybeAddDeAmpThrottle(content::NavigationThrottleRegistry& registry);

// The original of an AMP page served at `url`, from the start of its HTML
// `html`; nothing if it isn't one, or doesn't say where the original is.
std::optional<GURL> CanonicalOfAmpPage(std::string_view html, const GURL& url);

}  // namespace oblada::barrier

#endif  // OBLADA_BROWSER_BARRIER_DE_AMP_H_
