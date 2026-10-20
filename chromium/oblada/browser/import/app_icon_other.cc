#include "oblada/browser/import/app_icon.h"

namespace oblada::import {

// Not read yet on Windows (the icon of the app's executable) nor on Linux (the
// one its .desktop file names, from the icon theme).
std::string AppIconDataUrl(const std::string& app_id) {
  return std::string();
}

}  // namespace oblada::import
