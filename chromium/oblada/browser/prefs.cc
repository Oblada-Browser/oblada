#include "oblada/browser/prefs.h"

#include "oblada/browser/barrier/barrier.h"
#include "oblada/browser/clean_links/mozilla_list.h"
#include "oblada/browser/containers/containers.h"
#include "oblada/browser/firefox_addons/firefox_addons.h"
#include "oblada/browser/new_tab/new_tab_ui.h"
#include "oblada/browser/ui/sidebar_mode.h"
#include "oblada/browser/updater/updater.h"
#include "oblada/browser/workspaces/workspaces.h"

namespace oblada {

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry,
                          const std::string& locale) {
  barrier::RegisterProfilePrefs(registry);
  containers::RegisterProfilePrefs(registry, locale);
  firefox_addons::RegisterProfilePrefs(registry);
  new_tab::RegisterProfilePrefs(registry);
  sidebar::RegisterProfilePrefs(registry);
  workspaces::RegisterProfilePrefs(registry);
}

void RegisterProfilePrefDefaults(user_prefs::PrefRegistrySyncable* registry) {
  barrier::RegisterChromiumDefaults(registry);
}

void RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  barrier::RegisterLocalStatePrefs(registry);
  clean_links::RegisterMozillaListPrefs(registry);
  updater::RegisterLocalStatePrefs(registry);
}

}  // namespace oblada
