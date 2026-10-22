#ifndef OBLADA_BROWSER_CONTAINERS_CONTAINERS_H_
#define OBLADA_BROWSER_CONTAINERS_CONTAINERS_H_

// Firefox-style containers. Each container is a separate StoragePartition of the
// profile (cookies, logins, local storage, caches), so one site can be signed in to
// different accounts in different tabs. A tab's container is decided when its
// WebContents is created and then kept by Chromium itself: a SiteInstance with a fixed
// StoragePartition carries it across every navigation, and popups share it.

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback_forward.h"
#include "base/memory/scoped_refptr.h"
#include "base/types/expected.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/window_open_disposition.h"

class BrowserWindowInterface;
class GURL;
class Profile;
struct NavigateParams;

namespace content {
class NavigationThrottleRegistry;
class SiteInstance;
class StoragePartitionConfig;
class WebContents;
}  // namespace content

namespace gfx {
class Canvas;
class Rect;
}  // namespace gfx

namespace tabs {
class TabInterface;
}  // namespace tabs

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace oblada::containers {

// Key of a tab's container in session "extra data" (session restore, reopen closed tab).
inline constexpr char kSessionKey[] = "oblada.container";

// Command ids of the "New container tab", "Open link in new container tab" and
// "Reopen in container" submenus (a range Chromium's chrome_command_ids.h leaves
// unused).
inline constexpr int kNewContainerTabMenuId = 55600;
inline constexpr int kOpenLinkInContainerMenuId = 55601;
inline constexpr int kReopenInContainerMenuId = 55602;

struct Container {
  std::string id;     // [a-z0-9-]+, also the StoragePartition's name
  std::string name;
  std::string color;  // a Firefox Multi-Account Containers color name, e.g. "blue"
  std::string icon;
  // Stable and unique among the profile's containers, like Firefox's userContextId
  // (Firefox add-ons see the container as "firefox-container-<number>").
  int number = 0;
  // Temporary containers keep their data in memory only, and go away with their
  // last tab.
  bool temporary = false;
};

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry,
                          const std::string& locale);

// The profile's containers, then its temporary ones.
std::vector<Container> GetContainers(Profile* profile);
std::optional<Container> FindContainer(Profile* profile, std::string_view id);
std::optional<Container> FindContainerByNumber(Profile* profile, int number);
Container AddContainer(Profile* profile,
                       std::string_view name,
                       std::string_view color,
                       std::string_view icon = "circle");
void UpdateContainer(Profile* profile, const Container& container);
// Moves the (non temporary) container to `index` in the list.
void MoveContainer(Profile* profile, std::string_view id, size_t index);
// Closes the container's tabs, forgets it (and the sites always opened in it) and
// erases its data.
void RemoveContainer(Profile* profile, std::string_view id);
void ClearContainerData(Profile* profile,
                        std::string_view id,
                        base::OnceClosure done);

// A new temporary container, "Temporary <n>".
Container AddTemporaryContainer(Profile* profile);
// A temporary container's <n>, which its name may no longer have; 0 for another.
int TemporaryNumber(const Container& container);

// Notifies of any change to the profile's containers (added, renamed, removed...).
base::CallbackListSubscription ObserveContainers(Profile* profile,
                                                 base::RepeatingClosure changed);

// Notifies of each container added, changed or removed, in any profile.
enum class Change { kCreated, kUpdated, kRemoved };
using ChangeCallback =
    base::RepeatingCallback<void(Profile*, Change, const Container&)>;
base::CallbackListSubscription ObserveContainerChanges(ChangeCallback callback);

// --- Sites always opened in a container ---

// The host of `url` as sites are remembered (without "www."); empty if `url` isn't a
// web page.
std::string SiteOf(const GURL& url);
// The container `url`'s site always opens in, if any.
std::string AssignedContainer(Profile* profile, const GURL& url);
// Always opens `site` in container `id`; an empty id forgets it.
void AssignSite(Profile* profile, std::string_view site, std::string_view id);
// The sites always opened in container `id`.
std::vector<std::string> AssignedSites(Profile* profile, std::string_view id);

// The Firefox palette, by color name. Unknown names are grey.
SkColor ColorFor(std::string_view color);
const std::vector<std::string_view>& ColorNames();

// The container `contents` (or `site_instance`) lives in; empty if none.
std::string GetContainerId(content::WebContents* contents);
std::string GetContainerId(content::SiteInstance* site_instance);

content::StoragePartitionConfig PartitionFor(Profile* profile, std::string_view id);

// Opens `url` (the New Tab page if empty) in a new tab of `browser`, in container `id`
// (none if empty). Returns the new tab's contents.
content::WebContents* OpenInContainer(BrowserWindowInterface* browser,
                                      std::string_view id,
                                      const GURL& url,
                                      WindowOpenDisposition disposition);

// tabs_api.cc: opens a tab for an extension's tabs.create() in container `id`.
base::expected<content::WebContents*, std::string> OpenTabInContainer(
    BrowserWindowInterface* browser,
    std::string_view id,
    const GURL& url,
    bool active,
    bool pinned,
    int index);

// Replaces the tab of `contents` with one on `url` in container `id` (none if empty),
// at the same place. A tab that was only showing the New Tab page is closed, else it
// stays (it was showing something else when a site went to its container).
void ReopenInContainer(content::WebContents* contents,
                       std::string_view id,
                       const GURL& url,
                       bool keep_original);

// --- Hooks called from Oblada's patches to Chromium ---

// browser_navigator.cc: the SiteInstance for a new tab opened from a container tab
// (a link opened in a new tab stays in its container); null when there is none.
scoped_refptr<content::SiteInstance> SiteInstanceForNewTab(const NavigateParams& params,
                                                           const GURL& url);

// browser_tabrestore.cc: the SiteInstance that puts a restored tab back into its
// container; null when it had none.
scoped_refptr<content::SiteInstance> SiteInstanceForRestoredTab(
    Profile* profile,
    const GURL& url,
    const std::map<std::string, std::string>& extra_data);

// tab_lifecycle_unit.cc: the SiteInstance for the contents that replace a
// discarded tab's `contents`, so it reloads in its container; null when it has
// none.
scoped_refptr<content::SiteInstance> SiteInstanceForDiscardedTab(
    content::WebContents* contents);

// tab_helpers.cc
void AttachTabHelpers(content::WebContents* contents);

// chrome_content_browser_client_navigation_throttles.cc: opens sites in the
// container they are always opened in.
void MaybeAddNavigationThrottle(content::NavigationThrottleRegistry& registry);

// tab_view.cc: draws the container's color on the tab.
void PaintTabIndicator(gfx::Canvas* canvas,
                       const gfx::Rect& bounds,
                       bool vertical,
                       const tabs::TabInterface* tab);
// The same for a tab of container `id` (none if empty).
void PaintContainerIndicator(gfx::Canvas* canvas,
                             const gfx::Rect& bounds,
                             bool vertical,
                             Profile* profile,
                             std::string_view id);

}  // namespace oblada::containers

#endif  // OBLADA_BROWSER_CONTAINERS_CONTAINERS_H_
