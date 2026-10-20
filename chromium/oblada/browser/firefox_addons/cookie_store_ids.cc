#include "oblada/browser/firefox_addons/cookie_store_ids.h"

#include <algorithm>
#include <map>
#include <optional>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/extensions/api/cookies.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_contents.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest.h"
#include "oblada/browser/containers/containers.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"

namespace oblada::firefox_addons {

namespace {

constexpr std::string_view kContainerPrefix = "firefox-container-";

// Set by xpi_conversion.cc on the add-ons it converts.
constexpr char kFirefoxAddonKey[] = "firefox_addon";

struct ParsedStoreId {
  enum Kind { kDefault, kPrivate, kContainer, kInvalid } kind = kInvalid;
  int number = 0;
};

// "firefox-default", "firefox-private", "firefox-container-<n>", the same with any
// other prefix, or Chrome's "0" and "1".
ParsedStoreId ParseStoreId(std::string_view id) {
  if (id == "0") {
    return {ParsedStoreId::kDefault};
  }
  if (id == "1") {
    return {ParsedStoreId::kPrivate};
  }
  if (id.ends_with("-default")) {
    return {ParsedStoreId::kDefault};
  }
  if (id.ends_with("-private")) {
    return {ParsedStoreId::kPrivate};
  }
  constexpr std::string_view kContainer = "-container-";
  const size_t at = id.rfind(kContainer);
  int number = 0;
  if (at != std::string_view::npos &&
      base::StringToInt(id.substr(at + kContainer.size()), &number) && number > 0) {
    return {ParsedStoreId::kContainer, number};
  }
  return {};
}

// Whether `scripts` start with the compat script, which the conversion puts first
// (whatever its file was called when the add-on was converted).
bool LoadsCompatScript(const base::ListValue* scripts) {
  return scripts && !scripts->empty() && scripts->front().is_string() &&
         scripts->front().GetString().ends_with("-compat.js");
}

}  // namespace

bool IsFirefoxAddon(const extensions::Extension* extension) {
  if (!extension) {
    return false;
  }
  const base::DictValue& manifest = *extension->manifest()->value();
  if (manifest.FindBool(kFirefoxAddonKey).value_or(false)) {
    return true;
  }
  // Converted before they were marked: they load the compat script.
  if (LoadsCompatScript(manifest.FindListByDottedPath("background.scripts"))) {
    return true;
  }
  if (const std::string* worker =
          manifest.FindStringByDottedPath("background.service_worker");
      worker && *worker == "oblada-background.js") {
    return true;
  }
  if (const base::ListValue* content_scripts = manifest.FindList("content_scripts")) {
    for (const base::Value& entry : *content_scripts) {
      if (entry.is_dict() && LoadsCompatScript(entry.GetDict().FindList("js"))) {
        return true;
      }
    }
  }
  return false;
}

std::string CookieStoreIdOf(const containers::Container& container) {
  return base::StrCat({kContainerPrefix, base::NumberToString(container.number)});
}

std::string CookieStoreIdOf(content::WebContents* contents) {
  Profile* profile = Profile::FromBrowserContext(contents->GetBrowserContext());
  const std::string id = containers::GetContainerId(contents);
  if (!id.empty()) {
    if (std::optional<containers::Container> container =
            containers::FindContainer(profile, id)) {
      return CookieStoreIdOf(*container);
    }
  }
  return profile->IsOffTheRecord() ? kPrivateCookieStoreId : kDefaultCookieStoreId;
}

base::expected<std::string, std::string> ContainerOfCookieStoreId(
    Profile* profile,
    std::string_view cookie_store_id) {
  const ParsedStoreId parsed = ParseStoreId(cookie_store_id);
  switch (parsed.kind) {
    case ParsedStoreId::kDefault:
      if (profile->IsOffTheRecord()) {
        return base::unexpected(
            "Illegal to set non-private cookieStoreId in a private window");
      }
      return base::ok(std::string());
    case ParsedStoreId::kPrivate:
      if (!profile->IsOffTheRecord()) {
        return base::unexpected(
            "Illegal to set private cookieStoreId in a non-private window");
      }
      return base::ok(std::string());
    case ParsedStoreId::kContainer:
      if (std::optional<containers::Container> container =
              containers::FindContainerByNumber(profile, parsed.number)) {
        return base::ok(container->id);
      }
      break;
    case ParsedStoreId::kInvalid:
      break;
  }
  return base::unexpected(
      base::StrCat({"No cookie store exists with ID ", cookie_store_id}));
}

network::mojom::CookieManager* CookieManagerForStoreId(
    content::BrowserContext* context,
    bool include_incognito,
    std::string_view store_id) {
  // Chrome's own ids are the cookies API's business.
  if (store_id == "0" || store_id == "1") {
    return nullptr;
  }
  Profile* profile = Profile::FromBrowserContext(context)->GetOriginalProfile();
  const ParsedStoreId parsed = ParseStoreId(store_id);
  switch (parsed.kind) {
    case ParsedStoreId::kDefault:
      return profile->GetDefaultStoragePartition()->GetCookieManagerForBrowserProcess();
    case ParsedStoreId::kPrivate:
      if (!include_incognito || !profile->HasPrimaryOTRProfile()) {
        return nullptr;
      }
      return profile->GetPrimaryOTRProfile(/*create_if_needed=*/false)
          ->GetDefaultStoragePartition()
          ->GetCookieManagerForBrowserProcess();
    case ParsedStoreId::kContainer:
      if (std::optional<containers::Container> container =
              containers::FindContainerByNumber(profile, parsed.number)) {
        return profile
            ->GetStoragePartition(containers::PartitionFor(profile, container->id))
            ->GetCookieManagerForBrowserProcess();
      }
      return nullptr;
    case ParsedStoreId::kInvalid:
      return nullptr;
  }
}

std::vector<extensions::api::cookies::CookieStore> FirefoxCookieStores(
    Profile* profile,
    bool include_incognito) {
  std::map<std::string, std::vector<int>> tabs;
  tabs[kDefaultCookieStoreId];
  for (BrowserWindowInterface* browser : GetAllBrowserWindowInterfaces()) {
    Profile* browser_profile = browser->GetProfile();
    if (browser_profile->GetOriginalProfile() != profile->GetOriginalProfile() ||
        (browser_profile->IsOffTheRecord() && !include_incognito)) {
      continue;
    }
    TabStripModel* tab_strip = browser->GetTabStripModel();
    for (int i = 0; tab_strip && i < tab_strip->count(); ++i) {
      content::WebContents* contents = tab_strip->GetWebContentsAt(i);
      tabs[CookieStoreIdOf(contents)].push_back(
          extensions::ExtensionTabUtil::GetTabId(contents));
    }
  }
  std::vector<extensions::api::cookies::CookieStore> stores;
  // The default store first: add-ons take it for an example of the ids' form.
  for (auto& [id, tab_ids] : tabs) {
    extensions::api::cookies::CookieStore store;
    store.id = id;
    store.tab_ids = std::move(tab_ids);
    if (id == kDefaultCookieStoreId) {
      stores.insert(stores.begin(), std::move(store));
    } else {
      stores.push_back(std::move(store));
    }
  }
  return stores;
}

}  // namespace oblada::firefox_addons
