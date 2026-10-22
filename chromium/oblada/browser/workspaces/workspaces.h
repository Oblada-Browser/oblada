#ifndef OBLADA_BROWSER_WORKSPACES_WORKSPACES_H_
#define OBLADA_BROWSER_WORKSPACES_WORKSPACES_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "oblada/browser/workspaces/workspace_theme.h"

class BrowserWindowInterface;
class Profile;

namespace tabs {
class TabInterface;
}  // namespace tabs

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace oblada::workspaces {

// Zen-like workspaces: separate sets of tabs in a window, one shown at a time; the
// pinned tabs (Zen's Essentials) are in all of them.
//
// In a window, a workspace is a tab group named after it, focused when it's the one
// shown (TabStripModel's focus mode shows only that group and the pinned tabs). The
// workspaces themselves are the profile's, so that they're in every window and
// outlive their tabs.
struct Workspace {
  std::string id;
  std::string name;
  std::string icon;       // an emoji, or empty
  std::string container;  // the container its new tabs open in, or empty
  int color = 0;          // tab_groups::TabGroupColorId of its groups
  // The window's look while it's shown: its own, or, unset, the default theme.
  std::optional<Theme> theme;
};

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

// In order; there's always at least one.
std::vector<Workspace> GetWorkspaces(Profile* profile);
std::optional<Workspace> FindWorkspace(Profile* profile, std::string_view id);
// Adds a workspace at the end, named "Space N" when `name` is empty.
Workspace AddWorkspace(Profile* profile, std::string name = std::string());
// Saves changes to a workspace, e.g. its name, which its groups then take.
void UpdateWorkspace(Profile* profile, const Workspace& workspace);
// Closes the workspace's tabs in every window, then forgets it (not the last one).
void RemoveWorkspace(Profile* profile, std::string_view id);
// Notifies of any change to the workspaces, the default theme included.
base::CallbackListSubscription ObserveWorkspaces(Profile* profile,
                                                 base::RepeatingClosure changed);

// The theme of the workspaces without their own; to begin with, one without colors,
// with a little grain.
Theme DefaultTheme(Profile* profile);
void SetDefaultTheme(Profile* profile, const Theme& theme);
// Back to how it began.
void ResetDefaultTheme(Profile* profile);
bool IsDefaultThemeReset(Profile* profile);
// A workspace's theme: its own, else the default one.
Theme ThemeOf(Profile* profile, const Workspace& workspace);

// Whether the sidebar shows the workspace's header, its icon and name above its
// tabs (Settings > Appearance > Oblada): off by default, as the workspaces at the
// bottom of the sidebar tell which one is shown.
inline constexpr char kShowHeaderPref[] = "oblada.workspaces.show_header";
bool ShowsHeader(Profile* profile);

// The workspace a window shows.
std::optional<Workspace> CurrentWorkspace(BrowserWindowInterface* browser);
// Shows a workspace in a window (with a new tab, if it has none there).
void SwitchTo(BrowserWindowInterface* browser, std::string_view id);
// Shows the next or previous workspace, e.g. for a swipe on the sidebar.
void SwitchRelative(BrowserWindowInterface* browser, bool forward);
// Adds a workspace and shows it.
void AddAndSwitch(BrowserWindowInterface* browser);
// Moves a tab of a window to another workspace (the window keeps showing the one
// it was in, unless that was its last tab).
void MoveTab(BrowserWindowInterface* browser,
             tabs::TabInterface* tab,
             std::string_view id);
// The container a new tab of a window opens in: its workspace's, if any.
std::string ContainerForNewTabs(BrowserWindowInterface* browser);
// Closes `tabs` of a window if they're the last of the workspace shown, which
// keeps a New Tab page then; if they're New Tab pages themselves, the window
// closes instead, whatever tabs the other workspaces have in it. False if they
// aren't its last (for the caller to close them as usual).
bool CloseLastTabs(BrowserWindowInterface* browser,
                   const std::vector<const tabs::TabInterface*>& tabs);

// How a window's sidebar shows another workspace, animated: it calls ShowNow()
// once the animation is over. `forward`: the workspace comes after the one shown.
using SwitchAnimator =
    base::RepeatingCallback<void(const std::string& id, bool forward)>;
// Sets (or, with a null one, removes) a window's.
void SetSwitchAnimator(BrowserWindowInterface* browser, SwitchAnimator animator);
// Shows a workspace in a window right away.
void ShowNow(BrowserWindowInterface* browser, std::string_view id);
// The workspace after or before the one a window shows, if there's one.
std::optional<Workspace> Neighbor(BrowserWindowInterface* browser, bool forward);
// A workspace's tabs in a window, and the one it showed last.
std::vector<tabs::TabInterface*> TabsOf(BrowserWindowInterface* browser,
                                        std::string_view id);
tabs::TabInterface* LastActiveTabOf(BrowserWindowInterface* browser,
                                    std::string_view id);

// Starts keeping the tabs of every window in workspaces.
void Init();

}  // namespace oblada::workspaces

#endif  // OBLADA_BROWSER_WORKSPACES_WORKSPACES_H_
