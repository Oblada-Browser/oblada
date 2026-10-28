#include "oblada/browser/barrier/barrier.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <utility>

#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/scoped_observation.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/content_settings/cookie_settings_factory.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_manager_observer.h"
#include "components/attribution_reporting/features.h"
#include "components/content_settings/core/browser/content_settings_observer.h"
#include "components/content_settings/core/browser/cookie_settings.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings_pattern.h"
#include "components/content_settings/core/common/cookie_controls_enforcement.h"
#include "components/content_settings/core/common/pref_names.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "content/public/common/content_features.h"
#include "net/base/features.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/base/url_util.h"
#include "oblada/browser/barrier/data_lists.h"
#include "oblada/browser/barrier/domain_block.h"
#include "oblada/browser/barrier/filter_lists.h"
#include "oblada/browser/barrier/google_sign_in.h"
#include "services/network/public/cpp/features.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/features_generated.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace oblada::barrier {

namespace {

// Whether the profile's exceptions to Chromium's settings for scripts and
// cookies were last made with the Barrier on (SwitchWatcher).
constexpr char kSyncedOnPref[] = "oblada.barrier.synced_on";

// Whether the Barrier was on as Oblada started; unset until it's known.
std::optional<bool> g_on_at_startup;

// The protections decided as Oblada starts, with a switch each.
constexpr const char* kStartupPrefs[] = {
    kWebApisOffPref, kReferrerCapPref, kCookieExpiryCapPref,
    kPartitionConnectionsPref, kPartitionHstsPref,
};

// Their values as Oblada started.
std::map<std::string, bool>& StartupValues() {
  static base::NoDestructor<std::map<std::string, bool>> values;
  return *values;
}

constexpr std::pair<AdBlocking, std::string_view> kAdBlockingNames[] = {
    {AdBlocking::kDisabled, "disabled"},
    {AdBlocking::kStandard, "standard"},
    {AdBlocking::kAggressive, "aggressive"},
};
constexpr std::pair<HttpsUpgrade, std::string_view> kHttpsNames[] = {
    {HttpsUpgrade::kDisabled, "disabled"},
    {HttpsUpgrade::kStandard, "standard"},
    {HttpsUpgrade::kStrict, "strict"},
};
constexpr std::pair<Cookies, std::string_view> kCookiesNames[] = {
    {Cookies::kBlockThirdParty, "block_third_party"},
    {Cookies::kAllowAll, "allow_all"},
};

template <typename T, size_t N>
std::optional<T> FromName(const std::pair<T, std::string_view> (&names)[N],
                          const std::string* name) {
  if (name) {
    for (const auto& [value, value_name] : names) {
      if (value_name == *name) {
        return value;
      }
    }
  }
  return std::nullopt;
}

template <typename T, size_t N>
std::string_view NameOf(const std::pair<T, std::string_view> (&names)[N],
                        T value) {
  for (const auto& [named, name] : names) {
    if (named == value) {
      return name;
    }
  }
  return names[0].second;
}

// Per profile, incognito ones apart: who to tell about changes, and an
// incognito profile's sites' settings.
class ProfileBarrier : public base::SupportsUserData::Data,
                       public content_settings::Observer {
 public:
  static ProfileBarrier& Get(Profile* profile) {
    auto* data = static_cast<ProfileBarrier*>(profile->GetUserData(&kKey));
    if (!data) {
      auto owned = std::make_unique<ProfileBarrier>(profile);
      data = owned.get();
      profile->SetUserData(&kKey, std::move(owned));
    }
    return *data;
  }

  explicit ProfileBarrier(Profile* profile) : profile_(profile) {
    // Incognito profiles read the defaults and sites of their own profile.
    registrar_.Init(profile->GetOriginalProfile()->GetPrefs());
    for (const char* pref : {kAdsPref, kHttpsPref, kFingerprintingPref,
                             kForgetPref, kSitesPref,
                             prefs::kCookieControlsMode}) {
      registrar_.Add(pref, base::BindRepeating(&ProfileBarrier::Notify,
                                               base::Unretained(this)));
    }
    if (PrefService* local_state = g_browser_process->local_state()) {
      local_state_registrar_.Init(local_state);
      local_state_registrar_.Add(
          kEnabledPref,
          base::BindRepeating(&ProfileBarrier::Notify, base::Unretained(this)));
    }
    settings_map_observation_.Observe(
        HostContentSettingsMapFactory::GetForProfile(profile));
  }

  // content_settings::Observer:
  void OnContentSettingChanged(const ContentSettingsPattern& primary_pattern,
                               const ContentSettingsPattern& secondary_pattern,
                               ContentSettingsTypeSet content_type_set) override {
    // The default for scripts.
    if (content_type_set.Contains(ContentSettingsType::JAVASCRIPT) &&
        primary_pattern.MatchesAllHosts() &&
        secondary_pattern.MatchesAllHosts()) {
      Notify();
    }
  }

  base::RepeatingClosureList& observers() { return observers_; }
  base::DictValue& incognito_sites() { return incognito_sites_; }

  void Notify() { observers_.Notify(); }

 private:
  static constexpr int kKey = 0;

  raw_ptr<Profile> profile_;
  base::RepeatingClosureList observers_;
  base::DictValue incognito_sites_;
  PrefChangeRegistrar registrar_;
  PrefChangeRegistrar local_state_registrar_;
  base::ScopedObservation<HostContentSettingsMap, content_settings::Observer>
      settings_map_observation_{this};
};

const base::DictValue& SitesDict(Profile* profile) {
  if (profile->IsOffTheRecord()) {
    return ProfileBarrier::Get(profile).incognito_sites();
  }
  return profile->GetPrefs()->GetDict(kSitesPref);
}

// What sites get by default for scripts and third-party cookies: Chromium's
// own settings.
bool BlocksScriptsByDefault(Profile* profile) {
  return HostContentSettingsMapFactory::GetForProfile(profile)
             ->GetDefaultContentSetting(ContentSettingsType::JAVASCRIPT) ==
         CONTENT_SETTING_BLOCK;
}

bool BlocksThirdPartyCookiesByDefault(Profile* profile) {
  return static_cast<content_settings::CookieControlsMode>(
             profile->GetPrefs()->GetInteger(prefs::kCookieControlsMode)) ==
         content_settings::CookieControlsMode::kBlockThirdParty;
}

ContentSettingsPattern SitePattern(std::string_view site) {
  // Domains with their subdomains; IP addresses and hosts like localhost
  // alone.
  const bool domain =
      site.find('.') != std::string_view::npos &&
      !GURL(base::StrCat({"https://", site})).HostIsIPAddress();
  return ContentSettingsPattern::FromString(
      domain ? base::StrCat({"[*.]", site}) : std::string(site));
}

void ApplyOverrides(const base::DictValue& dict, Settings& settings) {
  if (std::optional<bool> enabled = dict.FindBool("enabled")) {
    settings.enabled = settings.enabled && *enabled;
  }
  if (auto ads = FromName(kAdBlockingNames, dict.FindString("ads"))) {
    settings.ads = *ads;
  }
  if (auto https = FromName(kHttpsNames, dict.FindString("https"))) {
    settings.https = *https;
  }
  if (std::optional<bool> scripts = dict.FindBool("scripts")) {
    settings.block_scripts = *scripts;
  }
  if (std::optional<bool> fingerprinting = dict.FindBool("fingerprinting")) {
    settings.fingerprinting = *fingerprinting;
  }
  if (const base::ListValue* exceptions =
          dict.FindList("fingerprinting_exceptions")) {
    settings.fingerprinting_exceptions = 0;
    for (const base::Value& name : *exceptions) {
      for (const auto& api : FingerprintingApis()) {
        if (name.is_string() && name.GetString() == api.name) {
          settings.fingerprinting_exceptions |= api.api;
        }
      }
    }
  }
  if (auto cookies = FromName(kCookiesNames, dict.FindString("cookies"))) {
    settings.cookies = *cookies;
  }
  if (std::optional<bool> forget = dict.FindBool("forget")) {
    settings.forget = *forget;
  }
}

void SyncContentSettings(Profile* profile, std::string_view site);

base::DictValue ToDict(const SiteOverrides& overrides) {
  base::DictValue dict;
  if (overrides.enabled) {
    dict.Set("enabled", *overrides.enabled);
  }
  if (overrides.ads) {
    dict.Set("ads", NameOf(kAdBlockingNames, *overrides.ads));
  }
  if (overrides.https) {
    dict.Set("https", NameOf(kHttpsNames, *overrides.https));
  }
  if (overrides.block_scripts) {
    dict.Set("scripts", *overrides.block_scripts);
  }
  if (overrides.fingerprinting) {
    dict.Set("fingerprinting", *overrides.fingerprinting);
  }
  if (overrides.fingerprinting_exceptions) {
    base::ListValue exceptions;
    for (const auto& api : FingerprintingApis()) {
      if (*overrides.fingerprinting_exceptions & api.api) {
        exceptions.Append(api.name);
      }
    }
    dict.Set("fingerprinting_exceptions", std::move(exceptions));
  }
  if (overrides.cookies) {
    dict.Set("cookies", NameOf(kCookiesNames, *overrides.cookies));
  }
  if (overrides.forget) {
    dict.Set("forget", *overrides.forget);
  }
  if (overrides.google_sign_in) {
    dict.Set("google_sign_in", *overrides.google_sign_in);
  }
  return dict;
}

// Keeps sites' scripts and cookies in step with the switch, in every profile:
// Chromium's exceptions for them are the Barrier's only while it's on.
class SwitchWatcher : public ProfileManagerObserver {
 public:
  static SwitchWatcher& Get() {
    static base::NoDestructor<SwitchWatcher> watcher;
    return *watcher;
  }

  void Start() {
    if (ProfileManager* manager = g_browser_process->profile_manager()) {
      observation_.Observe(manager);
      for (Profile* profile : manager->GetLoadedProfiles()) {
        Sync(profile);
      }
    }
    registrar_.Init(g_browser_process->local_state());
    registrar_.Add(kEnabledPref, base::BindRepeating(&SwitchWatcher::SyncAll,
                                                     base::Unretained(this)));
  }

  // ProfileManagerObserver:
  void OnProfileAdded(Profile* profile) override { Sync(profile); }
  void OnProfileManagerDestroying() override { observation_.Reset(); }

 private:
  void SyncAll() {
    for (Profile* profile :
         g_browser_process->profile_manager()->GetLoadedProfiles()) {
      Sync(profile);
    }
  }

  // Only when the profile's exceptions were made with the switch the other
  // way.
  static void Sync(Profile* profile) {
    PrefService* prefs = profile->GetPrefs();
    if (prefs->GetBoolean(kSyncedOnPref) == IsOn()) {
      return;
    }
    prefs->SetBoolean(kSyncedOnPref, IsOn());
    for (const std::string& site : SitesWithOverrides(profile)) {
      SyncContentSettings(profile, site);
    }
  }

  PrefChangeRegistrar registrar_;
  base::ScopedObservation<ProfileManager, ProfileManagerObserver> observation_{
      this};
};

// Chromium's settings for `site`'s scripts and third-party cookies, which
// Chromium enforces: what the Barrier says, as exceptions to Chromium's
// defaults where it differs.
void SyncContentSettings(Profile* profile, std::string_view site) {
  const GURL url(base::StrCat({"https://", site, "/"}));
  const Settings settings = GetSettings(profile, url);
  HostContentSettingsMap* map =
      HostContentSettingsMapFactory::GetForProfile(profile);

  const bool block_scripts = settings.enabled && settings.block_scripts;
  map->SetContentSettingCustomScope(
      SitePattern(site), ContentSettingsPattern::Wildcard(),
      ContentSettingsType::JAVASCRIPT,
      block_scripts == BlocksScriptsByDefault(profile)
          ? CONTENT_SETTING_DEFAULT
          : (block_scripts ? CONTENT_SETTING_BLOCK : CONTENT_SETTING_ALLOW));

  // Third-party cookies on the site: allowed against a default that blocks
  // them. (Chromium can't block them on one site only.)
  const bool allow_third_party =
      !settings.enabled || settings.cookies == Cookies::kAllowAll;
  scoped_refptr<content_settings::CookieSettings> cookie_settings =
      CookieSettingsFactory::GetForProfile(profile);
  if (allow_third_party && BlocksThirdPartyCookiesByDefault(profile)) {
    cookie_settings->SetThirdPartyCookieSetting(url, CONTENT_SETTING_ALLOW);
  } else {
    cookie_settings->ResetThirdPartyCookieSetting(url);
  }

  // Google's sign-in pages' cookies, when the site was allowed Google
  // sign-in.
  SyncGoogleSignInCookies(
      profile, site,
      settings.enabled &&
          GetSiteOverrides(profile, site).google_sign_in.value_or(false));
}

}  // namespace

const std::vector<FingerprintingApiName>& FingerprintingApis() {
  static const base::NoDestructor<std::vector<FingerprintingApiName>> apis({
      {kCanvas, "canvas"},
      {kAudio, "audio"},
      {kWebGl, "webgl"},
      {kWebGpu, "webgpu"},
      {kFonts, "fonts"},
      {kScreen, "screen"},
      {kHardware, "hardware"},
      {kMediaDevices, "media_devices"},
      {kSpeechSynthesis, "speech_synthesis"},
      {kKeyboard, "keyboard"},
      {kLanguages, "languages"},
      {kUsb, "usb"},
  });
  return *apis;
}

bool IsOn() {
  PrefService* local_state = g_browser_process->local_state();
  return !local_state || local_state->GetBoolean(kEnabledPref);
}

bool WasOnAtStartup() {
  if (!g_on_at_startup) {
    PrefService* local_state = g_browser_process->local_state();
    if (!local_state) {
      return true;
    }
    g_on_at_startup = local_state->GetBoolean(kEnabledPref);
  }
  return *g_on_at_startup;
}

Settings GetDefaults(Profile* profile) {
  PrefService* prefs = profile->GetOriginalProfile()->GetPrefs();
  Settings settings;
  settings.enabled = IsOn();
  settings.ads = FromName(kAdBlockingNames, &prefs->GetString(kAdsPref))
                     .value_or(AdBlocking::kStandard);
  settings.https = FromName(kHttpsNames, &prefs->GetString(kHttpsPref))
                       .value_or(HttpsUpgrade::kStandard);
  settings.block_scripts = BlocksScriptsByDefault(profile);
  settings.fingerprinting = prefs->GetBoolean(kFingerprintingPref);
  settings.cookies = BlocksThirdPartyCookiesByDefault(profile)
                         ? Cookies::kBlockThirdParty
                         : Cookies::kAllowAll;
  settings.forget = prefs->GetBoolean(kForgetPref);
  return settings;
}

namespace {

// The hosts of Brave's list of sites not upgraded to HTTPS.
std::set<std::string, std::less<>> ParseHttpsUpgradeExceptions(
    std::string_view text) {
  std::set<std::string, std::less<>> hosts;
  for (std::string_view line : base::SplitStringPiece(
           text, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (!line.starts_with('#')) {
      hosts.emplace(line);
    }
  }
  return hosts;
}

}  // namespace

HttpsUpgrade HttpsUpgradeFor(Profile* profile, const GURL& url) {
  // Whether its host is one Brave doesn't upgrade, standard or not.
  static base::NoDestructor<data_lists::Parsed<
      std::set<std::string, std::less<>>, &ParseHttpsUpgradeExceptions>>
      parsed(data_lists::DataList::kHttpsUpgradeExceptions);
  const auto exceptions = parsed->Get();
  const Settings settings = GetSettings(profile, url);
  if (!settings.enabled) {
    return HttpsUpgrade::kDisabled;
  }
  if (settings.https == HttpsUpgrade::kStandard &&
      exceptions->contains(url.host())) {
    return HttpsUpgrade::kDisabled;
  }
  return settings.https;
}

Settings GetSettings(Profile* profile, const GURL& url) {
  Settings settings = GetDefaults(profile);
  const std::string site = SiteOf(url);
  if (site.empty()) {
    // Not a web page: none of the Barrier's business.
    settings.enabled = false;
    return settings;
  }
  // An incognito profile's sites have the settings of its own profile's, and
  // their changes in it.
  if (profile->IsOffTheRecord()) {
    if (const base::DictValue* dict =
            SitesDict(profile->GetOriginalProfile()).FindDict(site)) {
      ApplyOverrides(*dict, settings);
    }
  }
  if (const base::DictValue* dict = SitesDict(profile).FindDict(site)) {
    ApplyOverrides(*dict, settings);
  }
  return settings;
}

bool SiteOverrides::empty() const {
  return !enabled && !ads && !https && !block_scripts && !fingerprinting &&
         !fingerprinting_exceptions && !cookies && !forget && !google_sign_in;
}

SiteOverrides GetSiteOverrides(Profile* profile, std::string_view site) {
  SiteOverrides overrides;
  auto read = [&](const base::DictValue& dict) {
    if (std::optional<bool> enabled = dict.FindBool("enabled")) {
      overrides.enabled = enabled;
    }
    if (auto ads = FromName(kAdBlockingNames, dict.FindString("ads"))) {
      overrides.ads = ads;
    }
    if (auto https = FromName(kHttpsNames, dict.FindString("https"))) {
      overrides.https = https;
    }
    if (std::optional<bool> scripts = dict.FindBool("scripts")) {
      overrides.block_scripts = scripts;
    }
    if (std::optional<bool> fp = dict.FindBool("fingerprinting")) {
      overrides.fingerprinting = fp;
    }
    if (dict.FindList("fingerprinting_exceptions")) {
      Settings settings;
      ApplyOverrides(dict, settings);
      overrides.fingerprinting_exceptions = settings.fingerprinting_exceptions;
    }
    if (auto cookies = FromName(kCookiesNames, dict.FindString("cookies"))) {
      overrides.cookies = cookies;
    }
    if (std::optional<bool> forget = dict.FindBool("forget")) {
      overrides.forget = forget;
    }
    if (std::optional<bool> google = dict.FindBool("google_sign_in")) {
      overrides.google_sign_in = google;
    }
  };
  if (profile->IsOffTheRecord()) {
    if (const base::DictValue* dict =
            SitesDict(profile->GetOriginalProfile()).FindDict(site)) {
      read(*dict);
    }
  }
  if (const base::DictValue* dict = SitesDict(profile).FindDict(site)) {
    read(*dict);
  }
  return overrides;
}

void SetSiteOverrides(Profile* profile,
                      std::string_view site,
                      const SiteOverrides& overrides) {
  if (site.empty()) {
    return;
  }
  if (profile->IsOffTheRecord()) {
    ProfileBarrier& barrier = ProfileBarrier::Get(profile);
    barrier.incognito_sites().Set(site, ToDict(overrides));
    barrier.Notify();
    return;
  }
  {
    ScopedDictPrefUpdate update(profile->GetPrefs(), kSitesPref);
    if (overrides.empty()) {
      update->Remove(site);
    } else {
      update->Set(site, ToDict(overrides));
    }
  }
  SyncContentSettings(profile, site);
}

std::vector<std::string> SitesWithOverrides(Profile* profile) {
  std::vector<std::string> sites;
  for (const auto [site, value] : SitesDict(profile)) {
    sites.push_back(site);
  }
  return sites;
}

std::string SiteOf(const GURL& url) {
  if (!url.SchemeIsHTTPOrHTTPS() || !url.has_host()) {
    return std::string();
  }
  std::string site = net::registry_controlled_domains::GetDomainAndRegistry(
      url, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  return site.empty() ? std::string(url.host()) : site;
}

bool IsListUrlAllowed(const GURL& url) {
  return url.SchemeIs(url::kHttpsScheme) ||
         (url.SchemeIs(url::kHttpScheme) && net::IsLocalhost(url));
}

base::CallbackListSubscription ObserveSettings(Profile* profile,
                                               base::RepeatingClosure changed) {
  return ProfileBarrier::Get(profile).observers().Add(std::move(changed));
}

bool IsPrivacyOn(Profile* profile, const char* pref) {
  return IsOn() && profile->GetPrefs()->GetBoolean(pref);
}

void AddToBlockedTotal(int count) {
  if (PrefService* local_state = g_browser_process->local_state()) {
    local_state->SetInt64(kBlockedTotalPref,
                          local_state->GetInt64(kBlockedTotalPref) + count);
  }
}

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterStringPref(kAdsPref, "standard");
  registry->RegisterStringPref(kHttpsPref, "standard");
  registry->RegisterBooleanPref(kFingerprintingPref, true);
  registry->RegisterBooleanPref(kForgetPref, false);
  registry->RegisterDictionaryPref(kSitesPref);
  registry->RegisterBooleanPref(kShowCountPref, true);
  registry->RegisterBooleanPref(kGpcPref, true);
  registry->RegisterBooleanPref(kStripParamsPref, true);
  registry->RegisterBooleanPref(kDebouncePref, true);
  registry->RegisterBooleanPref(kDeAmpPref, true);
  registry->RegisterBooleanPref(kReduceLanguagePref, true);
  registry->RegisterStringPref(kSensitiveSitesPref, "ask");
  registry->RegisterBooleanPref(kGoogleSignInPref, true);
  registry->RegisterBooleanPref(kSyncedOnPref, true);
}

void RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kEnabledPref, true);
  registry->RegisterBooleanPref(kFacebookEmbedsPref, true);
  registry->RegisterBooleanPref(kXEmbedsPref, true);
  registry->RegisterBooleanPref(kLinkedInEmbedsPref, false);
  filter_lists::RegisterLocalStatePrefs(registry);
  data_lists::RegisterLocalStatePrefs(registry);
  registry->RegisterInt64Pref(kBlockedTotalPref, 0);
  for (const char* pref : kStartupPrefs) {
    registry->RegisterBooleanPref(pref, true);
  }
}

void RegisterChromiumDefaults(user_prefs::PrefRegistrySyncable* registry) {
  if (!WasOnAtStartup()) {
    return;
  }
  // Third-party cookies blocked, as Brave does: Chromium's own setting
  // (Settings > Privacy too), unless it was changed.
  registry->SetDefaultPrefValue(
      prefs::kCookieControlsMode,
      base::Value(static_cast<int>(
          content_settings::CookieControlsMode::kBlockThirdParty)));
}

bool StartupProtectionsChanged() {
  PrefService* local_state = g_browser_process->local_state();
  if (local_state->GetBoolean(kEnabledPref) != WasOnAtStartup()) {
    return true;
  }
  return std::ranges::any_of(kStartupPrefs, [&](const char* pref) {
    const auto it = StartupValues().find(pref);
    return it != StartupValues().end() &&
           local_state->GetBoolean(pref) != it->second;
  });
}

void AddFeatureOverrides(
    PrefService* local_state,
    std::vector<base::FeatureList::FeatureOverrideInfo>& overrides) {
  g_on_at_startup = local_state->GetBoolean(kEnabledPref);
  for (const char* pref : kStartupPrefs) {
    StartupValues()[pref] = local_state->GetBoolean(pref);
  }
  auto set = [&](const base::Feature& feature, bool enabled) {
    overrides.emplace_back(
        std::cref(feature),
        enabled ? base::FeatureList::OVERRIDE_ENABLE_FEATURE
                : base::FeatureList::OVERRIDE_DISABLE_FEATURE);
  };
  // Each of these protections with the Barrier on, unless turned off.
  const auto on = [&](const char* pref) {
    return *g_on_at_startup && local_state->GetBoolean(pref);
  };
  set(net::features::kObladaReferrerCap, on(kReferrerCapPref));
  set(net::features::kObladaCookieExpiryCap, on(kCookieExpiryCapPref));
  // HSTS apart for each site, as Brave: other sites' pages can't read a
  // site's back as a cookie.
  set(net::features::kObladaPartitionHsts, on(kPartitionHstsPref));
  if (!*g_on_at_startup) {
    return;
  }
  // Global Privacy Control, which Settings turns on or off (kGpcPref).
  set(blink::features::kGlobalPrivacyControlTest, true);
  // Connections apart for each site, as their caches already are.
  if (on(kPartitionConnectionsPref)) {
    set(net::features::kPartitionConnectionsByNetworkIsolationKey, true);
  }
  if (!on(kWebApisOffPref)) {
    return;
  }
  // Web APIs, as Brave has them off.
  for (const base::Feature* feature : {
           &features::kWebBluetooth,
           &features::kFedCm,
           &features::kWebOTP,
           &features::kWebIdentityDigitalCredentials,
           &features::kInstalledApp,
           &features::kInstalledAppProvider,
           &features::kDigitalGoodsApi,
           &blink::features::kFileSystemAccessLocal,
           &blink::features::kWebSerialAPI,
           &blink::features::kFencedFrames,
           &blink::features::kFledge,
           &attribution_reporting::features::kConversionMeasurement,
           &network::features::kBrowsingTopics,
           &network::features::kReporting,
           &network::features::kNetworkErrorLogging,
       }) {
    set(*feature, false);
  }
}

void Init() {
  SwitchWatcher::Get().Start();
  InitDomainBlock();
  filter_lists::Start();
  data_lists::Start();
}

}  // namespace oblada::barrier
