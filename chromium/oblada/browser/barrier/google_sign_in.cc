#include "oblada/browser/barrier/google_sign_in.h"

#include <vector>

#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_pattern.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/base/url_util.h"
#include "oblada/browser/barrier/barrier.h"
#include "oblada/browser/barrier/domain_block.h"
#include "oblada/browser/l10n.h"
#include "url/origin.h"
#include "url/url_constants.h"

namespace oblada::barrier {

namespace {

constexpr char kGoogleSignInHost[] = "accounts.google.com";
constexpr char kFirebaseDomain[] = ".firebaseapp.com";

// The page's answer: the session's token, ".allow." or ".block.", then the
// site.
constexpr char kAnswerParam[] = "oblada-barrier-google";

bool IsSignInHost(std::string_view host) {
  return host == kGoogleSignInHost || host.ends_with(kFirebaseDomain);
}

// Whether `url` is one of the sign-ins that need Google's cookies in other
// sites' pages (as Brave tells them apart).
bool IsSignIn(const GURL& url) {
  if (!url.SchemeIs(url::kHttpsScheme)) {
    return false;
  }
  const std::string_view host = url.host();
  const std::string_view query = url.query();
  if (host == kGoogleSignInHost) {
    return query.find("redirect_uri=storagerelay") != std::string_view::npos;
  }
  return host.ends_with(kFirebaseDomain) && url.path().starts_with("/__/auth/") &&
         query.find("authType=signInViaRedirect") != std::string_view::npos;
}

ContentSettingsPattern HostPattern(std::string_view host) {
  return ContentSettingsPattern::FromString(base::StrCat({"[*.]", host}));
}

ContentSettingsPattern SitePattern(std::string_view site) {
  const bool domain = site.find('.') != std::string_view::npos &&
                      !GURL(base::StrCat({"https://", site})).HostIsIPAddress();
  return ContentSettingsPattern::FromString(
      domain ? base::StrCat({"[*.]", site}) : std::string(site));
}

// Lets `host`'s cookies reach `site`'s pages.
void AllowCookies(Profile* profile, std::string_view host,
                  std::string_view site) {
  HostContentSettingsMapFactory::GetForProfile(profile)
      ->SetContentSettingCustomScope(HostPattern(host), SitePattern(site),
                                     ContentSettingsType::COOKIES,
                                     CONTENT_SETTING_ALLOW);
}

std::string Page(const GURL& url, const std::string& site, bool blocked,
                 bool close) {
  const auto text = [&](const char* key) {
    std::string escaped = base::EscapeForHTML(L10nUTF8(key));
    base::ReplaceSubstringsAfterOffset(&escaped, 0, "$SITE",
                                       base::EscapeForHTML(site));
    return escaped;
  };
  const auto answer = [&](const char* choice) {
    return JavaScriptUrl(net::AppendOrReplaceQueryParameter(
        url, kAnswerParam, base::StrCat({SessionToken(), choice, site})));
  };
  const char* title =
      blocked ? "barrier.google.blocked_title" : "barrier.google.title";
  return base::StrCat({
      R"(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width">
<meta name="color-scheme" content="light dark">
<title>)",
      text(title),
      R"(</title><style>
body { font: 15px system-ui, sans-serif; margin: 0; display: grid;
  place-items: center; min-height: 100vh; background: Canvas;
  color: CanvasText; }
main { max-width: 520px; padding: 32px; }
h1 { font-size: 22px; font-weight: 600; margin: 0 0 12px; }
p { line-height: 1.5; opacity: 0.85; }
.buttons { display: flex; gap: 10px; margin-top: 24px; flex-wrap: wrap; }
button { font: inherit; border: none; border-radius: 10px; padding: 9px 16px;
  cursor: pointer; background: color-mix(in srgb, CanvasText 10%, Canvas);
  color: CanvasText; }
button.primary { background: #3a82f7; color: white; font-weight: 600; }
</style></head><body><main>
<h1>)",
      text(title),
      R"(</h1><p>)",
      text(blocked ? "barrier.google.blocked_text" : "barrier.google.text"),
      R"(</p><div class="buttons">)",
      blocked ? std::string() : base::StrCat({R"(<button id="block">)",
                                              text("barrier.google.block"),
                                              "</button>"}),
      R"(<button class="primary" id="allow">)",
      text("barrier.google.allow"),
      R"(</button></div></main><script>
const block = document.getElementById('block');
if (block) block.onclick = () => location.replace()",
      answer(".block."),
      R"();
document.getElementById('allow').onclick = () => location.replace()",
      answer(".allow."),
      ");\n",
      // The sign-in's window, just refused, closes.
      close ? "if (window.opener) window.close();\n" : "",
      "</script></body></html>",
  });
}

}  // namespace

GoogleSignInDecision CheckGoogleSignIn(
    content::WebContents* contents,
    const GURL& url,
    const std::optional<url::Origin>& initiator) {
  GoogleSignInDecision decision;
  if (!contents || !IsOn() || !IsSignIn(url)) {
    return decision;
  }
  Profile* profile = Profile::FromBrowserContext(contents->GetBrowserContext());

  // The question's answer.
  std::string answer;
  if (net::GetValueForKeyInQuery(url, kAnswerParam, &answer) &&
      !SessionToken().empty() && answer.starts_with(SessionToken())) {
    const GURL original = WithoutQueryParameter(url, kAnswerParam);
    answer.erase(0, SessionToken().size());
    const bool allow = answer.starts_with(".allow.");
    if (!allow && !answer.starts_with(".block.")) {
      return decision;
    }
    // After ".allow." or ".block.".
    const std::string site = answer.substr(7);
    if (site.empty() || site != SiteOf(GURL(base::StrCat({"https://", site})))) {
      return decision;
    }
    SiteOverrides overrides = GetSiteOverrides(profile, site);
    overrides.google_sign_in = allow;
    SetSiteOverrides(profile, site, overrides);
    if (allow) {
      AllowCookies(profile, original.host(), site);
      decision.action = GoogleSignInDecision::Action::kGo;
      decision.go_to = original;
    } else {
      decision.action = GoogleSignInDecision::Action::kPage;
      decision.page = Page(original, site, /*blocked=*/true, /*close=*/true);
    }
    return decision;
  }

  // Only when another site starts it.
  if (!initiator || initiator->opaque() ||
      (initiator->scheme() != url::kHttpsScheme &&
       initiator->scheme() != url::kHttpScheme) ||
      IsSignInHost(initiator->host()) ||
      net::registry_controlled_domains::SameDomainOrHost(
          initiator->GetURL(), GURL(base::StrCat({"https://", kGoogleSignInHost})),
          net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES)) {
    return decision;
  }
  const GURL site_url = initiator->GetURL();
  const std::string site = SiteOf(site_url);
  if (site.empty() || !GetSettings(profile, site_url).enabled) {
    return decision;
  }
  const std::optional<bool> allowed =
      GetSiteOverrides(profile, site).google_sign_in;
  if (allowed.value_or(false)) {
    AllowCookies(profile, url.host(), site);
    return decision;
  }
  const bool ask =
      !allowed && profile->GetPrefs()->GetBoolean(kGoogleSignInPref);
  decision.action = GoogleSignInDecision::Action::kPage;
  decision.page = Page(url, site, /*blocked=*/!ask, /*close=*/false);
  return decision;
}

void SyncGoogleSignInCookies(Profile* profile,
                             std::string_view site,
                             bool allowed) {
  HostContentSettingsMap* map =
      HostContentSettingsMapFactory::GetForProfile(profile);
  if (allowed) {
    AllowCookies(profile, kGoogleSignInHost, site);
    return;
  }
  // Every sign-in host's, Google's and Firebase apps'.
  const ContentSettingsPattern site_pattern = SitePattern(site);
  std::vector<ContentSettingsPattern> hosts;
  for (const ContentSettingPatternSource& setting :
       map->GetSettingsForOneType(ContentSettingsType::COOKIES)) {
    if (setting.secondary_pattern == site_pattern &&
        IsSignInHost(setting.primary_pattern.GetHost())) {
      hosts.push_back(setting.primary_pattern);
    }
  }
  for (const ContentSettingsPattern& host : hosts) {
    map->SetContentSettingCustomScope(host, site_pattern,
                                      ContentSettingsType::COOKIES,
                                      CONTENT_SETTING_DEFAULT);
  }
}

}  // namespace oblada::barrier
