#ifndef OBLADA_BROWSER_BARRIER_FILTER_LISTS_H_
#define OBLADA_BROWSER_BARRIER_FILTER_LISTS_H_

#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback_forward.h"
#include "base/time/time.h"

class PrefRegistrySimple;

// The filter lists the Barrier blocks trackers and ads with, the same for
// every profile, as Brave's: those of Brave's catalog (lists/list_catalog.json,
// kept up to date from Brave's repository), each downloaded from its own
// sources (EasyList, uBlock Origin's lists, Brave's…); lists subscribed to by
// address; and the user's own filters. Some of the catalog's are always on
// (Brave's defaults, hidden), some on unless turned off (cookie notices, app
// promos), some off unless turned on (annoyances, regional lists: the
// browser's language's is turned on once, as Brave does). Lists are
// downloaded again as they say (`! Expires:`), daily otherwise; the engines
// (engine_host.h) are compiled from them in the background, and kept on disk
// ("Oblada Barrier" in the user data directory) for the next start.
//
// For tests, --oblada-barrier-catalog=<URL> takes the catalog from there
// rather than Brave's (its lists' sources can be local too), at once.
namespace oblada::barrier::filter_lists {

void RegisterLocalStatePrefs(PrefRegistrySimple* registry);

// Loads the engines and starts keeping the lists up to date.
void Start();

// A list of the catalog, for Settings.
struct ListInfo {
  std::string uuid;
  std::string title;
  std::string description;
  // Turned on by default (or by the browser's language).
  bool default_enabled = false;
  bool enabled = false;
  // A regional list's languages ("it"…), which it's shown by; empty for
  // others.
  std::vector<std::string> langs;
  bool regional() const { return !langs.empty(); }
  // When its sources were last all downloaded; null if not yet.
  base::Time updated;
};
// The catalog's lists users can turn on or off.
std::vector<ListInfo> GetLists();
void SetListEnabled(const std::string& uuid, bool enabled);

// A list subscribed to by address.
struct Subscription {
  std::string url;
  bool enabled = true;
  // From its header (`! Title:`), once downloaded.
  std::string title;
  base::Time updated;
  // The last download failed.
  bool failed = false;
};
std::vector<Subscription> GetSubscriptions();
// False if `url` isn't one to download from (http or https).
bool Subscribe(const std::string& url);
void Unsubscribe(const std::string& url);
void SetSubscriptionEnabled(const std::string& url, bool enabled);

// The user's own filters, one per line.
std::string GetCustomFilters();
void SetCustomFilters(const std::string& filters);
// Adds `filter` to them (the element picker's).
void AddCustomFilter(const std::string& filter);

// Downloads every list now.
void UpdateNow();

// Notifies of changes to the lists or their state (downloaded, failed).
base::CallbackListSubscription ObserveLists(base::RepeatingClosure changed);

}  // namespace oblada::barrier::filter_lists

#endif  // OBLADA_BROWSER_BARRIER_FILTER_LISTS_H_
