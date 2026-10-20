#ifndef OBLADA_BROWSER_FIREFOX_ADDONS_ADDON_UPDATES_H_
#define OBLADA_BROWSER_FIREFOX_ADDONS_ADDON_UPDATES_H_

class Profile;

namespace oblada::firefox_addons {

// Keeps the profile's Firefox add-ons up to date, as Firefox does: a few minutes after
// the profile is first used, then twice a day, it looks for a newer version of each,
// where the add-on says (its update_url) or else on addons.mozilla.org, and installs
// it. Starting it again does nothing.
//
// --oblada-addon-update-interval=<seconds> checks that often instead (for tests).
void StartAddonUpdates(Profile* profile);

}  // namespace oblada::firefox_addons

#endif  // OBLADA_BROWSER_FIREFOX_ADDONS_ADDON_UPDATES_H_
