#ifndef OBLADA_BROWSER_SETTINGS_OBLADA_PAGE_H_
#define OBLADA_BROWSER_SETTINGS_OBLADA_PAGE_H_

namespace content {
class WebUIDataSource;
}  // namespace content

// Settings > Oblada (oblada://settings/oblada): Oblada's own settings, in
// sections. Appearance: the window's layout (sidebar_mode.h), and whether the
// sidebar shows the workspace's name above its tabs (workspaces.h's
// kShowHeaderPref).
//
// The page is a custom element, settings-oblada-page, served here as a module
// (oblada/oblada_page.js) rather than built with Settings' own, as Oblada adds
// no files to Chromium's tree. Settings imports it (settings_main.ts, which its
// compiler knows from oblada_page.d.ts and its bundle leaves out, BUILD.gn),
// routes to it (route.ts) and lists it (settings_menu.html, settings_main.html);
// prefs_util.cc lets it change its preferences.
namespace oblada::settings {

// Adds the page, its module and its strings, to `source`, Settings' data
// source.
void AddObladaPage(content::WebUIDataSource* source);

}  // namespace oblada::settings

#endif  // OBLADA_BROWSER_SETTINGS_OBLADA_PAGE_H_
