#ifndef OBLADA_BROWSER_BARRIER_BARRIER_H_
#define OBLADA_BROWSER_BARRIER_BARRIER_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/callback_list.h"
#include "base/feature_list.h"
#include "base/functional/callback_forward.h"

class GURL;
class PrefRegistrySimple;
class PrefService;
class Profile;

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

// The Barrier ("Barriera"): Oblada's protections of privacy, as Brave's Shields
// and privacy settings. Each site gets them as the profile's defaults say
// (Settings > Oblada > Barrier) unless it has settings of its own (the
// Barrier's button, or its section of the site panel), which go by site
// (eTLD+1: example.com's are those of www.example.com too):
// - blocking trackers and ads (filter_lists.h, request_filter.h), and their
//   elements on pages (cosmetic filters, renderer/barrier);
// - upgrading connections to HTTPS;
// - blocking scripts (Chromium's JavaScript content setting);
// - fingerprinting protection (farbling.h, and Blink);
// - blocking third-party cookies, whose storage is forgotten as the site's
//   tabs close (storage.h);
// - forgetting the site as its tabs close.
// Plus protections that aren't by site (Settings > Oblada > Privacy): Global
// Privacy Control, stripping tracking parameters, skipping tracking redirects,
// AMP pages, the languages sites see, sensitive sites in temporary
// containers, WebRTC's IP addresses, social media embeds, Google sign-in on
// other sites.
//
// A switch turns all of it off ("DANGEROUS", Settings > Oblada): what's
// decided as pages load at once, what's decided as Oblada starts (web APIs
// turned off, network changes) after a relaunch.
namespace oblada::barrier {

// --- The switch ---

// Local State: false when the Barrier is off, all of it ("DANGEROUS").
inline constexpr char kEnabledPref[] = "oblada.barrier.enabled";

// Whether the Barrier is on.
bool IsOn();
// Whether it was on when Oblada started: what's decided at startup follows
// this until the next one.
bool WasOnAtStartup();

// --- What a site gets ---

enum class AdBlocking {
  kDisabled,
  // Trackers and ads, but those of the site itself that only lists flagged
  // for first-party protection would block.
  kStandard,
  // The site's own too, and an interstitial before sites the lists block.
  kAggressive,
};

enum class HttpsUpgrade {
  kDisabled,
  // Upgraded, back to HTTP without a word if HTTPS fails.
  kStandard,
  // Upgraded, with a warning before HTTP.
  kStrict,
};

enum class Cookies {
  kBlockThirdParty,
  kAllowAll,
};

// The APIs fingerprinting protection changes, which a site can be left with
// as they are (bits of Settings::fingerprinting_exceptions).
enum FingerprintingApi : uint32_t {
  kCanvas = 1u << 0,
  kAudio = 1u << 1,
  kWebGl = 1u << 2,
  kWebGpu = 1u << 3,
  kFonts = 1u << 4,
  kScreen = 1u << 5,
  // navigator.hardwareConcurrency and deviceMemory.
  kHardware = 1u << 6,
  kPlugins = 1u << 7,
  kMediaDevices = 1u << 8,
  kSpeechSynthesis = 1u << 9,
  kKeyboard = 1u << 10,
  kLanguages = 1u << 11,
  kUsb = 1u << 12,
  // How many WebSockets and EventSource connections a site may have open at
  // once (not in FingerprintingApis(): only Brave's list of exceptions leaves
  // it alone).
  kConnectionPools = 1u << 13,
};
// Every FingerprintingApi, with its name in prefs and Oblada's strings
// ("canvas"…).
struct FingerprintingApiName {
  FingerprintingApi api;
  std::string_view name;
};
const std::vector<FingerprintingApiName>& FingerprintingApis();

struct Settings {
  // The Barrier is on for the site (and on at all): when it isn't, nothing
  // below applies.
  bool enabled = true;
  AdBlocking ads = AdBlocking::kStandard;
  HttpsUpgrade https = HttpsUpgrade::kStandard;
  bool block_scripts = false;
  bool fingerprinting = true;
  // FingerprintingApi bits fingerprinting protection leaves alone.
  uint32_t fingerprinting_exceptions = 0;
  Cookies cookies = Cookies::kBlockThirdParty;
  bool forget = false;

  bool AdsOn() const { return enabled && ads != AdBlocking::kDisabled; }
  bool FingerprintingOn(FingerprintingApi api) const {
    return enabled && fingerprinting && !(fingerprinting_exceptions & api);
  }
};

// What `url`'s site gets, `url` being a page's address (its top-level frame's).
Settings GetSettings(Profile* profile, const GURL& url);
// What sites get unless they have settings of their own.
Settings GetDefaults(Profile* profile);

// How `url` is upgraded to HTTPS (https_upgrades_interceptor.cc,
// https_upgrades_util.cc): as its page's site says, but not at all for the
// sites Brave knows serve HTTPS badly (lists/https-upgrade-exceptions-list.txt),
// nor where the Barrier is down.
HttpsUpgrade HttpsUpgradeFor(Profile* profile, const GURL& url);

// A site's settings of its own; unset, the defaults.
struct SiteOverrides {
  std::optional<bool> enabled;
  std::optional<AdBlocking> ads;
  std::optional<HttpsUpgrade> https;
  std::optional<bool> block_scripts;
  std::optional<bool> fingerprinting;
  std::optional<uint32_t> fingerprinting_exceptions;
  std::optional<Cookies> cookies;
  std::optional<bool> forget;
  // Google sign-in allowed, or not, as asked (google_sign_in.h).
  std::optional<bool> google_sign_in;

  bool empty() const;
};
SiteOverrides GetSiteOverrides(Profile* profile, std::string_view site);
// Replaces `site`'s settings (an empty `overrides` forgets them). In an
// incognito profile, they last as long as it does.
void SetSiteOverrides(Profile* profile,
                      std::string_view site,
                      const SiteOverrides& overrides);
// The sites with settings of their own.
std::vector<std::string> SitesWithOverrides(Profile* profile);

// The site settings go by: `url`'s registrable domain (eTLD+1), or its host
// when it has none (an IP address, localhost). Empty for anything but web
// pages.
std::string SiteOf(const GURL& url);

// Whether lists may be downloaded from `url`: HTTPS, or HTTP from this computer
// (tests' servers). Anyone on the network could change a list fetched over
// plain HTTP, and lists change what pages run.
bool IsListUrlAllowed(const GURL& url);

// Notifies of changes to the profile's defaults or its sites' settings.
base::CallbackListSubscription ObserveSettings(Profile* profile,
                                               base::RepeatingClosure changed);

// --- Settings that aren't by site (Settings > Oblada > Privacy) ---

// Profile prefs, all on unless said otherwise.
// Global Privacy Control: Sec-GPC and navigator.globalPrivacyControl.
inline constexpr char kGpcPref[] = "oblada.barrier.gpc";
// Tracking parameters stripped from addresses (url_cleaning.h).
inline constexpr char kStripParamsPref[] = "oblada.barrier.strip_params";
// Tracking redirects skipped (url_cleaning.h).
inline constexpr char kDebouncePref[] = "oblada.barrier.debounce";
// AMP pages left for the original (de_amp.h).
inline constexpr char kDeAmpPref[] = "oblada.barrier.de_amp";
// Sites see only the first language, and fewer fonts.
inline constexpr char kReduceLanguagePref[] = "oblada.barrier.reduce_language";
// Sensitive sites in a temporary container: "ask", "always" or "never".
inline constexpr char kSensitiveSitesPref[] = "oblada.barrier.sensitive_sites";
// Google sign-in on other sites (google_sign_in.h): asked about; blocked
// unless allowed on the site when off.
inline constexpr char kGoogleSignInPref[] = "oblada.barrier.google_sign_in";
// Local State, as filter lists are the same for every profile: embedded posts
// and logins of social networks (filter lists of Brave's), allowed for
// Facebook and X, not LinkedIn.
inline constexpr char kFacebookEmbedsPref[] = "oblada.barrier.facebook_embeds";
inline constexpr char kXEmbedsPref[] = "oblada.barrier.x_embeds";
inline constexpr char kLinkedInEmbedsPref[] = "oblada.barrier.linkedin_embeds";
// The number of blocked items on the Barrier's button.
inline constexpr char kShowCountPref[] = "oblada.barrier.show_count";

// Whether a profile pref above is on, and the Barrier with it.
bool IsPrivacyOn(Profile* profile, const char* pref);

// Local State, decided as Oblada starts (AddFeatureOverrides()), so changes
// apply after a relaunch; all on, and each only with the Barrier on then.
// Web APIs that mostly serve to recognize you off, as in Brave.
inline constexpr char kWebApisOffPref[] = "oblada.barrier.web_apis_off";
// Referrers never say more than the origin across sites.
inline constexpr char kReferrerCapPref[] = "oblada.barrier.referrer_cap";
// Cookies last 6 months at most.
inline constexpr char kCookieExpiryCapPref[] =
    "oblada.barrier.cookie_expiry_cap";
// Connections apart for each site.
inline constexpr char kPartitionConnectionsPref[] =
    "oblada.barrier.partition_connections";
// HSTS apart for each site, as Brave (net's kObladaPartitionHsts).
inline constexpr char kPartitionHstsPref[] = "oblada.barrier.partition_hsts";
// Whether one of them, or the Barrier's switch, changed since Oblada started.
bool StartupProtectionsChanged();

// The defaults (profile prefs).
inline constexpr char kAdsPref[] = "oblada.barrier.ads";
inline constexpr char kHttpsPref[] = "oblada.barrier.https";
inline constexpr char kFingerprintingPref[] = "oblada.barrier.fingerprinting";
inline constexpr char kForgetPref[] = "oblada.barrier.forget";
// Sites' settings of their own, by site.
inline constexpr char kSitesPref[] = "oblada.barrier.sites";

// Local State: how many trackers and ads were blocked, ever.
inline constexpr char kBlockedTotalPref[] = "oblada.barrier.blocked_total";
void AddToBlockedTotal(int count);

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);
void RegisterLocalStatePrefs(PrefRegistrySimple* registry);

// Gives Chromium's prefs the Barrier's defaults, when it was on at startup
// (browser_prefs.cc, once Chromium's are registered).
void RegisterChromiumDefaults(user_prefs::PrefRegistrySyncable* registry);

// chrome_feature_list_creator.cc, as Local State is read and before features
// are set up: remembers whether the Barrier was on (WasOnAtStartup()), and
// adds its changes to Chromium's features to `overrides` (which the command
// line overrides in turn): web APIs Brave turns off (Web Bluetooth, the File
// System Access API's local files, Web Serial, FedCM, WebOTP, the Reporting
// API…), network connections partitioned by site, Global Privacy Control,
// and Oblada's own changes to the network (referrers, cookies' lifetime),
// which the switch turns off.
void AddFeatureOverrides(
    PrefService* local_state,
    std::vector<base::FeatureList::FeatureOverrideInfo>& overrides);

// Called once the browser has started.
void Init();

}  // namespace oblada::barrier

#endif  // OBLADA_BROWSER_BARRIER_BARRIER_H_
