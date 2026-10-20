#ifndef OBLADA_BROWSER_UPDATER_UPDATER_H_
#define OBLADA_BROWSER_UPDATER_UPDATER_H_

#include <memory>

class PrefRegistrySimple;
class VersionUpdater;

// Oblada updates itself from its releases on GitHub: once a day it reads the feed of the
// latest one, and downloads a newer version in the background, which replaces the app
// when Oblada quits (or right away, from the About page's "Relaunch" button). On macOS
// that's Sparkle (updater_mac.mm). Only an app packaged for release updates itself: the
// feed and the key updates are signed with are added to it then (dmg.sh), so builds
// never replace themselves with a release.
namespace oblada::updater {

void RegisterLocalStatePrefs(PrefRegistrySimple* registry);

// Before the browser opens: after a relaunch to update, has it restore the last session,
// as Chromium's own relaunches do whatever opens at startup.
void PrepareStartup();

// Starts the updater, if this app updates itself. At browser start.
void Start();

// Whether an update has been downloaded and is waiting for Oblada to quit.
bool IsUpdateReady();

// Installs the update that's ready, quitting Oblada and opening it again. Returns false
// when there's none (Chromium's relaunch then goes on as usual).
bool InstallAndRelaunch();

// The About page's view of the updater: what it's doing, or that the app doesn't update
// itself.
std::unique_ptr<VersionUpdater> CreateVersionUpdater();

}  // namespace oblada::updater

#endif  // OBLADA_BROWSER_UPDATER_UPDATER_H_
