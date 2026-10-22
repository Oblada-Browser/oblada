#ifndef OBLADA_BROWSER_CLEAN_LINKS_MOZILLA_LIST_H_
#define OBLADA_BROWSER_CLEAN_LINKS_MOZILLA_LIST_H_

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>

class PrefRegistrySimple;

// Mozilla's list of tracking parameters, which Firefox strips from pages'
// addresses as they load and adds to the global rule of its lists for copying
// (its Remote Settings collection "query-stripping"). As in Firefox, it's built
// in (lists/mozilla/), then kept up to date from Mozilla's server: checked a
// minute after the browser starts if it wasn't in the last day, then daily. The
// last one downloaded is kept in Local State, and used instead of the one built
// in.
namespace oblada::clean_links {

using Names = std::set<std::string, std::less<>>;

void RegisterMozillaListPrefs(PrefRegistrySimple* registry);

// Starts keeping the list up to date.
void StartMozillaListUpdates();

// The list's parameters, in lowercase.
const Names& MozillaListParams();

// The parameters of a Remote Settings answer for the collection (`json`), none
// if it isn't one.
std::optional<Names> ParseMozillaList(std::string_view json);

}  // namespace oblada::clean_links

#endif  // OBLADA_BROWSER_CLEAN_LINKS_MOZILLA_LIST_H_
