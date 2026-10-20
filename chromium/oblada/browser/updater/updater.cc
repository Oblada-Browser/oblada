#include "oblada/browser/updater/updater.h"

#include <string>
#include <utility>

#include "base/callback_list.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/devtools/chrome_devtools_manager_delegate.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/ui/webui/help/version_updater.h"
#include "chrome/common/chrome_switches.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "oblada/browser/updater/updater_platform.h"

namespace oblada::updater {

namespace {

// Set when Oblada quits to install an update: the next start restores the session.
constexpr char kRestoreSessionPref[] = "oblada.updater.restore_session";

bool g_started = false;
// Whether this app updates itself (see StartPlatformUpdater()).
bool g_enabled = false;

UpdaterStatus& CurrentStatus() {
  static base::NoDestructor<UpdaterStatus> status;
  return *status;
}

base::RepeatingClosureList& StatusObservers() {
  static base::NoDestructor<base::RepeatingClosureList> observers;
  return *observers;
}

// The About page's: has the updater check the feed when the page opens, and reports
// what it's doing until the page closes.
class ObladaVersionUpdater : public VersionUpdater {
 public:
  ObladaVersionUpdater() = default;
  ObladaVersionUpdater(const ObladaVersionUpdater&) = delete;
  ObladaVersionUpdater& operator=(const ObladaVersionUpdater&) = delete;
  ~ObladaVersionUpdater() override = default;

  void CheckForUpdate(StatusCallback status_callback,
                      PromoteCallback promote_callback) override {
    status_callback_ = std::move(status_callback);
    if (!g_enabled) {
      status_callback_.Run(DISABLED, 0, false, false, std::string(), 0,
                           std::u16string());
      return;
    }
    subscription_ = StatusObservers().Add(base::BindRepeating(
        &ObladaVersionUpdater::Report, base::Unretained(this)));
    CheckNow();
    Report();
  }

#if BUILDFLAG(IS_MAC)
  void PromoteUpdater() override {}
#endif

 private:
  void Report() {
    const UpdaterStatus& status = CurrentStatus();
    Status about_status = CHECKING;
    int progress = 0;
    std::u16string message;
    switch (status.state) {
      case UpdaterStatus::State::kIdle:
      case UpdaterStatus::State::kChecking:
        about_status = CHECKING;
        break;
      case UpdaterStatus::State::kDownloading:
        about_status = UPDATING;
        progress = status.progress;
        break;
      case UpdaterStatus::State::kReady:
        about_status = NEARLY_UPDATED;
        break;
      case UpdaterStatus::State::kUpToDate:
        about_status = UPDATED;
        break;
      case UpdaterStatus::State::kFailed:
        about_status = FAILED;
        message = status.error;
        break;
    }
    status_callback_.Run(about_status, progress, false, false, status.version,
                         0, message);
  }

  StatusCallback status_callback_;
  base::CallbackListSubscription subscription_;
};

}  // namespace

void RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kRestoreSessionPref, false);
}

void PrepareStartup() {
  PrefService* local_state = g_browser_process->local_state();
  if (!local_state || !local_state->GetBoolean(kRestoreSessionPref)) {
    return;
  }
  local_state->SetBoolean(kRestoreSessionPref, false);
  base::CommandLine::ForCurrentProcess()->AppendSwitch(
      switches::kRestoreLastSession);
}

void Start() {
  if (g_started) {
    return;
  }
  g_started = true;
  g_enabled = StartPlatformUpdater();
}

bool IsUpdateReady() {
  return CurrentStatus().state == UpdaterStatus::State::kReady;
}

bool InstallAndRelaunch() {
  if (!IsUpdateReady()) {
    return false;
  }
  if (PrefService* local_state = g_browser_process->local_state()) {
    local_state->SetBoolean(kRestoreSessionPref, true);
  }
  InstallNow();
  // The updater replaces the app once Oblada has quit, and opens it again. Oblada quits
  // as the menu's Quit does, rather than waiting to be asked (a headless Oblada can't
  // be), and nothing may keep it running meanwhile: not even automation's DevTools,
  // which otherwise does until it says so.
  ChromeDevToolsManagerDelegate::AllowBrowserToClose();
  chrome::AttemptExit();
  return true;
}

std::unique_ptr<VersionUpdater> CreateVersionUpdater() {
  return std::make_unique<ObladaVersionUpdater>();
}

void SetStatus(const UpdaterStatus& status) {
  CurrentStatus() = status;
  StatusObservers().Notify();
}

}  // namespace oblada::updater
