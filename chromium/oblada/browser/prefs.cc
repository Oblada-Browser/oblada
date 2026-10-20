#include "oblada/browser/prefs.h"

#include "oblada/browser/containers/containers.h"
#include "oblada/browser/firefox_addons/firefox_addons.h"
#include "oblada/browser/new_tab/new_tab_ui.h"
#include "oblada/browser/updater/updater.h"
#include "oblada/browser/workspaces/workspaces.h"

namespace oblada {

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry,
                          const std::string& locale) {
  containers::RegisterProfilePrefs(registry, locale);
  firefox_addons::RegisterProfilePrefs(registry);
  new_tab::RegisterProfilePrefs(registry);
  workspaces::RegisterProfilePrefs(registry);
}

void RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  updater::RegisterLocalStatePrefs(registry);
}

}  // namespace oblada
