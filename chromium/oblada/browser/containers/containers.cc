#include "oblada/browser/containers/containers.h"

#include <algorithm>
#include <array>
#include <utility>

#include "base/barrier_closure.h"
#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/supports_user_data.h"
#include "base/task/sequenced_task_runner.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/webui_url_constants.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/browser_url_handler.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/security_principal.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/web_contents_user_data.h"
#include "content/public/common/url_constants.h"
#include "oblada/browser/l10n.h"
#include "oblada/browser/workspaces/workspaces.h"
#include "services/network/public/mojom/clear_data_filter.mojom.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "url/gurl.h"

namespace oblada::containers {

namespace {

constexpr char kPref[] = "oblada.containers.list";
// Site (host without "www.") → id of the container it always opens in.
constexpr char kSitesPref[] = "oblada.containers.sites";
// The last container number given out: numbers aren't reused, as add-ons may keep
// data about a container by its number.
constexpr char kLastNumberPref[] = "oblada.containers.last_number";

constexpr char kTemporaryPrefix[] = "temp-";
// Temporary containers' numbers, apart from the others'.
constexpr int kFirstTemporaryNumber = 100000;

// StoragePartitionConfig's domain for every container; the container id is its name.
constexpr char kPartitionDomain[] = "containers";

struct NamedColor {
  std::string_view name;
  SkColor color;
};

// Firefox Multi-Account Containers' palette.
constexpr auto kColors = std::to_array<NamedColor>({
    {"blue", SkColorSetRGB(0x37, 0xad, 0xff)},
    {"turquoise", SkColorSetRGB(0x00, 0xc7, 0x9a)},
    {"green", SkColorSetRGB(0x51, 0xcd, 0x00)},
    {"yellow", SkColorSetRGB(0xff, 0xcb, 0x00)},
    {"orange", SkColorSetRGB(0xff, 0x9f, 0x00)},
    {"red", SkColorSetRGB(0xff, 0x61, 0x3d)},
    {"pink", SkColorSetRGB(0xff, 0x4b, 0xda)},
    {"purple", SkColorSetRGB(0xaf, 0x51, 0xf5)},
    {"toolbar", SkColorSetRGB(0x8f, 0x8f, 0x9d)},
});

bool IsValidId(std::string_view id) {
  return !id.empty() && id.size() <= 64 &&
         std::ranges::all_of(id, [](char c) {
           return base::IsAsciiLower(c) || base::IsAsciiDigit(c) || c == '-';
         });
}

bool IsTemporaryId(std::string_view id) {
  return id.starts_with(kTemporaryPrefix);
}

// Firefox's numbers for the same default containers.
int DefaultNumber(std::string_view id) {
  constexpr std::string_view kDefaults[] = {"personal", "work", "banking", "shopping"};
  int number = 0;
  for (std::string_view default_id : kDefaults) {
    ++number;
    if (default_id == id) {
      return number;
    }
  }
  return 0;
}

base::DictValue ToDict(const Container& container) {
  return base::DictValue()
      .Set("id", container.id)
      .Set("name", container.name)
      .Set("color", container.color)
      .Set("icon", container.icon)
      .Set("number", container.number);
}

std::optional<Container> FromDict(const base::Value& value) {
  const base::DictValue* dict = value.GetIfDict();
  if (!dict) {
    return std::nullopt;
  }
  const std::string* id = dict->FindString("id");
  const std::string* name = dict->FindString("name");
  if (!id || !name || !IsValidId(*id)) {
    return std::nullopt;
  }
  const std::string* color = dict->FindString("color");
  const std::string* icon = dict->FindString("icon");
  return Container{*id, *name, color ? *color : "toolbar", icon ? *icon : "circle",
                   dict->FindInt("number").value_or(0)};
}

PrefService* PrefsOf(Profile* profile) {
  // Incognito windows use the same containers (with in-memory storage).
  return profile->GetOriginalProfile()->GetPrefs();
}

// Per profile: its temporary containers, and who to tell about changes.
class ProfileContainers : public base::SupportsUserData::Data {
 public:
  static ProfileContainers& Get(Profile* profile) {
    Profile* original = profile->GetOriginalProfile();
    auto* data = static_cast<ProfileContainers*>(original->GetUserData(&kKey));
    if (!data) {
      auto owned = std::make_unique<ProfileContainers>();
      data = owned.get();
      original->SetUserData(&kKey, std::move(owned));
    }
    return *data;
  }

  std::vector<Container> temporary;
  int temporary_count = 0;
  base::RepeatingClosureList observers;

 private:
  static constexpr int kKey = 0;
};

base::RepeatingCallbackList<void(Profile*, Change, const Container&)>&
ChangeObservers() {
  static base::NoDestructor<
      base::RepeatingCallbackList<void(Profile*, Change, const Container&)>>
      observers;
  return *observers;
}

void NotifyChanged(Profile* profile) {
  ProfileContainers::Get(profile).observers.Notify();
}

void NotifyChange(Profile* profile, Change change, const Container& container) {
  ChangeObservers().Notify(profile->GetOriginalProfile(), change, container);
  NotifyChanged(profile);
}

// The last number given out: never below one in use (the pref may be missing, e.g.
// for containers saved before containers had numbers).
int LastNumber(PrefService* prefs) {
  int last = prefs->GetInteger(kLastNumberPref);
  for (const base::Value& value : prefs->GetList(kPref)) {
    if (value.is_dict()) {
      last = std::max(last, value.GetDict().FindInt("number").value_or(0));
    }
  }
  return last;
}

// Gives numbers to containers saved before they had any.
void EnsureNumbers(PrefService* prefs) {
  const base::ListValue& list = prefs->GetList(kPref);
  if (std::ranges::all_of(list, [](const base::Value& value) {
        return !value.is_dict() || value.GetDict().FindInt("number").value_or(0) > 0;
      })) {
    return;
  }
  int last = LastNumber(prefs);
  ScopedListPrefUpdate update(prefs, kPref);
  for (base::Value& value : *update) {
    std::optional<Container> container = FromDict(value);
    if (container && container->number <= 0) {
      container->number = DefaultNumber(container->id);
      if (!container->number) {
        container->number = ++last;
      }
      value = base::Value(ToDict(*container));
    }
  }
  prefs->SetInteger(kLastNumberPref, last);
}

scoped_refptr<content::SiteInstance> CreateSiteInstance(Profile* profile,
                                                        std::string_view id,
                                                        GURL url) {
  // Like tab_util::GetSiteInstanceForNewTab: e.g. chrome://newtab is really loaded
  // from another URL, and the SiteInstance should be for the one that loads.
  content::BrowserURLHandler::GetInstance()->RewriteURLIfNecessary(&url, profile);
  return content::SiteInstance::CreateForFixedStoragePartition(
      profile, url, PartitionFor(profile, id));
}

}  // namespace

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry,
                          const std::string& locale) {
  const bool italian = locale.starts_with("it");
  struct Default {
    const char* id;
    const char* en;
    const char* it;
    const char* color;
    const char* icon;
  };
  constexpr Default kDefaults[] = {
      {"personal", "Personal", "Personale", "blue", "fingerprint"},
      {"work", "Work", "Lavoro", "orange", "briefcase"},
      {"banking", "Banking", "Banca", "green", "circle-dollar-sign"},
      {"shopping", "Shopping", "Acquisti", "pink", "shopping-cart"},
  };
  base::ListValue defaults;
  for (const Default& d : kDefaults) {
    defaults.Append(ToDict({d.id, italian ? d.it : d.en, d.color, d.icon,
                            DefaultNumber(d.id)}));
  }
  registry->RegisterListPref(kPref, std::move(defaults));
  registry->RegisterDictionaryPref(kSitesPref);
  registry->RegisterIntegerPref(kLastNumberPref, std::size(kDefaults));
}

std::vector<Container> GetContainers(Profile* profile) {
  EnsureNumbers(PrefsOf(profile));
  std::vector<Container> containers;
  for (const base::Value& value : PrefsOf(profile)->GetList(kPref)) {
    if (std::optional<Container> container = FromDict(value)) {
      containers.push_back(std::move(*container));
    }
  }
  for (const Container& container : ProfileContainers::Get(profile).temporary) {
    containers.push_back(container);
  }
  return containers;
}

std::optional<Container> FindContainer(Profile* profile, std::string_view id) {
  for (Container& container : GetContainers(profile)) {
    if (container.id == id) {
      return std::move(container);
    }
  }
  return std::nullopt;
}

std::optional<Container> FindContainerByNumber(Profile* profile, int number) {
  for (Container& container : GetContainers(profile)) {
    if (container.number == number) {
      return std::move(container);
    }
  }
  return std::nullopt;
}

Container AddContainer(Profile* profile,
                       std::string_view name,
                       std::string_view color,
                       std::string_view icon) {
  PrefService* prefs = PrefsOf(profile);
  EnsureNumbers(prefs);
  const int number = LastNumber(prefs) + 1;
  prefs->SetInteger(kLastNumberPref, number);
  Container container{"container-" + base::HexEncodeLower(base::RandBytesAsVector(5)),
                      std::string(name), std::string(color), std::string(icon),
                      number};
  ScopedListPrefUpdate(prefs, kPref)->Append(ToDict(container));
  NotifyChange(profile, Change::kCreated, container);
  return container;
}

namespace {

Container AddTemporaryContainerWithId(Profile* profile, std::string id) {
  ProfileContainers& data = ProfileContainers::Get(profile);
  ++data.temporary_count;
  const std::vector<std::string_view>& colors = ColorNames();
  Container container{
      std::move(id),
      L10nUTF8("containers.temporary") + " " +
          base::NumberToString(data.temporary_count),
      std::string(colors[(data.temporary_count - 1) % (colors.size() - 1)]),
      "fence", kFirstTemporaryNumber + data.temporary_count,
      /*temporary=*/true};
  data.temporary.push_back(container);
  NotifyChange(profile, Change::kCreated, container);
  return container;
}

}  // namespace

Container AddTemporaryContainer(Profile* profile) {
  return AddTemporaryContainerWithId(
      profile, kTemporaryPrefix + base::HexEncodeLower(base::RandBytesAsVector(5)));
}

int TemporaryNumber(const Container& container) {
  return container.temporary ? container.number - kFirstTemporaryNumber : 0;
}

void UpdateContainer(Profile* profile, const Container& container) {
  for (Container& temporary : ProfileContainers::Get(profile).temporary) {
    if (temporary.id == container.id) {
      temporary.name = container.name;
      temporary.color = container.color;
      temporary.icon = container.icon;
    }
  }
  if (!IsTemporaryId(container.id)) {
    ScopedListPrefUpdate update(PrefsOf(profile), kPref);
    for (base::Value& value : *update) {
      std::optional<Container> existing = FromDict(value);
      if (existing && existing->id == container.id) {
        Container updated = container;
        updated.number = existing->number;
        value = base::Value(ToDict(updated));
      }
    }
  }
  if (std::optional<Container> updated = FindContainer(profile, container.id)) {
    NotifyChange(profile, Change::kUpdated, *updated);
  }
}

void MoveContainer(Profile* profile, std::string_view id, size_t index) {
  ScopedListPrefUpdate update(PrefsOf(profile), kPref);
  base::ListValue& list = update.Get();
  for (size_t i = 0; i < list.size(); ++i) {
    std::optional<Container> container = FromDict(list[i]);
    if (container && container->id == id) {
      base::Value moved = list[i].Clone();
      list.erase(list.begin() + i);
      list.Insert(list.begin() + std::min(index, list.size()), std::move(moved));
      break;
    }
  }
  NotifyChanged(profile);
}

base::CallbackListSubscription ObserveContainers(Profile* profile,
                                                 base::RepeatingClosure changed) {
  return ProfileContainers::Get(profile).observers.Add(std::move(changed));
}

base::CallbackListSubscription ObserveContainerChanges(ChangeCallback callback) {
  return ChangeObservers().Add(std::move(callback));
}

std::string SiteOf(const GURL& url) {
  if (!url.SchemeIsHTTPOrHTTPS() || url.host().empty()) {
    return std::string();
  }
  std::string host(url.host());
  if (host.starts_with("www.")) {
    host.erase(0, 4);
  }
  return host;
}

std::string AssignedContainer(Profile* profile, const GURL& url) {
  const std::string site = SiteOf(url);
  const std::string* id =
      site.empty() ? nullptr : PrefsOf(profile)->GetDict(kSitesPref).FindString(site);
  return id && FindContainer(profile, *id) ? *id : std::string();
}

void AssignSite(Profile* profile, std::string_view site, std::string_view id) {
  if (site.empty()) {
    return;
  }
  ScopedDictPrefUpdate update(PrefsOf(profile), kSitesPref);
  if (id.empty()) {
    update->Remove(site);
  } else {
    update->Set(site, id);
  }
  NotifyChanged(profile);
}

std::vector<std::string> AssignedSites(Profile* profile, std::string_view id) {
  std::vector<std::string> sites;
  for (const auto [site, value] : PrefsOf(profile)->GetDict(kSitesPref)) {
    if (value.is_string() && value.GetString() == id) {
      sites.push_back(site);
    }
  }
  return sites;
}

void RemoveContainer(Profile* profile, std::string_view id) {
  const std::string container_id(id);
  const std::optional<Container> removed = FindContainer(profile, container_id);
  for (BrowserWindowInterface* browser : GetAllBrowserWindowInterfaces()) {
    if (browser->GetProfile()->GetOriginalProfile() != profile->GetOriginalProfile()) {
      continue;
    }
    TabStripModel* tabs = browser->GetTabStripModel();
    for (int i = tabs->count() - 1; i >= 0; --i) {
      if (GetContainerId(tabs->GetWebContentsAt(i)) == container_id) {
        tabs->CloseWebContentsAt(i, TabCloseTypes::CLOSE_NONE);
      }
    }
  }
  ScopedListPrefUpdate(PrefsOf(profile), kPref)->EraseIf([&](const base::Value& value) {
    std::optional<Container> container = FromDict(value);
    return container && container->id == container_id;
  });
  std::erase_if(ProfileContainers::Get(profile).temporary,
                [&](const Container& container) { return container.id == container_id; });
  for (const std::string& site : AssignedSites(profile, container_id)) {
    ScopedDictPrefUpdate(PrefsOf(profile), kSitesPref)->Remove(site);
  }
  ClearContainerData(profile, container_id, base::DoNothing());
  if (removed) {
    NotifyChange(profile, Change::kRemoved, *removed);
  } else {
    NotifyChanged(profile);
  }
}

void ClearContainerData(Profile* profile,
                        std::string_view id,
                        base::OnceClosure done) {
  content::StoragePartition* partition =
      profile->GetStoragePartition(PartitionFor(profile, id), /*can_create=*/false);
  if (!partition) {
    std::move(done).Run();
    return;
  }
  base::RepeatingClosure barrier = base::BarrierClosure(2, std::move(done));
  partition->ClearData(content::StoragePartition::REMOVE_DATA_MASK_ALL,
                       /*filter_builder=*/nullptr,
                       /*storage_key_policy_matcher=*/{},
                       network::mojom::CookieDeletionFilter::New(),
                       /*perform_storage_cleanup=*/true, base::Time(),
                       base::Time::Max(), barrier);
  partition->GetNetworkContext()->ClearHttpCache(base::Time(), base::Time::Max(),
                                                 /*filter=*/nullptr, barrier);
}

SkColor ColorFor(std::string_view color) {
  for (const NamedColor& named : kColors) {
    if (named.name == color) {
      return named.color;
    }
  }
  return gfx::kGoogleGrey500;
}

const std::vector<std::string_view>& ColorNames() {
  static const base::NoDestructor<std::vector<std::string_view>> names([] {
    std::vector<std::string_view> list;
    for (const NamedColor& named : kColors) {
      list.push_back(named.name);
    }
    return list;
  }());
  return *names;
}

std::string GetContainerId(content::WebContents* contents) {
  return contents ? GetContainerId(contents->GetSiteInstance()) : std::string();
}

std::string GetContainerId(content::SiteInstance* site_instance) {
  if (!site_instance) {
    return std::string();
  }
  const content::StoragePartitionConfig& config =
      site_instance->GetSecurityPrincipal().GetStoragePartitionConfig();
  if (config.partition_domain() != kPartitionDomain ||
      !IsValidId(config.partition_name())) {
    return std::string();
  }
  return config.partition_name();
}

content::StoragePartitionConfig PartitionFor(Profile* profile, std::string_view id) {
  // Temporary containers keep nothing on disk.
  return content::StoragePartitionConfig::Create(
      profile, kPartitionDomain, id,
      profile->IsOffTheRecord() || IsTemporaryId(id));
}

namespace {

// On a tab opened in a container by the user's choice: its first page stays there,
// even if its site always opens in another container.
constexpr int kChosenContainerKey = 0;

content::WebContents* OpenInContainerAt(BrowserWindowInterface* browser,
                                        std::string_view id,
                                        const GURL& url,
                                        WindowOpenDisposition disposition,
                                        std::optional<int> index,
                                        bool pinned = false) {
  Profile* profile = browser->GetProfile();
  const GURL target = url.is_empty() ? GURL(chrome::kChromeUINewTabURL) : url;
  // Containers are for web pages (and the New Tab page); others, e.g. extension
  // pages, belong to the profile.
  if (!target.SchemeIsHTTPOrHTTPS() && target != GURL(chrome::kChromeUINewTabURL)) {
    id = std::string_view();
  }
  content::WebContents::CreateParams create_params(
      profile,
      id.empty() ? nullptr : CreateSiteInstance(profile, id, target));
  create_params.initially_hidden =
      disposition == WindowOpenDisposition::NEW_BACKGROUND_TAB;
  std::unique_ptr<content::WebContents> contents =
      content::WebContents::Create(create_params);
  contents->SetUserData(&kChosenContainerKey,
                        std::make_unique<base::SupportsUserData::Data>());

  NavigateParams params(browser, std::move(contents));
  params.disposition = disposition;
  if (index) {
    params.tabstrip_index = *index;
    params.tabstrip_add_types |= AddTabTypes::ADD_FORCE_INDEX;
  }
  if (pinned) {
    params.tabstrip_add_types |= AddTabTypes::ADD_PINNED;
  }
  Navigate(&params);

  // Load only once it is a tab, so every tab helper sees the navigation.
  content::WebContents* opened = params.navigated_or_inserted_contents;
  if (opened) {
    content::NavigationController::LoadURLParams load(target);
    load.transition_type = url.is_empty() ? ui::PAGE_TRANSITION_AUTO_TOPLEVEL
                                          : ui::PAGE_TRANSITION_LINK;
    opened->GetController().LoadURLWithParams(load);
  }
  return opened;
}

// Whether the tab shows something worth keeping: not just the New Tab page.
bool ShowsSomething(content::WebContents* contents) {
  content::NavigationEntry* entry = contents->GetController().GetLastCommittedEntry();
  if (!entry || entry->IsInitialEntry()) {
    return false;
  }
  const GURL& url = entry->GetURL();
  return !url.IsAboutBlank() && !(url.SchemeIs(content::kChromeUIScheme) &&
                                  url.host() == chrome::kChromeUINewTabHost);
}

}  // namespace

content::WebContents* OpenInContainer(BrowserWindowInterface* browser,
                                      std::string_view id,
                                      const GURL& url,
                                      WindowOpenDisposition disposition) {
  return OpenInContainerAt(browser, id, url, disposition, std::nullopt);
}

base::expected<content::WebContents*, std::string> OpenTabInContainer(
    BrowserWindowInterface* browser,
    std::string_view id,
    const GURL& url,
    bool active,
    bool pinned,
    int index) {
  content::WebContents* contents = OpenInContainerAt(
      browser, id, url,
      active ? WindowOpenDisposition::NEW_FOREGROUND_TAB
             : WindowOpenDisposition::NEW_BACKGROUND_TAB,
      index >= 0 ? std::optional<int>(index) : std::nullopt, pinned);
  if (!contents) {
    return base::unexpected("Couldn't open the tab");
  }
  return contents;
}

void ReopenInContainer(content::WebContents* contents,
                       std::string_view id,
                       const GURL& url,
                       bool keep_original) {
  tabs::TabInterface* tab = tabs::TabInterface::MaybeGetFromContents(contents);
  if (!tab || !tab->GetBrowserWindowInterface()) {
    return;
  }
  BrowserWindowInterface* browser = tab->GetBrowserWindowInterface();
  TabStripModel* tabs = browser->GetTabStripModel();
  const int index = tabs->GetIndexOfWebContents(contents);
  if (index == TabStripModel::kNoTab) {
    return;
  }
  OpenInContainerAt(browser, id, url,
                    tabs->active_index() == index
                        ? WindowOpenDisposition::NEW_FOREGROUND_TAB
                        : WindowOpenDisposition::NEW_BACKGROUND_TAB,
                    index + 1);
  if (!keep_original) {
    const int original = tabs->GetIndexOfWebContents(contents);
    if (original != TabStripModel::kNoTab) {
      tabs->CloseWebContentsAt(original, TabCloseTypes::CLOSE_NONE);
    }
  }
}

scoped_refptr<content::SiteInstance> SiteInstanceForNewTab(const NavigateParams& params,
                                                           const GURL& url) {
  std::string id;
  if (params.source_site_instance) {
    id = GetContainerId(params.source_site_instance.get());
  }
  // Links opened in a new tab from a container tab, whatever opened them (a click, the
  // context menu): not a new tab the user asked for, e.g. typed in the address bar.
  if (id.empty() && params.source_contents &&
      (params.is_renderer_initiated || params.started_from_context_menu ||
       ui::PageTransitionCoreTypeIs(params.transition, ui::PAGE_TRANSITION_LINK))) {
    id = GetContainerId(params.source_contents.get());
  }
  // Else the container of the window's workspace, if it has one.
  if (id.empty() && params.browser) {
    id = workspaces::ContainerForNewTabs(params.browser);
  }
  Profile* profile = params.browser ? params.browser->GetProfile() : nullptr;
  if (id.empty() || !profile || !FindContainer(profile, id)) {
    return nullptr;
  }
  return CreateSiteInstance(profile, id, url);
}

scoped_refptr<content::SiteInstance> SiteInstanceForRestoredTab(
    Profile* profile,
    const GURL& url,
    const std::map<std::string, std::string>& extra_data) {
  auto it = extra_data.find(kSessionKey);
  if (it == extra_data.end() || !IsValidId(it->second)) {
    return nullptr;
  }
  // A temporary container that is gone comes back, empty, for its tabs.
  if (IsTemporaryId(it->second) && !FindContainer(profile, it->second)) {
    AddTemporaryContainerWithId(profile, it->second);
  }
  // A deleted container's data is gone: its tabs come back as normal tabs.
  if (!FindContainer(profile, it->second)) {
    return nullptr;
  }
  return CreateSiteInstance(profile, it->second, url);
}

scoped_refptr<content::SiteInstance> SiteInstanceForDiscardedTab(
    content::WebContents* contents) {
  const std::string id = GetContainerId(contents);
  if (id.empty()) {
    return nullptr;
  }
  return CreateSiteInstance(
      Profile::FromBrowserContext(contents->GetBrowserContext()), id,
      contents->GetLastCommittedURL());
}

namespace {

// Forgets a temporary container, and its data, once its last tab is gone.
void MaybeRemoveTemporary(base::WeakPtr<Profile> profile, std::string id) {
  if (!profile || !FindContainer(profile.get(), id)) {
    return;
  }
  for (BrowserWindowInterface* browser : GetAllBrowserWindowInterfaces()) {
    TabStripModel* tabs = browser->GetTabStripModel();
    for (int i = 0; tabs && i < tabs->count(); ++i) {
      if (GetContainerId(tabs->GetWebContentsAt(i)) == id) {
        return;
      }
    }
  }
  RemoveContainer(profile.get(), id);
}

class TemporaryContainerTab
    : public content::WebContentsObserver,
      public content::WebContentsUserData<TemporaryContainerTab> {
 public:
  ~TemporaryContainerTab() override = default;

  void WebContentsDestroyed() override {
    Profile* profile =
        Profile::FromBrowserContext(web_contents()->GetBrowserContext());
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&MaybeRemoveTemporary, profile->GetWeakPtr(), id_));
  }

 private:
  TemporaryContainerTab(content::WebContents* contents, std::string id)
      : content::WebContentsObserver(contents),
        content::WebContentsUserData<TemporaryContainerTab>(*contents),
        id_(std::move(id)) {}
  friend class content::WebContentsUserData<TemporaryContainerTab>;
  WEB_CONTENTS_USER_DATA_KEY_DECL();

  const std::string id_;
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(TemporaryContainerTab);

// Opens sites in the container they always open in (see AssignSite).
class SiteAssignmentThrottle : public content::NavigationThrottle {
 public:
  using content::NavigationThrottle::NavigationThrottle;

  ThrottleCheckResult WillStartRequest() override {
    content::WebContents* contents = navigation_handle()->GetWebContents();
    if (contents && contents->GetUserData(&kChosenContainerKey)) {
      contents->RemoveUserData(&kChosenContainerKey);
      chosen_ = true;
    }
    return Check();
  }
  ThrottleCheckResult WillRedirectRequest() override { return Check(); }
  const char* GetNameForLogging() override { return "ObladaSiteAssignmentThrottle"; }

 private:
  ThrottleCheckResult Check() {
    content::NavigationHandle* navigation = navigation_handle();
    content::WebContents* contents = navigation->GetWebContents();
    if (chosen_ || !navigation->IsInPrimaryMainFrame() ||
        navigation->IsSameDocument() ||
        navigation->IsPost() || !contents ||
        !tabs::TabInterface::MaybeGetFromContents(contents)) {
      return PROCEED;
    }
    // Going back to, or reloading, a page that is already in this tab.
    if (navigation->GetPageTransition() & ui::PAGE_TRANSITION_FORWARD_BACK ||
        navigation->GetReloadType() != content::ReloadType::NONE) {
      return PROCEED;
    }
    Profile* profile = Profile::FromBrowserContext(contents->GetBrowserContext());
    const std::string assigned = AssignedContainer(profile, navigation->GetURL());
    if (assigned.empty() || assigned == GetContainerId(contents)) {
      return PROCEED;
    }
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<content::WebContents> contents, std::string id,
               GURL url, bool keep) {
              if (contents) {
                ReopenInContainer(contents.get(), id, url, keep);
              }
            },
            contents->GetWeakPtr(), assigned, navigation->GetURL(),
            ShowsSomething(contents)));
    return CANCEL_AND_IGNORE;
  }

  // The user opened this tab in its container.
  bool chosen_ = false;
};

}  // namespace

void AttachTabHelpers(content::WebContents* contents) {
  const std::string id = GetContainerId(contents);
  if (IsTemporaryId(id)) {
    TemporaryContainerTab::CreateForWebContents(contents, id);
  }
}

void MaybeAddNavigationThrottle(content::NavigationThrottleRegistry& registry) {
  if (registry.GetNavigationHandle().IsInPrimaryMainFrame()) {
    registry.AddThrottle(std::make_unique<SiteAssignmentThrottle>(registry));
  }
}

void PaintTabIndicator(gfx::Canvas* canvas,
                       const gfx::Rect& bounds,
                       bool vertical,
                       const tabs::TabInterface* tab) {
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  if (!contents) {
    return;
  }
  PaintContainerIndicator(
      canvas, bounds, vertical,
      Profile::FromBrowserContext(contents->GetBrowserContext()),
      GetContainerId(contents));
}

void PaintContainerIndicator(gfx::Canvas* canvas,
                             const gfx::Rect& bounds,
                             bool vertical,
                             Profile* profile,
                             std::string_view id) {
  if (id.empty() || !profile) {
    return;
  }
  std::optional<Container> container = FindContainer(profile, id);

  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(container ? ColorFor(container->color) : gfx::kGoogleGrey500);
  // A short bar along the leading edge of a vertical tab, or under a horizontal one.
  gfx::RectF bar;
  if (vertical) {
    // Thin, against the tab's edge: well clear of its favicon.
    const float height = bounds.height() * 0.5f;
    bar = gfx::RectF(bounds.x() + 1.5f, bounds.y() + (bounds.height() - height) / 2,
                     2, height);
  } else {
    // As thin, along the bottom of the tab's veil, under its title.
    const float width = bounds.width() * 0.4f;
    bar = gfx::RectF(bounds.x() + (bounds.width() - width) / 2, bounds.bottom() - 3.5f,
                     width, 2);
  }
  canvas->DrawRoundRect(bar, std::min(bar.width(), bar.height()) / 2, flags);
}

}  // namespace oblada::containers
