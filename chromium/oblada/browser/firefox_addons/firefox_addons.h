#ifndef OBLADA_BROWSER_FIREFOX_ADDONS_FIREFOX_ADDONS_H_
#define OBLADA_BROWSER_FIREFOX_ADDONS_FIREFOX_ADDONS_H_

// Firefox add-ons in Oblada. addons.mozilla.org is shown its Firefox page (so it offers
// "Add to Firefox"), the .xpi it serves is converted (xpi_conversion.h) and installed
// like any extension, after the usual confirmation listing what it can do.

#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/version.h"
#include "url/gurl.h"

class Profile;

namespace base {
class FilePath;
}  // namespace base

namespace content {
class WebContents;
}  // namespace content

namespace download {
class DownloadItem;
}  // namespace download

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace oblada::firefox_addons {

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

// Whether add-ons may come from `url`: addons.mozilla.org (and its files' host),
// over HTTPS.
bool IsAddonSource(const GURL& url);

// chrome_download_manager_delegate.cc: whether `item` is a Firefox add-on (.xpi) to
// install, one from addons.mozilla.org; then InstallDownload() installs it, telling
// `done` whether it was.
bool IsAddonDownload(Profile* profile, const download::DownloadItem& item);
void InstallDownload(Profile* profile,
                     download::DownloadItem* item,
                     base::OnceCallback<void(bool)> done);

// tab_helpers.cc
void AttachTabHelpers(content::WebContents* contents);

// The Firefox add-ons installed in `profile`.
struct InstalledAddon {
  std::string gecko_id;
  base::Version version;
  GURL update_url;  // Empty: addons.mozilla.org.
};
std::vector<InstalledAddon> GetInstalledAddons(Profile* profile);

// Installs a new version of an installed add-on, downloaded to `xpi`, without asking;
// not if it's another add-on than `gecko_id`.
void InstallUpdate(Profile* profile,
                   const base::FilePath& xpi,
                   const std::string& gecko_id);

// Browser startup.
void Init();

}  // namespace oblada::firefox_addons

#endif  // OBLADA_BROWSER_FIREFOX_ADDONS_FIREFOX_ADDONS_H_
