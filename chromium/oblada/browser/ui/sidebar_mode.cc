#include "oblada/browser/ui/sidebar_mode.h"

#include <iterator>
#include <string>
#include <string_view>

#include "base/functional/bind.h"
#include "base/notreached.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "oblada/browser/l10n.h"
#include "ui/base/class_property.h"
#include "ui/views/view.h"

DEFINE_UI_CLASS_PROPERTY_TYPE(oblada::sidebar::Mode)

namespace oblada::sidebar {

namespace {

DEFINE_UI_CLASS_PROPERTY_KEY(Mode, kModeKey, Mode::kSidebar)

// The layout collapsing the tab strip goes to from the sidebar: scaffold or
// compact, whichever was chosen last.
constexpr char kCollapsedModePref[] = "oblada.sidebar.collapsed_mode";
// The layout expanding it goes to: sidebar or expanded scaffold, whichever was
// chosen last.
constexpr char kExpandedModePref[] = "oblada.sidebar.expanded_mode";

constexpr char kSidebar[] = "sidebar";
constexpr char kScaffold[] = "scaffold";
constexpr char kExpandedScaffold[] = "expanded_scaffold";
constexpr char kCompact[] = "compact";
constexpr char kHorizon[] = "horizon";
constexpr char kHorizonCompact[] = "horizon_compact";
constexpr char kClassic[] = "classic";

constexpr Mode kModes[] = {Mode::kSidebar,          Mode::kScaffold,
                           Mode::kExpandedScaffold, Mode::kCompact,
                           Mode::kHorizon,          Mode::kHorizonCompact,
                           Mode::kClassic};

// The profile's own, also for its incognito windows.
PrefService* PrefsOf(Profile* profile) {
  return profile->GetOriginalProfile()->GetPrefs();
}

const char* NameOf(Mode mode) {
  switch (mode) {
    case Mode::kSidebar:
      return kSidebar;
    case Mode::kScaffold:
      return kScaffold;
    case Mode::kExpandedScaffold:
      return kExpandedScaffold;
    case Mode::kCompact:
      return kCompact;
    case Mode::kHorizon:
      return kHorizon;
    case Mode::kHorizonCompact:
      return kHorizonCompact;
    case Mode::kClassic:
      return kClassic;
  }
  NOTREACHED();
}

Mode ModeNamed(std::string_view name, Mode otherwise) {
  for (Mode mode : kModes) {
    if (name == NameOf(mode)) {
      return mode;
    }
  }
  return otherwise;
}

// The layout last chosen of those collapsing the tab strip, or of those
// expanding it.
Mode LastMode(const PrefService* prefs, bool collapsed) {
  const Mode otherwise = collapsed ? Mode::kCompact : Mode::kSidebar;
  const Mode mode = ModeNamed(
      prefs->GetString(collapsed ? kCollapsedModePref : kExpandedModePref),
      otherwise);
  return IsCollapsed(mode) == collapsed ? mode : otherwise;
}

// Remembers `mode` as the last chosen of its kind (see LastMode()), if it has
// vertical tabs.
void RememberMode(PrefService* prefs, Mode mode) {
  if (IsHorizontal(mode)) {
    return;
  }
  prefs->SetString(IsCollapsed(mode) ? kCollapsedModePref : kExpandedModePref,
                   NameOf(mode));
}

}  // namespace

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterStringPref(kModePref, kSidebar);
  registry->RegisterStringPref(kCollapsedModePref, kCompact);
  registry->RegisterStringPref(kExpandedModePref, kSidebar);
}

Mode GetMode(Profile* profile) {
  return ModeNamed(PrefsOf(profile)->GetString(kModePref), Mode::kSidebar);
}

std::string_view ModeName(Mode mode) {
  return NameOf(mode);
}

std::optional<Mode> ModeFromName(std::string_view name) {
  for (Mode mode : kModes) {
    if (name == NameOf(mode)) {
      return mode;
    }
  }
  return std::nullopt;
}

bool IsCollapsed(Mode mode) {
  return mode == Mode::kScaffold || mode == Mode::kCompact;
}

bool IsScaffold(Mode mode) {
  return mode == Mode::kScaffold || mode == Mode::kExpandedScaffold;
}

bool IsHorizontal(Mode mode) {
  return mode == Mode::kHorizon || mode == Mode::kHorizonCompact ||
         mode == Mode::kClassic;
}

bool CollapsesTabStrip(const PrefService* prefs) {
  return IsCollapsed(ModeNamed(prefs->GetString(kModePref), Mode::kSidebar));
}

bool HasHorizontalTabs(const PrefService* prefs) {
  return IsHorizontal(ModeNamed(prefs->GetString(kModePref), Mode::kSidebar));
}

void SetMode(Profile* profile, Mode mode) {
  PrefService* prefs = PrefsOf(profile);
  RememberMode(prefs, mode);
  prefs->SetString(kModePref, NameOf(mode));
}

namespace {

// ModeMenuModel's items, a command for each layout.
constexpr int kFirstModeCommand = kModeMenuId + 1;

}  // namespace

ModeMenuModel::ModeMenuModel(Profile* profile)
    : ui::SimpleMenuModel(this), profile_(profile) {
  for (size_t i = 0; i < std::size(kModes); ++i) {
    // The layouts with horizontal tabs apart from those with the sidebar.
    if (IsHorizontal(kModes[i]) && i > 0 && !IsHorizontal(kModes[i - 1])) {
      AddSeparator(ui::NORMAL_SEPARATOR);
    }
    AddRadioItem(kFirstModeCommand + static_cast<int>(i),
                 L10n(std::string("settings.layout.") + NameOf(kModes[i])),
                 /*group_id=*/0);
  }
}

ModeMenuModel::~ModeMenuModel() = default;

bool ModeMenuModel::IsCommandIdChecked(int command_id) const {
  return kModes[command_id - kFirstModeCommand] == GetMode(profile_);
}

void ModeMenuModel::ExecuteCommand(int command_id, int event_flags) {
  SetMode(profile_, kModes[command_id - kFirstModeCommand]);
}

Mode ModeOf(const views::View* tab_strip_region) {
  return tab_strip_region->GetProperty(kModeKey);
}

ModeKeeper::ModeKeeper(Profile* profile,
                       tabs::VerticalTabStripStateController* controller,
                       views::View* tab_strip_region)
    : profile_(profile),
      controller_(controller),
      tab_strip_region_(tab_strip_region) {
  registrar_.Init(PrefsOf(profile));
  registrar_.Add(kModePref, base::BindRepeating(&ModeKeeper::OnModeChanged,
                                                base::Unretained(this)));
  collapse_subscription_ = controller_->RegisterOnCollapseChanged(
      base::BindRepeating(&ModeKeeper::OnCollapseChanged,
                          base::Unretained(this)));
  OnModeChanged();
}

ModeKeeper::~ModeKeeper() = default;

void ModeKeeper::OnModeChanged() {
  const Mode mode = GetMode(profile_);
  // Chosen in Settings: the next collapse or expansion goes there too.
  RememberMode(PrefsOf(profile_), mode);
  // With horizontal tabs, the vertical strip stays as it was, out of sight
  // (the state controller hides it, vertical_tab_strip_state_controller_impl.cc).
  const bool collapse = IsCollapsed(mode);
  if (!IsHorizontal(mode) && controller_->IsCollapsed() != collapse) {
    controller_->RequestCollapse(collapse);
  }
  // Two layouts are collapsed, two expanded: the layout changes all the same.
  tab_strip_region_->SetProperty(kModeKey, mode);
  tab_strip_region_->InvalidateLayout();
}

void ModeKeeper::OnCollapseChanged(tabs::VerticalTabStripCollapseState state) {
  const Mode mode = GetMode(profile_);
  if (IsHorizontal(mode)) {
    return;
  }
  if (state == tabs::VerticalTabStripCollapseState::kExpanded &&
      IsCollapsed(mode)) {
    SetMode(profile_, LastMode(PrefsOf(profile_), /*collapsed=*/false));
  } else if (state == tabs::VerticalTabStripCollapseState::kCollapsed &&
             !IsCollapsed(mode)) {
    SetMode(profile_, mode == Mode::kExpandedScaffold
                          ? Mode::kScaffold
                          : LastMode(PrefsOf(profile_), /*collapsed=*/true));
  }
}

}  // namespace oblada::sidebar
