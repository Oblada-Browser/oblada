#ifndef OBLADA_BROWSER_FIREFOX_ADDONS_XPI_CONVERSION_H_
#define OBLADA_BROWSER_FIREFOX_ADDONS_XPI_CONVERSION_H_

#include <string>
#include <string_view>
#include <vector>

#include "base/types/expected.h"

namespace base {
class FilePath;
}  // namespace base

namespace oblada::firefox_addons {

struct ConvertedAddon {
  // The add-on's own id (browser_specific_settings.gecko.id): it stays the same
  // across versions, so it decides the Chromium extension id.
  std::string gecko_id;
  // Where the add-on looks for new versions (browser_specific_settings.gecko.update_url),
  // if not on addons.mozilla.org.
  std::string update_url;
  // What won't work here, e.g. Firefox-only APIs it asks for.
  std::vector<std::string> warnings;
};

// Unzips the .xpi `xpi` into the empty directory `dir` and makes it a Chromium
// extension in place:
//  - Firefox-only manifest keys go, and permissions are renamed (menus is
//    contextMenus) or dropped when Chromium has no such API;
//  - Firefox's Manifest V3 background scripts become a service worker;
//  - SVG icons go (Chromium can't decode them, which would fail the install);
//  - kCompatScript runs first in the background, in content scripts and in pages.
// `fallback_id` names the add-on when the manifest has no id. Blocking.
base::expected<ConvertedAddon, std::string> UnpackAndConvert(
    const base::FilePath& xpi,
    const base::FilePath& dir,
    std::string_view fallback_id);

}  // namespace oblada::firefox_addons

#endif  // OBLADA_BROWSER_FIREFOX_ADDONS_XPI_CONVERSION_H_
