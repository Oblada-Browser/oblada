#include "oblada/browser/updater/updater_platform.h"

// Oblada doesn't update itself on this platform yet: the About page shows no update
// status.
namespace oblada::updater {

bool StartPlatformUpdater() {
  return false;
}

void CheckNow() {}

void InstallNow() {}

}  // namespace oblada::updater
