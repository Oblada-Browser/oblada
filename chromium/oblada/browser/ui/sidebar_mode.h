#ifndef OBLADA_BROWSER_UI_SIDEBAR_MODE_H_
#define OBLADA_BROWSER_UI_SIDEBAR_MODE_H_

#include <optional>
#include <string_view>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "components/prefs/pref_change_registrar.h"
#include "ui/menus/simple_menu_model.h"

class PrefService;
class Profile;

namespace tabs {
class VerticalTabStripStateController;
enum class VerticalTabStripCollapseState;
}  // namespace tabs

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace views {
class View;
}  // namespace views

// The window's layout, which the user picks in Settings > Oblada, the same in
// every window of the profile (sidebar_layout.h lays each out):
// - kSidebar, the full sidebar: tabs, the address bar and workspaces;
// - kScaffold, the tabs as icons in a narrow column, the address bar in a row
//   at the top, next to the window controls;
// - kCompact, only the page, the sidebar showing over it when the pointer
//   reaches the window's left edge (sidebar_card.h).
// Scaffold and compact are Chromium's collapsed vertical tab strip, sidebar its
// expanded one: collapsing the strip (⌘⇧L, the sidebar's menu) goes to the
// layout last chosen of the two, expanding it to the sidebar.
namespace oblada::sidebar {

enum class Mode {
  kSidebar,
  kScaffold,
  kCompact,
};

// The layout, as Settings sets it ("sidebar", "scaffold" or "compact").
inline constexpr char kModePref[] = "oblada.sidebar.mode";

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

Mode GetMode(Profile* profile);
void SetMode(Profile* profile, Mode mode);

// A layout's name, as the pref has it, and back (none for an unknown name).
std::string_view ModeName(Mode mode);
std::optional<Mode> ModeFromName(std::string_view name);

// Whether the layout in `prefs` (a profile's) has the vertical tab strip
// collapsed: for a new window's to be from the start.
bool CollapsesTabStrip(const PrefService* prefs);

// The layout of the window whose tab strip region is `tab_strip_region`, as
// its ModeKeeper last saw it: for the browser layout.
Mode ModeOf(const views::View* tab_strip_region);

// The app menu's "Window layout" submenu, its layouts to pick one.
inline constexpr int kModeMenuId = 55670;
class ModeMenuModel : public ui::SimpleMenuModel,
                      public ui::SimpleMenuModel::Delegate {
 public:
  explicit ModeMenuModel(Profile* profile);
  ModeMenuModel(const ModeMenuModel&) = delete;
  ModeMenuModel& operator=(const ModeMenuModel&) = delete;
  ~ModeMenuModel() override;

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  raw_ptr<Profile> profile_;
};

// For a window's tab strip region: keeps its tab strip collapsed or expanded as
// the layout says, and the layout as the strip is collapsed or expanded.
class ModeKeeper {
 public:
  ModeKeeper(Profile* profile,
             tabs::VerticalTabStripStateController* controller,
             views::View* tab_strip_region);
  ModeKeeper(const ModeKeeper&) = delete;
  ModeKeeper& operator=(const ModeKeeper&) = delete;
  ~ModeKeeper();

 private:
  void OnModeChanged();
  void OnCollapseChanged(tabs::VerticalTabStripCollapseState state);

  raw_ptr<Profile> profile_;
  raw_ptr<tabs::VerticalTabStripStateController> controller_;
  raw_ptr<views::View> tab_strip_region_;
  PrefChangeRegistrar registrar_;
  base::CallbackListSubscription collapse_subscription_;
};

}  // namespace oblada::sidebar

#endif  // OBLADA_BROWSER_UI_SIDEBAR_MODE_H_
