#ifndef OBLADA_BROWSER_PREFS_H_
#define OBLADA_BROWSER_PREFS_H_

#include <string>

class PrefRegistrySimple;

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace oblada {

// Oblada's profile preferences (called from chrome/browser/prefs/browser_prefs.cc).
void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry,
                          const std::string& locale);

// Oblada's defaults for Chromium's own profile preferences (called from
// browser_prefs.cc once they're registered).
void RegisterProfilePrefDefaults(user_prefs::PrefRegistrySyncable* registry);

// Oblada's browser-wide preferences, in Local State (called from browser_prefs.cc too).
void RegisterLocalStatePrefs(PrefRegistrySimple* registry);

}  // namespace oblada

#endif  // OBLADA_BROWSER_PREFS_H_
