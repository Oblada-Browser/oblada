#ifndef OBLADA_BROWSER_IMPORT_APP_ICON_H_
#define OBLADA_BROWSER_IMPORT_APP_ICON_H_

#include <string>

namespace oblada::import {

// The icon of the app `app_id` names (see Source::app_id), as a PNG data: URL
// for the page, or nothing where the platform's icons aren't read yet (the
// page then shows the browser's initial).
std::string AppIconDataUrl(const std::string& app_id);

}  // namespace oblada::import

#endif  // OBLADA_BROWSER_IMPORT_APP_ICON_H_
