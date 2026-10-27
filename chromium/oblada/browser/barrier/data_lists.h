#ifndef OBLADA_BROWSER_BARRIER_DATA_LISTS_H_
#define OBLADA_BROWSER_BARRIER_DATA_LISTS_H_

#include <memory>
#include <string>
#include <string_view>

#include "base/synchronization/lock.h"
#include "base/thread_annotations.h"

class PrefRegistrySimple;

// Brave's data lists the Barrier works from (lists/README.md): tracking
// parameters, tracking redirects, sensitive sites, fingerprinting protection's
// exceptions, sites not upgraded to HTTPS. Oblada has them built in, and keeps
// them up to date from Brave's repository (brave/adblock-lists' brave-lists),
// daily, keeping the latest in the user data directory ("Oblada Barrier/data")
// for the next start, as the filter lists (filter_lists.h).
//
// For tests, --oblada-barrier-data=<URL> downloads them from there (<URL> +
// the list's file name) at once; with --oblada-barrier-catalog and without it,
// they aren't downloaded.
namespace oblada::barrier::data_lists {

enum class DataList {
  kQueryFilter,
  kDebounce,
  kSensitiveSites,
  kWebcompatExceptions,
  kHttpsUpgradeExceptions,
};

// The list's text: the latest downloaded, or the one built in. On any
// sequence.
std::string GetText(DataList list);
// Changes each time the list does. On any sequence.
int GetVersion(DataList list);

void RegisterLocalStatePrefs(PrefRegistrySimple* registry);

// Reads the lists kept on disk, and starts keeping them up to date.
void Start();

// A list as its users read it: parsed by `Parse` (from its text into a `T`)
// the first time it's asked for, and again once it changed. On any sequence.
template <typename T, T (*Parse)(std::string_view)>
class Parsed {
 public:
  explicit Parsed(DataList list) : list_(list) {}
  Parsed(const Parsed&) = delete;
  Parsed& operator=(const Parsed&) = delete;

  std::shared_ptr<const T> Get() {
    const int version = GetVersion(list_);
    base::AutoLock lock(lock_);
    if (!parsed_ || version != version_) {
      parsed_ = std::make_shared<const T>(Parse(GetText(list_)));
      version_ = version;
    }
    return parsed_;
  }

 private:
  const DataList list_;
  base::Lock lock_;
  std::shared_ptr<const T> parsed_ GUARDED_BY(lock_);
  int version_ GUARDED_BY(lock_) = -1;
};

}  // namespace oblada::barrier::data_lists

#endif  // OBLADA_BROWSER_BARRIER_DATA_LISTS_H_
