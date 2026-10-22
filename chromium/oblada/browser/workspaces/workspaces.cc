#include "oblada/browser/workspaces/workspaces.h"

#include <algorithm>
#include <map>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/scoped_observation.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/supports_user_data.h"
#include "base/task/sequenced_task_runner.h"
#include "base/uuid.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/common/webui_url_constants.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "oblada/browser/containers/containers.h"
#include "oblada/browser/l10n.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/range/range.h"
#include "third_party/abseil-cpp/absl/cleanup/cleanup.h"
#include "url/gurl.h"

namespace oblada::workspaces {

namespace {

constexpr char kPref[] = "oblada.workspaces.list";
constexpr char kDefaultThemePref[] = "oblada.workspaces.default_theme";

// The colors new workspaces get, in turn.
constexpr tab_groups::TabGroupColorId kColors[] = {
    tab_groups::TabGroupColorId::kBlue,   tab_groups::TabGroupColorId::kGreen,
    tab_groups::TabGroupColorId::kPurple, tab_groups::TabGroupColorId::kOrange,
    tab_groups::TabGroupColorId::kCyan,   tab_groups::TabGroupColorId::kPink,
    tab_groups::TabGroupColorId::kRed,    tab_groups::TabGroupColorId::kYellow,
    tab_groups::TabGroupColorId::kGrey,
};

// The default theme until it's changed: no colors (gray), the window's opacity,
// and grain at the dial's second step.
Theme FirstDefaultTheme() {
  Theme theme;
  theme.grain = 2.0 / (kGrainSteps - 1);
  return theme;
}

PrefService* PrefsOf(Profile* profile) {
  // Incognito windows have the same workspaces.
  return profile->GetOriginalProfile()->GetPrefs();
}

// Per profile: who to tell about changes.
class ProfileWorkspaces : public base::SupportsUserData::Data {
 public:
  static ProfileWorkspaces& Get(Profile* profile) {
    Profile* original = profile->GetOriginalProfile();
    auto* data = static_cast<ProfileWorkspaces*>(original->GetUserData(&kKey));
    if (!data) {
      auto owned = std::make_unique<ProfileWorkspaces>();
      data = owned.get();
      original->SetUserData(&kKey, std::move(owned));
    }
    return *data;
  }

  base::RepeatingClosureList observers;

 private:
  static constexpr int kKey = 0;
};

base::DictValue ToDict(const Workspace& workspace) {
  return base::DictValue()
      .Set("id", workspace.id)
      .Set("name", workspace.name)
      .Set("icon", workspace.icon)
      .Set("container", workspace.container)
      .Set("color", workspace.color)
      .Set(workspace.theme ? "theme" : "default_theme",
           workspace.theme ? base::Value(ThemeToDict(*workspace.theme))
                           : base::Value(true));
}

std::optional<Workspace> FromDict(const base::Value& value) {
  const base::DictValue* dict = value.GetIfDict();
  if (!dict) {
    return std::nullopt;
  }
  const std::string* id = dict->FindString("id");
  const std::string* name = dict->FindString("name");
  if (!id || id->empty() || !name || name->empty()) {
    return std::nullopt;
  }
  Workspace workspace;
  workspace.id = *id;
  workspace.name = *name;
  if (const std::string* icon = dict->FindString("icon")) {
    workspace.icon = *icon;
  }
  if (const std::string* container = dict->FindString("container")) {
    workspace.container = *container;
  }
  workspace.color =
      std::clamp(dict->FindInt("color").value_or(0), 0,
                 static_cast<int>(tab_groups::TabGroupColorId::kNumEntries) - 1);
  if (const base::DictValue* theme = dict->FindDict("theme")) {
    workspace.theme = ThemeFromDict(*theme);
  } else if (!dict->FindBool("default_theme").value_or(false) &&
             dict->FindBool("tints_window").value_or(true)) {
    // From before themes: the window took the workspace's color (which is its
    // own theme now), or kept its own colors (the default theme now).
    workspace.theme = ThemeFromGroupColor(workspace.color, /*tints_window=*/true);
  }
  return workspace;
}

void Save(Profile* profile, const std::vector<Workspace>& workspaces) {
  base::ListValue list;
  for (const Workspace& workspace : workspaces) {
    list.Append(ToDict(workspace));
  }
  PrefsOf(profile)->SetList(kPref, std::move(list));
  ProfileWorkspaces::Get(profile).observers.Notify();
}

Workspace NewWorkspace(const std::vector<Workspace>& existing, std::string name) {
  Workspace workspace;
  workspace.id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  workspace.color = static_cast<int>(kColors[existing.size() % std::size(kColors)]);
  auto taken = [&existing](std::string_view candidate) {
    return std::ranges::any_of(existing, [candidate](const Workspace& w) {
      return w.name == candidate;
    });
  };
  if (name.empty()) {
    // "Space 2", "Space 3"...
    for (size_t n = existing.size() + 1;; ++n) {
      name = base::ReplaceStringPlaceholders(L10nUTF8("workspaces.new"),
                                             {base::NumberToString(n)}, nullptr);
      if (!taken(name)) {
        break;
      }
    }
  }
  // Names are how groups find their workspace: no two alike.
  std::string unique = name;
  for (int n = 2; taken(unique); ++n) {
    unique = base::StrCat({name, " ", base::NumberToString(n)});
  }
  workspace.name = std::move(unique);
  return workspace;
}

tab_groups::TabGroupVisualData VisualsOf(const Workspace& workspace) {
  return tab_groups::TabGroupVisualData(
      base::UTF8ToUTF16(workspace.name),
      static_cast<tab_groups::TabGroupColorId>(workspace.color));
}

TabStripModel* ModelOf(BrowserWindowInterface* browser) {
  TabStripModel* model = browser ? browser->GetTabStripModel() : nullptr;
  return model && model->SupportsTabGroups() ? model : nullptr;
}

std::string TitleOf(TabStripModel* model, const tab_groups::TabGroupId& group) {
  const TabGroup* tab_group = model->group_model()->GetTabGroup(group);
  return tab_group && tab_group->visual_data()
             ? base::UTF16ToUTF8(tab_group->visual_data()->title())
             : std::string();
}

// The group of a window that is workspace `name`.
std::optional<tab_groups::TabGroupId> GroupOf(TabStripModel* model,
                                              std::string_view name) {
  for (const tab_groups::TabGroupId& group :
       model->group_model()->ListTabGroups()) {
    if (TitleOf(model, group) == name) {
      return group;
    }
  }
  return std::nullopt;
}

bool SameProfile(BrowserWindowInterface* browser, Profile* profile) {
  return browser->GetProfile()->GetOriginalProfile() ==
         profile->GetOriginalProfile();
}

// Keeps the tabs of a window in workspaces: every unpinned tab is in one (a group),
// and one is always shown (focused).
class Keeper : public TabStripModelObserver {
 public:
  explicit Keeper(BrowserWindowInterface* browser) : browser_(browser) {
    browser_->GetTabStripModel()->AddObserver(this);
    subscription_ = ObserveWorkspaces(
        browser_->GetProfile(),
        base::BindRepeating(&Keeper::Schedule, weak_factory_.GetWeakPtr()));
    Schedule();
  }
  Keeper(const Keeper&) = delete;
  Keeper& operator=(const Keeper&) = delete;
  ~Keeper() override = default;

  // The tab last shown in a group, to show again when switching back to it.
  tabs::TabInterface* LastActiveTab(const tab_groups::TabGroupId& group) {
    auto it = last_active_.find(group);
    return it != last_active_.end() ? it->second.get() : nullptr;
  }

  const std::string& last_shown() const { return last_shown_; }

  // Remembers that `group` is workspace `id`, should something else (e.g. saving
  // the group for sync) rename it.
  void Adopt(const tab_groups::TabGroupId& group, const std::string& id) {
    workspace_of_[group] = id;
  }

  // Remembers the tab shown in a workspace (group), to show it again later.
  void Remember(const tab_groups::TabGroupId& group, tabs::TabInterface* tab) {
    last_active_[group] = tab->GetWeakPtr();
  }

  // TabStripModelObserver:
  void OnTabStripModelChanged(TabStripModel* model,
                              const TabStripModelChange& change,
                              const TabStripSelectionChange& selection) override {
    if (selection.active_tab_changed() && selection.new_tab &&
        selection.new_tab->GetGroup()) {
      last_active_[*selection.new_tab->GetGroup()] =
          selection.new_tab->GetWeakPtr();
    }
    Schedule();
  }

  void OnTabGroupFocusChanged(
      std::optional<tab_groups::TabGroupId> new_focused_group_id,
      std::optional<tab_groups::TabGroupId> old_focused_group_id) override {
    if (new_focused_group_id) {
      last_shown_ = TitleOf(browser_->GetTabStripModel(), *new_focused_group_id);
    }
    Schedule();
  }

  void OnTabGroupChanged(const TabGroupChange& change) override {
    // Renamed in the group's editor: so is the workspace.
    if (change.type == TabGroupChange::kVisualsChanged) {
      const TabGroupChange::VisualsChange* visuals = change.GetVisualsChange();
      if (visuals && visuals->old_visuals && visuals->new_visuals &&
          visuals->old_visuals->title() != visuals->new_visuals->title() &&
          !visuals->new_visuals->title().empty()) {
        Profile* profile = browser_->GetProfile();
        const std::string old_name =
            base::UTF16ToUTF8(visuals->old_visuals->title());
        for (Workspace& workspace : GetWorkspaces(profile)) {
          if (workspace.name == old_name) {
            workspace.name = base::UTF16ToUTF8(visuals->new_visuals->title());
            UpdateWorkspace(profile, workspace);
            if (last_shown_ == old_name) {
              last_shown_ = workspace.name;
            }
            break;
          }
        }
      }
    }
    Schedule();
  }

  void OnTabStripModelDestroyed(TabStripModel* model) override {
    destroyed_ = true;
  }

 private:
  // The model can't change while it tells its observers about a change: later.
  void Schedule() {
    if (scheduled_ || destroyed_) {
      return;
    }
    scheduled_ = true;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&Keeper::Fix, weak_factory_.GetWeakPtr()));
  }

  void Fix() {
    scheduled_ = false;
    TabStripModel* model = destroyed_ ? nullptr : ModelOf(browser_);
    if (!model || model->empty()) {
      return;
    }
    Profile* profile = browser_->GetProfile();

    // Every group is a workspace (e.g. one made with "Add tab to new group").
    for (const tab_groups::TabGroupId& group :
         model->group_model()->ListTabGroups()) {
      const std::string title = TitleOf(model, group);
      const std::vector<Workspace> workspaces = GetWorkspaces(profile);
      auto named = std::ranges::find(workspaces, title, &Workspace::name);
      if (!title.empty() && named != workspaces.end()) {
        workspace_of_[group] = named->id;
        continue;
      }
      // A workspace's group renamed by something else: its name again.
      if (auto it = workspace_of_.find(group); it != workspace_of_.end()) {
        auto known = std::ranges::find(workspaces, it->second, &Workspace::id);
        if (known != workspaces.end()) {
          model->ChangeTabGroupVisuals(group, VisualsOf(*known));
          continue;
        }
      }
      const Workspace workspace = AddWorkspace(profile, title);
      model->ChangeTabGroupVisuals(group, VisualsOf(workspace));
      workspace_of_[group] = workspace.id;
    }

    // Unpinned tabs in no workspace go to the one shown.
    std::vector<int> loose;
    for (int i = 0; i < model->count(); ++i) {
      const tabs::TabInterface* tab = model->GetTabAtIndex(i);
      if (!tab->IsPinned() && !tab->GetGroup()) {
        loose.push_back(i);
      }
    }
    if (!loose.empty()) {
      std::optional<tab_groups::TabGroupId> group = model->GetFocusedGroup();
      if (!group) {
        group = GroupOf(model, last_shown_);
      }
      if (!group) {
        for (const Workspace& workspace : GetWorkspaces(profile)) {
          if ((group = GroupOf(model, workspace.name))) {
            break;
          }
        }
      }
      if (group) {
        model->AddToExistingGroup(loose, *group);
      } else {
        std::optional<Workspace> workspace;
        for (const Workspace& candidate : GetWorkspaces(profile)) {
          if (!workspace || candidate.name == last_shown_) {
            workspace = candidate;
          }
        }
        const tab_groups::TabGroupId new_group = model->AddToNewGroup(loose);
        model->ChangeTabGroupVisuals(new_group, VisualsOf(*workspace));
        workspace_of_[new_group] = workspace->id;
      }
    }

    // One of them is always shown: the active tab's, else the last one shown.
    if (!model->GetFocusedGroup()) {
      std::optional<tab_groups::TabGroupId> group;
      if (const tabs::TabInterface* active = model->GetActiveTab()) {
        group = active->GetGroup();
      }
      if (!group) {
        group = GroupOf(model, last_shown_);
      }
      if (!group) {
        const std::vector<tab_groups::TabGroupId> groups =
            model->group_model()->ListTabGroups();
        if (!groups.empty()) {
          group = groups.front();
        }
      }
      if (group) {
        model->EnterFocusMode(*group);
      }
    }
  }

  raw_ptr<BrowserWindowInterface> browser_;
  bool scheduled_ = false;
  bool destroyed_ = false;
  std::string last_shown_;
  std::map<tab_groups::TabGroupId, base::WeakPtr<tabs::TabInterface>>
      last_active_;
  std::map<tab_groups::TabGroupId, std::string> workspace_of_;
  base::CallbackListSubscription subscription_;
  base::WeakPtrFactory<Keeper> weak_factory_{this};
};

// A Keeper for each window with tabs.
class Keepers : public BrowserCollectionObserver {
 public:
  static Keepers& Get() {
    static base::NoDestructor<Keepers> keepers;
    return *keepers;
  }

  void Start() {
    if (started_) {
      return;
    }
    started_ = true;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [this](BrowserWindowInterface* browser) {
          OnBrowserCreated(browser);
          return true;
        });
    observation_.Observe(GlobalBrowserCollection::GetInstance());
  }

  Keeper* For(BrowserWindowInterface* browser) {
    auto it = keepers_.find(browser);
    return it != keepers_.end() ? it->second.get() : nullptr;
  }

  // BrowserCollectionObserver:
  void OnBrowserCreated(BrowserWindowInterface* browser) override {
    if (browser->GetType() == BrowserWindowInterface::Type::TYPE_NORMAL &&
        ModelOf(browser) && !keepers_.contains(browser)) {
      keepers_[browser] = std::make_unique<Keeper>(browser);
    }
  }

  void OnBrowserClosed(BrowserWindowInterface* browser) override {
    keepers_.erase(browser);
  }

 private:
  bool started_ = false;
  std::map<BrowserWindowInterface*, std::unique_ptr<Keeper>> keepers_;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      observation_{this};
};

}  // namespace

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterListPref(kPref);
  registry->RegisterDictionaryPref(kDefaultThemePref);
  registry->RegisterBooleanPref(kShowHeaderPref, false);
}

std::vector<Workspace> GetWorkspaces(Profile* profile) {
  std::vector<Workspace> workspaces;
  for (const base::Value& value : PrefsOf(profile)->GetList(kPref)) {
    if (std::optional<Workspace> workspace = FromDict(value)) {
      workspaces.push_back(std::move(*workspace));
    }
  }
  if (workspaces.empty()) {
    workspaces.push_back(NewWorkspace({}, L10nUTF8("workspaces.home")));
    Save(profile, workspaces);
  }
  return workspaces;
}

std::optional<Workspace> FindWorkspace(Profile* profile, std::string_view id) {
  for (Workspace& workspace : GetWorkspaces(profile)) {
    if (workspace.id == id) {
      return std::move(workspace);
    }
  }
  return std::nullopt;
}

Workspace AddWorkspace(Profile* profile, std::string name) {
  std::vector<Workspace> workspaces = GetWorkspaces(profile);
  Workspace workspace = NewWorkspace(workspaces, std::move(name));
  workspaces.push_back(workspace);
  Save(profile, workspaces);
  return workspace;
}

void UpdateWorkspace(Profile* profile, const Workspace& workspace) {
  std::vector<Workspace> workspaces = GetWorkspaces(profile);
  auto it = std::ranges::find(workspaces, workspace.id, &Workspace::id);
  if (it == workspaces.end()) {
    return;
  }
  const std::string old_name = it->name;
  Workspace updated = workspace;
  if (updated.name.empty()) {
    updated.name = old_name;
  }
  // Still no two alike.
  for (int n = 2; std::ranges::any_of(workspaces, [&](const Workspace& w) {
         return w.id != updated.id && w.name == updated.name;
       });
       ++n) {
    updated.name = base::StrCat({workspace.name, " ", base::NumberToString(n)});
  }
  *it = updated;
  Save(profile, workspaces);
  // Its groups take its new name and color.
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&](BrowserWindowInterface* browser) {
        TabStripModel* model = ModelOf(browser);
        if (!model || !SameProfile(browser, profile)) {
          return true;
        }
        if (std::optional<tab_groups::TabGroupId> group =
                GroupOf(model, old_name)) {
          model->ChangeTabGroupVisuals(*group, VisualsOf(updated));
        }
        return true;
      });
}

void RemoveWorkspace(Profile* profile, std::string_view id) {
  std::vector<Workspace> workspaces = GetWorkspaces(profile);
  auto it = std::ranges::find(workspaces, id, &Workspace::id);
  if (it == workspaces.end() || workspaces.size() < 2) {
    return;
  }
  const Workspace removed = *it;
  const Workspace& other = it == workspaces.begin() ? *(it + 1) : *(it - 1);
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&](BrowserWindowInterface* browser) {
        TabStripModel* model = ModelOf(browser);
        if (!model || !SameProfile(browser, profile)) {
          return true;
        }
        const std::optional<tab_groups::TabGroupId> group =
            GroupOf(model, removed.name);
        if (!group) {
          return true;
        }
        if (model->GetFocusedGroup() == group) {
          ShowNow(browser, other.id);
        }
        const gfx::Range tabs =
            model->group_model()->GetTabGroup(*group)->ListTabs();
        for (int i = static_cast<int>(tabs.end()) - 1;
             i >= static_cast<int>(tabs.start()); --i) {
          model->CloseWebContentsAt(i, TabCloseTypes::CLOSE_USER_GESTURE);
        }
        return true;
      });
  workspaces.erase(std::ranges::find(workspaces, id, &Workspace::id));
  Save(profile, workspaces);
}

base::CallbackListSubscription ObserveWorkspaces(Profile* profile,
                                                 base::RepeatingClosure changed) {
  return ProfileWorkspaces::Get(profile).observers.Add(std::move(changed));
}

Theme DefaultTheme(Profile* profile) {
  const base::DictValue& saved = PrefsOf(profile)->GetDict(kDefaultThemePref);
  return saved.empty() ? FirstDefaultTheme() : ThemeFromDict(saved);
}

void SetDefaultTheme(Profile* profile, const Theme& theme) {
  PrefsOf(profile)->SetDict(kDefaultThemePref, ThemeToDict(theme));
  ProfileWorkspaces::Get(profile).observers.Notify();
}

void ResetDefaultTheme(Profile* profile) {
  PrefsOf(profile)->ClearPref(kDefaultThemePref);
  ProfileWorkspaces::Get(profile).observers.Notify();
}

bool IsDefaultThemeReset(Profile* profile) {
  return DefaultTheme(profile) == FirstDefaultTheme();
}

Theme ThemeOf(Profile* profile, const Workspace& workspace) {
  return workspace.theme ? *workspace.theme : DefaultTheme(profile);
}

bool ShowsHeader(Profile* profile) {
  return PrefsOf(profile)->GetBoolean(kShowHeaderPref);
}

std::optional<Workspace> CurrentWorkspace(BrowserWindowInterface* browser) {
  TabStripModel* model = ModelOf(browser);
  if (!model) {
    return std::nullopt;
  }
  std::optional<tab_groups::TabGroupId> group = model->GetFocusedGroup();
  std::string name = group ? TitleOf(model, *group) : std::string();
  // Between a workspace's last tab closing and the next one being shown.
  if (name.empty()) {
    if (Keeper* keeper = Keepers::Get().For(browser)) {
      name = keeper->last_shown();
    }
  }
  for (Workspace& workspace : GetWorkspaces(browser->GetProfile())) {
    if (workspace.name == name) {
      return std::move(workspace);
    }
  }
  return std::nullopt;
}

namespace {

std::map<BrowserWindowInterface*, SwitchAnimator>& Animators() {
  static base::NoDestructor<std::map<BrowserWindowInterface*, SwitchAnimator>>
      animators;
  return *animators;
}

int IndexOf(const std::vector<Workspace>& workspaces, std::string_view id) {
  auto it = std::ranges::find(workspaces, id, &Workspace::id);
  return it == workspaces.end() ? -1 : it - workspaces.begin();
}

// Shows `id`, through the window's animation if it has one.
void Switch(BrowserWindowInterface* browser, std::string_view id, bool forward) {
  const std::optional<Workspace> current = CurrentWorkspace(browser);
  auto it = Animators().find(browser);
  if (it != Animators().end() && current && current->id != id) {
    it->second.Run(std::string(id), forward);
    return;
  }
  ShowNow(browser, id);
}

}  // namespace

void SetSwitchAnimator(BrowserWindowInterface* browser, SwitchAnimator animator) {
  if (animator) {
    Animators()[browser] = std::move(animator);
  } else {
    Animators().erase(browser);
  }
}

void SwitchTo(BrowserWindowInterface* browser, std::string_view id) {
  const std::vector<Workspace> workspaces =
      GetWorkspaces(browser->GetProfile());
  const std::optional<Workspace> current = CurrentWorkspace(browser);
  Switch(browser, id,
         !current || IndexOf(workspaces, id) > IndexOf(workspaces, current->id));
}

bool CloseLastTabs(BrowserWindowInterface* browser,
                   const std::vector<const tabs::TabInterface*>& tabs) {
  TabStripModel* model = ModelOf(browser);
  const std::optional<tab_groups::TabGroupId> shown =
      model ? model->GetFocusedGroup() : std::nullopt;
  if (!shown) {
    return false;
  }
  // Its last tabs, or not; New Tab pages, or not.
  bool new_tab_pages = true;
  for (int i = 0; i < model->count(); ++i) {
    const tabs::TabInterface* tab = model->GetTabAtIndex(i);
    if (tab->IsPinned() || tab->GetGroup() != shown) {
      continue;
    }
    if (!std::ranges::contains(tabs, tab)) {
      return false;
    }
    const GURL url =
        tab->GetContents() ? tab->GetContents()->GetVisibleURL() : GURL();
    new_tab_pages &= url.SchemeIs(content::kChromeUIScheme) &&
                     (url.host() == chrome::kChromeUINewTabHost ||
                      url.host() == chrome::kChromeUINewTabPageHost);
  }
  if (new_tab_pages) {
    chrome::CloseWindow(browser);
    return true;
  }
  // A New Tab page in their place (in the workspace's container), then they go.
  std::vector<content::WebContents*> closing;
  for (const tabs::TabInterface* tab : tabs) {
    const int index = model->GetIndexOfTab(tab);
    if (index != TabStripModel::kNoTab) {
      closing.push_back(model->GetTabAtIndex(index)->GetContents());
    }
  }
  const std::optional<Workspace> workspace = CurrentWorkspace(browser);
  content::WebContents* contents = containers::OpenInContainer(
      browser, workspace ? workspace->container : std::string(), GURL(),
      WindowOpenDisposition::NEW_FOREGROUND_TAB);
  const int index =
      contents ? model->GetIndexOfWebContents(contents) : TabStripModel::kNoTab;
  if (index == TabStripModel::kNoTab) {
    return false;
  }
  if (model->GetTabAtIndex(index)->GetGroup() != shown) {
    model->AddToExistingGroup({index}, *shown);
  }
  for (content::WebContents* old : closing) {
    if (old) {
      old->Close();
    }
  }
  return true;
}

std::optional<Workspace> Neighbor(BrowserWindowInterface* browser,
                                  bool forward) {
  const std::vector<Workspace> workspaces =
      GetWorkspaces(browser->GetProfile());
  const std::optional<Workspace> current = CurrentWorkspace(browser);
  const int index = current ? IndexOf(workspaces, current->id) : -1;
  const int next = index + (forward ? 1 : -1);
  if (index < 0 || next < 0 || next >= static_cast<int>(workspaces.size())) {
    return std::nullopt;
  }
  return workspaces[next];
}

std::vector<tabs::TabInterface*> TabsOf(BrowserWindowInterface* browser,
                                        std::string_view id) {
  std::vector<tabs::TabInterface*> tabs;
  TabStripModel* model = ModelOf(browser);
  const std::optional<Workspace> workspace =
      model ? FindWorkspace(browser->GetProfile(), id) : std::nullopt;
  const std::optional<tab_groups::TabGroupId> group =
      workspace ? GroupOf(model, workspace->name) : std::nullopt;
  if (!group) {
    return tabs;
  }
  const gfx::Range range = model->group_model()->GetTabGroup(*group)->ListTabs();
  for (uint32_t i = range.start(); i < range.end(); ++i) {
    tabs.push_back(model->GetTabAtIndex(i));
  }
  return tabs;
}

tabs::TabInterface* LastActiveTabOf(BrowserWindowInterface* browser,
                                    std::string_view id) {
  TabStripModel* model = ModelOf(browser);
  const std::optional<Workspace> workspace =
      model ? FindWorkspace(browser->GetProfile(), id) : std::nullopt;
  const std::optional<tab_groups::TabGroupId> group =
      workspace ? GroupOf(model, workspace->name) : std::nullopt;
  Keeper* keeper = Keepers::Get().For(browser);
  tabs::TabInterface* tab =
      group && keeper ? keeper->LastActiveTab(*group) : nullptr;
  return tab && tab->GetGroup() == group ? tab : nullptr;
}

void ShowNow(BrowserWindowInterface* browser, std::string_view id) {
  TabStripModel* model = ModelOf(browser);
  const std::optional<Workspace> workspace =
      model ? FindWorkspace(browser->GetProfile(), id) : std::nullopt;
  if (!workspace) {
    return;
  }
  Keeper* keeper = Keepers::Get().For(browser);
  // The tab shown before, to show again when coming back (switching opens and
  // activates tabs, which the keeper would otherwise take for it).
  tabs::TabInterface* const previous = model->GetActiveTab();
  const std::optional<tab_groups::TabGroupId> previous_group =
      previous ? previous->GetGroup() : std::nullopt;
  absl::Cleanup remember_previous = [&] {
    if (keeper && previous_group &&
        model->GetIndexOfTab(previous) != TabStripModel::kNoTab &&
        previous->GetGroup() == previous_group) {
      keeper->Remember(*previous_group, previous);
    }
  };
  std::optional<tab_groups::TabGroupId> group = GroupOf(model, workspace->name);
  if (!group) {
    // None of its tabs in this window yet: a new one.
    content::WebContents* contents = containers::OpenInContainer(
        browser, workspace->container, GURL(),
        WindowOpenDisposition::NEW_FOREGROUND_TAB);
    const int index =
        contents ? model->GetIndexOfWebContents(contents) : TabStripModel::kNoTab;
    if (index == TabStripModel::kNoTab) {
      return;
    }
    group = model->AddToNewGroup({index});
    model->ChangeTabGroupVisuals(*group, VisualsOf(*workspace));
    if (keeper) {
      keeper->Adopt(*group, workspace->id);
    }
    model->EnterFocusMode(*group);
    return;
  }
  // Back where it was (known before showing the group activates its first tab).
  tabs::TabInterface* const last_active =
      keeper ? keeper->LastActiveTab(*group) : nullptr;
  model->EnterFocusMode(*group);
  if (last_active && last_active->GetGroup() == group) {
    const int index = model->GetIndexOfTab(last_active);
    if (index != TabStripModel::kNoTab) {
      model->ActivateTabAt(index);
    }
  }
}

void SwitchRelative(BrowserWindowInterface* browser, bool forward) {
  const std::vector<Workspace> workspaces =
      GetWorkspaces(browser->GetProfile());
  const std::optional<Workspace> current = CurrentWorkspace(browser);
  auto it = current ? std::ranges::find(workspaces, current->id, &Workspace::id)
                    : workspaces.end();
  const int size = static_cast<int>(workspaces.size());
  const int index = it == workspaces.end() ? 0 : it - workspaces.begin();
  const int next = (index + (forward ? 1 : -1) + size) % size;
  if (next != index || it == workspaces.end()) {
    Switch(browser, workspaces[next].id, forward);
  }
}

void AddAndSwitch(BrowserWindowInterface* browser) {
  SwitchTo(browser, AddWorkspace(browser->GetProfile()).id);
}

void MoveTab(BrowserWindowInterface* browser,
             tabs::TabInterface* tab,
             std::string_view id) {
  TabStripModel* model = ModelOf(browser);
  const std::optional<Workspace> workspace =
      model ? FindWorkspace(browser->GetProfile(), id) : std::nullopt;
  if (!workspace || !tab || tab->IsPinned()) {
    return;
  }
  std::optional<tab_groups::TabGroupId> group = GroupOf(model, workspace->name);
  if (group && tab->GetGroup() == group) {
    return;
  }
  // The window keeps showing the tab's workspace, with another of its tabs.
  if (tab == model->GetActiveTab() && tab->GetGroup()) {
    const gfx::Range tabs =
        model->group_model()->GetTabGroup(*tab->GetGroup())->ListTabs();
    const int index = model->GetIndexOfTab(tab);
    for (int i : {index + 1, index - 1}) {
      if (i >= static_cast<int>(tabs.start()) &&
          i < static_cast<int>(tabs.end())) {
        model->ActivateTabAt(i);
        break;
      }
    }
  }
  const int index = model->GetIndexOfTab(tab);
  if (index == TabStripModel::kNoTab) {
    return;
  }
  if (group) {
    model->AddToExistingGroup({index}, *group, /*add_to_end=*/true);
  } else {
    const tab_groups::TabGroupId new_group = model->AddToNewGroup({index});
    model->ChangeTabGroupVisuals(new_group, VisualsOf(*workspace));
    if (Keeper* keeper = Keepers::Get().For(browser)) {
      keeper->Adopt(new_group, workspace->id);
    }
  }
}

std::string ContainerForNewTabs(BrowserWindowInterface* browser) {
  const std::optional<Workspace> workspace = CurrentWorkspace(browser);
  if (!workspace || workspace->container.empty() ||
      !containers::FindContainer(browser->GetProfile(), workspace->container)) {
    return std::string();
  }
  return workspace->container;
}

void Init() {
  Keepers::Get().Start();
}

}  // namespace oblada::workspaces
