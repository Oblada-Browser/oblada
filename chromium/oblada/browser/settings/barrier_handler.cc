#include "oblada/browser/settings/barrier_handler.h"

#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/i18n/case_conversion.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/profiles/profile.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_ui.h"
#include "oblada/browser/barrier/barrier.h"
#include "oblada/browser/barrier/filter_lists.h"
#include "oblada/browser/l10n.h"
#include "ui/base/l10n/l10n_util.h"

namespace oblada::settings {

namespace {

// Oblada's names for the catalog's lists, by the start of their uuid; the
// catalog's own (in English) for the others.
constexpr std::string_view kTranslatedLists[] = {
    "ac023d22", "67e792d4", "6b91e355", "690ff3b4", "2f3dce16", "7911a1cb",
    "9e8ec586", "5c6a6c95", "2d57aded", "7f11b964", "d579f370", "49958da7",
    "bd308b90", "3afa9d35", "e2fa7d98", "1ed1870b", "78672887", "f61d6b7b",
    "cc98e4ba", "564c3b75",
};

std::string ListTitle(const barrier::filter_lists::ListInfo& list) {
  const std::string id = base::ToLowerASCII(list.uuid.substr(0, 8));
  for (std::string_view translated : kTranslatedLists) {
    if (id == translated) {
      return L10nUTF8(base::StrCat({"barrier.list.", id}));
    }
  }
  return list.title;
}

double ToJsTime(base::Time time) {
  return time.is_null() ? 0 : time.InMillisecondsFSinceUnixEpoch();
}

}  // namespace

BarrierHandler::BarrierHandler(Profile* profile) : profile_(profile) {}

BarrierHandler::~BarrierHandler() = default;

void BarrierHandler::RegisterMessages() {
  auto on = [this](const char* message,
                   void (BarrierHandler::*handler)(const base::ListValue&)) {
    web_ui()->RegisterMessageCallback(
        message, base::BindRepeating(handler, base::Unretained(this)));
  };
  on("obladaBarrierState", &BarrierHandler::HandleGetState);
  on("obladaBarrierSetEnabled", &BarrierHandler::HandleSetEnabled);
  on("obladaBarrierRelaunch", &BarrierHandler::HandleRelaunch);
  on("obladaBarrierSetBlockScripts", &BarrierHandler::HandleSetBlockScripts);
  on("obladaBarrierLists", &BarrierHandler::HandleGetLists);
  on("obladaBarrierSetList", &BarrierHandler::HandleSetList);
  on("obladaBarrierSubscribe", &BarrierHandler::HandleSubscribe);
  on("obladaBarrierUnsubscribe", &BarrierHandler::HandleUnsubscribe);
  on("obladaBarrierSetSubscription", &BarrierHandler::HandleSetSubscription);
  on("obladaBarrierSetCustomFilters", &BarrierHandler::HandleSetCustomFilters);
  on("obladaBarrierUpdateLists", &BarrierHandler::HandleUpdateLists);
  on("obladaBarrierSites", &BarrierHandler::HandleGetSites);
  on("obladaBarrierResetSite", &BarrierHandler::HandleResetSite);
}

void BarrierHandler::OnJavascriptAllowed() {
  lists_subscription_ = barrier::filter_lists::ObserveLists(base::BindRepeating(
      &BarrierHandler::OnListsChanged, base::Unretained(this)));
  local_state_registrar_.Init(g_browser_process->local_state());
  for (const char* pref :
       {barrier::kEnabledPref, barrier::kWebApisOffPref,
        barrier::kReferrerCapPref, barrier::kCookieExpiryCapPref,
        barrier::kPartitionConnectionsPref, barrier::kPartitionHstsPref}) {
    local_state_registrar_.Add(
        pref, base::BindRepeating(&BarrierHandler::OnStateChanged,
                                  base::Unretained(this)));
  }
}

void BarrierHandler::OnJavascriptDisallowed() {
  lists_subscription_ = {};
  local_state_registrar_.RemoveAll();
}

void BarrierHandler::OnStateChanged() {
  FireWebUIListener("oblada-barrier-state-changed", State());
}

void BarrierHandler::OnListsChanged() {
  FireWebUIListener("oblada-barrier-lists-changed", Lists());
}

base::DictValue BarrierHandler::State() {
  return base::DictValue()
      .Set("on", barrier::IsOn())
      .Set("onAtStartup", barrier::WasOnAtStartup())
      .Set("startupChanged", barrier::StartupProtectionsChanged())
      .Set("blockedTotal",
           static_cast<double>(g_browser_process->local_state()->GetInt64(
               barrier::kBlockedTotalPref)))
      .Set("blockScripts", barrier::GetDefaults(profile_).block_scripts);
}

void BarrierHandler::HandleGetState(const base::ListValue& args) {
  AllowJavascript();
  ResolveJavascriptCallback(args[0], State());
}

void BarrierHandler::HandleSetEnabled(const base::ListValue& args) {
  AllowJavascript();
  g_browser_process->local_state()->SetBoolean(barrier::kEnabledPref,
                                               args[1].GetBool());
  ResolveJavascriptCallback(args[0], State());
}

void BarrierHandler::HandleRelaunch(const base::ListValue& args) {
  chrome::AttemptRelaunch();
}

void BarrierHandler::HandleSetBlockScripts(const base::ListValue& args) {
  HostContentSettingsMapFactory::GetForProfile(profile_)
      ->SetDefaultContentSetting(ContentSettingsType::JAVASCRIPT,
                                 args[0].GetBool() ? CONTENT_SETTING_BLOCK
                                                   : CONTENT_SETTING_ALLOW);
}

base::DictValue BarrierHandler::Lists() {
  namespace lists = barrier::filter_lists;
  const std::string locale = g_browser_process->GetApplicationLocale();
  base::ListValue catalog;
  for (const lists::ListInfo& list : lists::GetLists()) {
    std::string title = ListTitle(list);
    if (list.regional()) {
      // Its languages' names, as the browser's language says them.
      std::vector<std::u16string> names;
      for (const std::string& lang : list.langs) {
        std::u16string name =
            l10n_util::GetDisplayNameForLocale(lang, locale, true);
        if (!name.empty()) {
          name.replace(0, 1, base::i18n::ToUpper(name.substr(0, 1)));
          names.push_back(std::move(name));
        }
      }
      if (!names.empty()) {
        title = base::UTF16ToUTF8(base::JoinString(names, u", "));
      }
    }
    catalog.Append(base::DictValue()
                       .Set("uuid", list.uuid)
                       .Set("title", title)
                       .Set("description", list.description)
                       .Set("enabled", list.enabled)
                       .Set("regional", list.regional())
                       .Set("updated", ToJsTime(list.updated)));
  }
  base::ListValue subscriptions;
  for (const lists::Subscription& subscription : lists::GetSubscriptions()) {
    subscriptions.Append(base::DictValue()
                             .Set("url", subscription.url)
                             .Set("enabled", subscription.enabled)
                             .Set("title", subscription.title)
                             .Set("updated", ToJsTime(subscription.updated))
                             .Set("failed", subscription.failed));
  }
  return base::DictValue()
      .Set("lists", std::move(catalog))
      .Set("subscriptions", std::move(subscriptions))
      .Set("customFilters", lists::GetCustomFilters());
}

void BarrierHandler::HandleGetLists(const base::ListValue& args) {
  AllowJavascript();
  ResolveJavascriptCallback(args[0], Lists());
}

void BarrierHandler::HandleSetList(const base::ListValue& args) {
  barrier::filter_lists::SetListEnabled(args[0].GetString(), args[1].GetBool());
}

void BarrierHandler::HandleSubscribe(const base::ListValue& args) {
  AllowJavascript();
  const bool ok = barrier::filter_lists::Subscribe(args[1].GetString());
  ResolveJavascriptCallback(args[0], base::Value(ok));
}

void BarrierHandler::HandleUnsubscribe(const base::ListValue& args) {
  barrier::filter_lists::Unsubscribe(args[0].GetString());
}

void BarrierHandler::HandleSetSubscription(const base::ListValue& args) {
  barrier::filter_lists::SetSubscriptionEnabled(args[0].GetString(),
                                                args[1].GetBool());
}

void BarrierHandler::HandleSetCustomFilters(const base::ListValue& args) {
  barrier::filter_lists::SetCustomFilters(args[0].GetString());
}

void BarrierHandler::HandleUpdateLists(const base::ListValue& args) {
  barrier::filter_lists::UpdateNow();
}

void BarrierHandler::HandleGetSites(const base::ListValue& args) {
  AllowJavascript();
  base::ListValue sites;
  for (const std::string& site : barrier::SitesWithOverrides(profile_)) {
    const barrier::SiteOverrides overrides =
        barrier::GetSiteOverrides(profile_, site);
    base::ListValue changes;
    if (overrides.enabled) {
      changes.Append(*overrides.enabled ? "enabled" : "disabled");
    }
    if (overrides.ads) {
      changes.Append(*overrides.ads == barrier::AdBlocking::kAggressive
                         ? "ads_aggressive"
                     : *overrides.ads == barrier::AdBlocking::kStandard
                         ? "ads_standard"
                         : "ads_disabled");
    }
    if (overrides.https) {
      changes.Append(*overrides.https == barrier::HttpsUpgrade::kStrict
                         ? "https_strict"
                     : *overrides.https == barrier::HttpsUpgrade::kStandard
                         ? "https_standard"
                         : "https_disabled");
    }
    if (overrides.block_scripts) {
      changes.Append(*overrides.block_scripts ? "scripts_blocked"
                                              : "scripts_allowed");
    }
    if (overrides.fingerprinting) {
      changes.Append(*overrides.fingerprinting ? "fingerprinting_blocked"
                                               : "fingerprinting_allowed");
    }
    if (overrides.cookies) {
      changes.Append(*overrides.cookies == barrier::Cookies::kAllowAll
                         ? "cookies_allowed"
                         : "cookies_blocked");
    }
    if (overrides.forget) {
      changes.Append(*overrides.forget ? "forget" : "remember");
    }
    if (overrides.google_sign_in) {
      changes.Append(*overrides.google_sign_in ? "google_sign_in_allowed"
                                               : "google_sign_in_blocked");
    }
    sites.Append(base::DictValue()
                     .Set("site", site)
                     .Set("changes", std::move(changes)));
  }
  ResolveJavascriptCallback(args[0], std::move(sites));
}

void BarrierHandler::HandleResetSite(const base::ListValue& args) {
  barrier::SetSiteOverrides(profile_, args[0].GetString(),
                            barrier::SiteOverrides());
}

}  // namespace oblada::settings
