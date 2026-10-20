#ifndef OBLADA_BROWSER_FIREFOX_ADDONS_COOKIE_STORE_IDS_H_
#define OBLADA_BROWSER_FIREFOX_ADDONS_COOKIE_STORE_IDS_H_

// Firefox names a tab's container by a "cookieStoreId": "firefox-default" (none),
// "firefox-private" (private browsing) or "firefox-container-<number>". Firefox
// add-ons see Oblada's containers by these names: in tabs (tabs_api.cc,
// extension_tab_util.cc) and in the contextualIdentities API.

#include <string>
#include <string_view>
#include <vector>

#include "base/types/expected.h"

class Profile;

namespace content {
class BrowserContext;
class WebContents;
}  // namespace content

namespace extensions {
class Extension;
namespace api::cookies {
struct CookieStore;
}  // namespace api::cookies
}  // namespace extensions

namespace network::mojom {
class CookieManager;
}  // namespace network::mojom

namespace oblada::containers {
struct Container;
}  // namespace oblada::containers

namespace oblada::firefox_addons {

inline constexpr char kDefaultCookieStoreId[] = "firefox-default";
inline constexpr char kPrivateCookieStoreId[] = "firefox-private";

std::string CookieStoreIdOf(const containers::Container& container);
std::string CookieStoreIdOf(content::WebContents* contents);

// Whether `extension` is a Firefox add-on (converted by xpi_conversion.cc). Only those
// see Firefox's names: Chrome extensions get tabs and cookie stores as in Chrome
// (some, written for both browsers, would take them for Firefox's).
bool IsFirefoxAddon(const extensions::Extension* extension);

// The container `cookie_store_id` names (its id; empty for no container) in
// `profile`, or an error message. Like Firefox's, but also "<anything>-default",
// "-private" and "-container-<n>" (as add-ons written for both browsers make them from
// Chrome's cookie store ids), and Chrome's own "0" and "1".
base::expected<std::string, std::string> ContainerOfCookieStoreId(
    Profile* profile,
    std::string_view cookie_store_id);

// cookies_api.cc: the cookie manager of a cookie store Firefox names (as above),
// containers' included; null for other ids (Chrome's).
network::mojom::CookieManager* CookieManagerForStoreId(
    content::BrowserContext* context,
    bool include_incognito,
    std::string_view store_id);

// cookies_api.cc: the cookie stores as Firefox lists them (cookies.getAllCookieStores
// for Firefox add-ons): the default one, then each in use by a tab.
std::vector<extensions::api::cookies::CookieStore> FirefoxCookieStores(
    Profile* profile,
    bool include_incognito);

}  // namespace oblada::firefox_addons

#endif  // OBLADA_BROWSER_FIREFOX_ADDONS_COOKIE_STORE_IDS_H_
