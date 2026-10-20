#ifndef OBLADA_BROWSER_UPDATER_UPDATER_PLATFORM_H_
#define OBLADA_BROWSER_UPDATER_UPDATER_PLATFORM_H_

#include <string>

// What each platform's updater implements (updater_mac.mm, updater_other.cc), and how it
// reports what it's doing to updater.cc, which the rest of Oblada talks to.
namespace oblada::updater {

struct UpdaterStatus {
  enum class State {
    kIdle,         // hasn't checked yet in this session
    kChecking,     // reading the feed
    kDownloading,  // a newer version, `version`
    kReady,        // downloaded `version`: installed when Oblada quits
    kUpToDate,     // the feed has nothing newer
    kFailed,       // `error` says why
  };
  State state = State::kIdle;
  std::string version;
  int progress = 0;  // percent of the download, when known
  std::u16string error;
};

// Starts the platform's updater; false if this app doesn't update itself.
bool StartPlatformUpdater();

// Checks the feed now, unless the updater is busy or has an update ready.
void CheckNow();

// Installs the update that's ready and relaunches Oblada.
void InstallNow();

// The platform's updater reports here.
void SetStatus(const UpdaterStatus& status);

}  // namespace oblada::updater

#endif  // OBLADA_BROWSER_UPDATER_UPDATER_PLATFORM_H_
