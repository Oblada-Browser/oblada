#include "oblada/browser/barrier/de_amp.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_user_data.h"
#include "content/public/common/referrer.h"
#include "net/http/http_response_headers.h"
#include "third_party/blink/public/mojom/loader/referrer.mojom.h"
#include "oblada/browser/barrier/barrier.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace oblada::barrier {

namespace {

// How much of a page is read for its <html> tag and canonical link.
constexpr size_t kMaxBytes = 3 * 65536;
// How many AMP pages in a row a tab is taken past, against loops.
constexpr size_t kMaxHops = 7;
// How long a page waits for the start of its HTML: one that sends its headers
// and then takes its time commits all the same.
constexpr base::TimeDelta kMaxWait = base::Seconds(2);

bool IsSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

// A tag's attributes, from what follows its name up to its '>': names in
// lowercase, values as written (without quotes).
std::vector<std::pair<std::string, std::string>> ParseAttributes(
    std::string_view tag) {
  std::vector<std::pair<std::string, std::string>> attributes;
  size_t i = 0;
  while (i < tag.size()) {
    while (i < tag.size() && (IsSpace(tag[i]) || tag[i] == '/')) {
      ++i;
    }
    const size_t name_start = i;
    while (i < tag.size() && !IsSpace(tag[i]) && tag[i] != '=' &&
           tag[i] != '/') {
      ++i;
    }
    if (i == name_start) {
      break;
    }
    std::string name =
        base::ToLowerASCII(tag.substr(name_start, i - name_start));
    while (i < tag.size() && IsSpace(tag[i])) {
      ++i;
    }
    std::string value;
    if (i < tag.size() && tag[i] == '=') {
      ++i;
      while (i < tag.size() && IsSpace(tag[i])) {
        ++i;
      }
      if (i < tag.size() && (tag[i] == '"' || tag[i] == '\'')) {
        const char quote = tag[i++];
        const size_t end = tag.find(quote, i);
        const size_t value_end = end == std::string_view::npos ? tag.size() : end;
        value = std::string(tag.substr(i, value_end - i));
        i = value_end + 1;
      } else {
        const size_t value_start = i;
        while (i < tag.size() && !IsSpace(tag[i])) {
          ++i;
        }
        value = std::string(tag.substr(value_start, i - value_start));
      }
    }
    attributes.emplace_back(std::move(name), std::move(value));
  }
  return attributes;
}

// The next `name` tag in `lower` (the page in lowercase) from `from`: where
// its attributes start and end (at its '>'), or nothing.
std::optional<std::pair<size_t, size_t>> FindTag(std::string_view lower,
                                                 std::string_view name,
                                                 size_t from) {
  const std::string open = "<" + std::string(name);
  for (size_t at = lower.find(open, from); at != std::string_view::npos;
       at = lower.find(open, at + 1)) {
    const size_t start = at + open.size();
    if (start < lower.size() &&
        (IsSpace(lower[start]) || lower[start] == '>' || lower[start] == '/')) {
      const size_t end = lower.find('>', start);
      if (end == std::string_view::npos) {
        return std::nullopt;
      }
      return std::make_pair(start, end);
    }
  }
  return std::nullopt;
}

// `url` as compared against others: without its fragment and a final '/'.
std::string Comparable(const GURL& url) {
  std::string spec = url.GetWithoutRef().spec();
  if (spec.ends_with('/')) {
    spec.pop_back();
  }
  return spec;
}

// The AMP pages a tab was just taken past, and the originals it was taken to,
// against loops: a page that says its original is an AMP page that leads back.
class DeAmpTrail : public content::WebContentsUserData<DeAmpTrail> {
 public:
  ~DeAmpTrail() override = default;

  // The pages of the trail so far, if `navigation` is one the trail led to;
  // a new trail otherwise.
  std::vector<std::string>& For(content::NavigationHandle* navigation) {
    const std::vector<GURL>& redirects = navigation->GetRedirectChain();
    const GURL& first =
        redirects.empty() ? navigation->GetURL() : redirects.front();
    if (Comparable(first) != awaited_) {
      pages_.clear();
    }
    awaited_.clear();
    return pages_;
  }

  void Await(const GURL& url) { awaited_ = Comparable(url); }

 private:
  friend class content::WebContentsUserData<DeAmpTrail>;
  explicit DeAmpTrail(content::WebContents* contents)
      : content::WebContentsUserData<DeAmpTrail>(*contents) {}

  std::vector<std::string> pages_;
  std::string awaited_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(DeAmpTrail);

void LoadOriginal(base::WeakPtr<content::WebContents> contents,
                  content::NavigationController::LoadURLParams params) {
  if (contents) {
    contents->GetController().LoadURLWithParams(params);
  }
}

class DeAmpThrottle : public content::NavigationThrottle {
 public:
  using content::NavigationThrottle::NavigationThrottle;

  ThrottleCheckResult WillProcessResponse() override {
    content::NavigationHandle* navigation = navigation_handle();
    if (!navigation->IsInPrimaryMainFrame() || navigation->IsSameDocument() ||
        navigation->IsPost() || !navigation->GetURL().SchemeIsHTTPOrHTTPS()) {
      return PROCEED;
    }
    const net::HttpResponseHeaders* headers = navigation->GetResponseHeaders();
    std::string mime_type;
    if (!headers || !headers->GetMimeType(&mime_type) ||
        !base::EqualsCaseInsensitiveASCII(mime_type, "text/html")) {
      return PROCEED;
    }
    Profile* profile = Profile::FromBrowserContext(
        navigation->GetWebContents()->GetBrowserContext());
    if (!IsPrivacyOn(profile, kDeAmpPref) ||
        !GetSettings(profile, navigation->GetURL()).enabled) {
      return PROCEED;
    }
    navigation->GetResponseBody(base::BindOnce(&DeAmpThrottle::OnResponseBody,
                                               weak_factory_.GetWeakPtr()));
    timeout_.Start(FROM_HERE, kMaxWait,
                   base::BindOnce(&DeAmpThrottle::Resume, base::Unretained(this)));
    return DEFER;
  }

  const char* GetNameForLogging() override { return "ObladaDeAmpThrottle"; }

 private:
  void OnResponseBody(const std::string& body) {
    // Too late: the page went on without it.
    if (!timeout_.IsRunning()) {
      return;
    }
    timeout_.Stop();
    content::NavigationHandle* navigation = navigation_handle();
    const GURL url = navigation->GetURL();
    const std::optional<GURL> original = CanonicalOfAmpPage(body, url);
    if (!original) {
      Resume();
      return;
    }
    content::WebContents* contents = navigation->GetWebContents();
    DeAmpTrail::CreateForWebContents(contents);
    DeAmpTrail* trail = DeAmpTrail::FromWebContents(contents);
    std::vector<std::string>& pages = trail->For(navigation);
    const std::string comparable = Comparable(*original);
    // Not back where the tab came from, nor round in circles.
    if (comparable == Comparable(navigation->GetReferrer().url) ||
        std::ranges::contains(pages, comparable) || pages.size() >= kMaxHops) {
      Resume();
      return;
    }
    pages.push_back(Comparable(url));
    pages.push_back(comparable);
    trail->Await(*original);

    content::NavigationController::LoadURLParams params(*original);
    params.transition_type =
        ui::PageTransitionFromInt(navigation->GetPageTransition());
    // Started by the AMP page, which named the original: a navigation from
    // another site, not one the user typed, so sites get what they'd get from
    // a link (no SameSite=Strict cookies, Sec-Fetch-Site: cross-site).
    params.initiator_origin = url::Origin::Create(url);
    params.is_renderer_initiated = true;
    params.has_user_gesture = navigation->HasUserGesture();
    const blink::mojom::Referrer& referrer = navigation->GetReferrer();
    params.referrer = content::Referrer::SanitizeForRequest(
        *original, content::Referrer(referrer.url, referrer.policy));
    // The throttle is gone once the navigation is cancelled.
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE, base::BindOnce(&LoadOriginal, contents->GetWeakPtr(),
                                  std::move(params)));
    CancelDeferredNavigation(CANCEL_AND_IGNORE);
  }

  base::OneShotTimer timeout_;
  base::WeakPtrFactory<DeAmpThrottle> weak_factory_{this};
};

}  // namespace

void MaybeAddDeAmpThrottle(content::NavigationThrottleRegistry& registry) {
  if (IsOn() && registry.GetNavigationHandle().IsInPrimaryMainFrame()) {
    registry.AddThrottle(std::make_unique<DeAmpThrottle>(registry));
  }
}

std::optional<GURL> CanonicalOfAmpPage(std::string_view html, const GURL& url) {
  html = html.substr(0, kMaxBytes);
  const std::string lower = base::ToLowerASCII(html);
  const std::optional<std::pair<size_t, size_t>> html_tag =
      FindTag(lower, "html", 0);
  if (!html_tag) {
    return std::nullopt;
  }
  bool amp = false;
  for (const auto& [name, value] : ParseAttributes(std::string_view(html).substr(
           html_tag->first, html_tag->second - html_tag->first))) {
    if (name == "amp" || name == "⚡") {
      amp = true;
    }
  }
  if (!amp) {
    return std::nullopt;
  }
  for (std::optional<std::pair<size_t, size_t>> link =
           FindTag(lower, "link", html_tag->second);
       link; link = FindTag(lower, "link", link->second)) {
    std::string rel;
    std::string href;
    for (auto& [name, value] : ParseAttributes(std::string_view(html).substr(
             link->first, link->second - link->first))) {
      if (name == "rel") {
        rel = base::ToLowerASCII(value);
      } else if (name == "href") {
        href = std::move(value);
      }
    }
    const std::vector<std::string_view> rels = base::SplitStringPiece(
        rel, " \t\n\r\f", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    if (!std::ranges::contains(rels, "canonical") || href.empty()) {
      continue;
    }
    base::ReplaceSubstringsAfterOffset(&href, 0, "&amp;", "&");
    const GURL original = url.Resolve(href);
    if (!original.is_valid() || !original.SchemeIsHTTPOrHTTPS() ||
        Comparable(original) == Comparable(url)) {
      return std::nullopt;
    }
    return original;
  }
  return std::nullopt;
}

}  // namespace oblada::barrier
