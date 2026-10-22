#include "oblada/browser/oblada_browser_main_extra_parts.h"

#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_browser_main_extra_parts.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "oblada/browser/clean_links/mozilla_list.h"
#include "oblada/browser/debug/window_snapshots.h"
#include "oblada/browser/firefox_addons/firefox_addons.h"
#include "oblada/browser/peek/peek.h"
#include "oblada/browser/updater/updater.h"
#include "oblada/browser/workspaces/workspaces.h"

namespace oblada {

namespace {

class ObladaBrowserMainExtraParts : public ChromeBrowserMainExtraParts {
 public:
  void PreProfileInit() override {
    // Chromium's first run window (signing in to a Google account) doesn't open
    // before the browser: the first launch opens Oblada's welcome in the browser
    // instead (startup_browser_creator_impl.cc).
    if (PrefService* local_state = g_browser_process->local_state()) {
      local_state->SetBoolean(prefs::kFirstRunFinished, true);
    }
    updater::PrepareStartup();
  }

  void PostBrowserStart() override {
    firefox_addons::Init();
    workspaces::Init();
    peek::Init();
    clean_links::StartMozillaListUpdates();
    debug::MaybeStartWindowSnapshots();
    updater::Start();
  }
};

}  // namespace

std::unique_ptr<ChromeBrowserMainExtraParts> CreateBrowserMainExtraParts() {
  return std::make_unique<ObladaBrowserMainExtraParts>();
}

}  // namespace oblada
