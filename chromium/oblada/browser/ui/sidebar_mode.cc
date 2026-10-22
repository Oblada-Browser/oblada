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

// The layout collapsing the tab strip goes to: scaffold or compact, whichever
// was chosen last.
constexpr char kCollapsedModePref[] = "oblada.sidebar.collapsed_mode";

constexpr char kSidebar[] = "sidebar";
constexpr char kScaffold[] = "scaffold";
constexpr char kCompact[] = "compact";

// The profile's own, also for its incognito windows.
PrefService* PrefsOf(Profile* profile) {
  return profile->GetOriginalProfile()->GetPrefs();
}

Mode ModeNamed(std::string_view name, Mode otherwise) {
  return name == kSidebar    ? Mode::kSidebar
         : name == kScaffold ? Mode::kScaffold
         : name == kCompact  ? Mode::kCompact
                             : otherwise;
}

const char* NameOf(Mode mode) {
  switch (mode) {
    case Mode::kSidebar:
      return kSidebar;
    case Mode::kScaffold:
      return kScaffold;
    case Mode::kCompact:
      return kCompact;
  }
  NOTREACHED();
}

}  // namespace

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterStringPref(kModePref, kSidebar);
  registry->RegisterStringPref(kCollapsedModePref, kCompact);
}

Mode GetMode(Profile* profile) {
  return ModeNamed(PrefsOf(profile)->GetString(kModePref), Mode::kSidebar);
}

std::string_view ModeName(Mode mode) {
  return NameOf(mode);
}

std::optional<Mode> ModeFromName(std::string_view name) {
  for (Mode mode : {Mode::kSidebar, Mode::kScaffold, Mode::kCompact}) {
    if (name == NameOf(mode)) {
      return mode;
    }
  }
  return std::nullopt;
}

bool CollapsesTabStrip(const PrefService* prefs) {
  return ModeNamed(prefs->GetString(kModePref), Mode::kSidebar) !=
         Mode::kSidebar;
}

void SetMode(Profile* profile, Mode mode) {
  PrefService* prefs = PrefsOf(profile);
  if (mode != Mode::kSidebar) {
    prefs->SetString(kCollapsedModePref, NameOf(mode));
  }
  prefs->SetString(kModePref, NameOf(mode));
}

namespace {

// ModeMenuModel's items, a command for each layout.
constexpr int kFirstModeCommand = kModeMenuId + 1;
constexpr Mode kModes[] = {Mode::kSidebar, Mode::kScaffold, Mode::kCompact};

}  // namespace

ModeMenuModel::ModeMenuModel(Profile* profile)
    : ui::SimpleMenuModel(this), profile_(profile) {
  for (size_t i = 0; i < std::size(kModes); ++i) {
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
  // Chosen in Settings: the next collapse goes there too.
  if (mode != Mode::kSidebar) {
    PrefsOf(profile_)->SetString(kCollapsedModePref, NameOf(mode));
  }
  const bool collapse = mode != Mode::kSidebar;
  if (controller_->IsCollapsed() != collapse) {
    controller_->RequestCollapse(collapse);
  }
  // Scaffold and compact are both collapsed: the layout changes all the same.
  tab_strip_region_->SetProperty(kModeKey, mode);
  tab_strip_region_->InvalidateLayout();
}

void ModeKeeper::OnCollapseChanged(tabs::VerticalTabStripCollapseState state) {
  const Mode mode = GetMode(profile_);
  if (state == tabs::VerticalTabStripCollapseState::kExpanded &&
      mode != Mode::kSidebar) {
    SetMode(profile_, Mode::kSidebar);
  } else if (state == tabs::VerticalTabStripCollapseState::kCollapsed &&
             mode == Mode::kSidebar) {
    SetMode(profile_,
            ModeNamed(PrefsOf(profile_)->GetString(kCollapsedModePref),
                      Mode::kCompact));
  }
}

}  // namespace oblada::sidebar
