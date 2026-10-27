#include "oblada/browser/barrier/storage.h"

#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browsing_data_filter_builder.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_contents.h"
#include "net/base/schemeful_site.h"
#include "net/cookies/cookie_partition_key.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_access_result.h"
#include "net/cookies/cookie_options.h"
#include "net/cookies/cookie_partition_key_collection.h"
#include "oblada/browser/barrier/barrier.h"
#include "oblada/browser/barrier/tab_helper.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "third_party/blink/public/common/storage_key/storage_key.h"
#include "url/gurl.h"

namespace oblada::barrier {

namespace {

// As Brave: long enough for a site that goes elsewhere and comes back (a
// login, a redirect).
constexpr base::TimeDelta kDelay = base::Seconds(30);
// For tests: the delay, in milliseconds.
constexpr char kDelaySwitch[] = "oblada-barrier-forget-delay";

// The kinds of storage sites keep (not the HTTP cache, nor shader caches).
constexpr uint32_t kStorageMask =
    content::StoragePartition::REMOVE_DATA_MASK_COOKIES |
    content::StoragePartition::REMOVE_DATA_MASK_FILE_SYSTEMS |
    content::StoragePartition::REMOVE_DATA_MASK_INDEXEDDB |
    content::StoragePartition::REMOVE_DATA_MASK_LOCAL_STORAGE |
    content::StoragePartition::REMOVE_DATA_MASK_SERVICE_WORKERS |
    content::StoragePartition::REMOVE_DATA_MASK_CACHE_STORAGE |
    content::StoragePartition::REMOVE_DATA_MASK_BACKGROUND_FETCH;

base::TimeDelta Delay() {
  const std::string delay =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(kDelaySwitch);
  int ms = 0;
  return base::StringToInt(delay, &ms) ? base::Milliseconds(ms) : kDelay;
}

// A site in a profile's container.
using SiteKey = std::tuple<const void*, std::string, std::string>;

SiteKey KeyOf(content::WebContents* contents, const std::string& site) {
  return {contents->GetBrowserContext(),
          contents->GetPrimaryMainFrame()
              ->GetStoragePartition()
              ->GetConfig()
              .partition_name(),
          site};
}

// Sites forgotten once closed, though not set to be (unlinkable bouncing).
std::set<SiteKey>& ToForget() {
  static base::NoDestructor<std::set<SiteKey>> sites;
  return *sites;
}

bool IsOpen(const SiteKey& key) {
  for (BarrierTabHelper* helper : BarrierTabHelper::All()) {
    content::WebContents* contents = helper->web_contents();
    if (KeyOf(contents, SiteOf(contents->GetLastCommittedURL())) == key) {
      return true;
    }
  }
  return false;
}

// What third parties stored under `top`: their partitioned cookies and
// storage.
void ClearThirdParties(content::StoragePartition* partition,
                       const net::SchemefulSite& top) {
  auto cookies = network::mojom::CookieDeletionFilter::New();
  if (std::optional<net::CookiePartitionKey> key =
          net::CookiePartitionKey::FromStorageKeyComponents(
              top, net::CookiePartitionKey::AncestorChainBit::kCrossSite,
              std::nullopt)) {
    cookies->cookie_partition_key_collection =
        net::CookiePartitionKeyCollection(*key);
  } else {
    cookies->cookie_partition_key_collection =
        net::CookiePartitionKeyCollection();
  }
  cookies->partitioned_state_only = true;
  partition->ClearData(
      kStorageMask, /*filter_builder=*/nullptr,
      base::BindRepeating(
          [](const net::SchemefulSite& top, const blink::StorageKey& key,
             storage::SpecialStoragePolicy*) {
            return key.top_level_site() == top && key.IsThirdPartyContext();
          },
          top),
      std::move(cookies), /*perform_storage_cleanup=*/true, base::Time(),
      base::Time::Max(), base::DoNothing());
}

// Everything `site` stored.
void ClearSite(content::StoragePartition* partition, const std::string& site) {
  auto builder = content::BrowsingDataFilterBuilder::Create(
      content::BrowsingDataFilterBuilder::Mode::kDelete);
  builder->AddRegisterableDomain(site);
  auto cookies = network::mojom::CookieDeletionFilter::New();
  cookies->including_domains = std::vector<std::string>{site};
  partition->ClearData(kStorageMask, builder.get(),
                       content::StoragePartition::StorageKeyPolicyMatcherFunction(),
                       std::move(cookies), /*perform_storage_cleanup=*/true,
                       base::Time(), base::Time::Max(), base::DoNothing());
}

void Check(base::WeakPtr<Profile> profile,
           content::StoragePartitionConfig config,
           GURL page_url) {
  if (!profile) {
    return;
  }
  const std::string site = SiteOf(page_url);
  const SiteKey key{profile.get(), config.partition_name(), site};
  if (site.empty() || IsOpen(key)) {
    return;
  }
  content::StoragePartition* partition =
      profile->GetStoragePartition(config, /*can_create=*/false);
  if (!partition) {
    return;
  }
  const Settings settings = GetSettings(profile.get(), page_url);
  if (!settings.enabled) {
    ToForget().erase(key);
    return;
  }
  if (settings.cookies == Cookies::kBlockThirdParty) {
    ClearThirdParties(partition, net::SchemefulSite(page_url));
  }
  if (settings.forget || ToForget().erase(key)) {
    ClearSite(partition, site);
  }
}

}  // namespace

void OnPageLeft(content::WebContents* contents, const GURL& page_url) {
  if (SiteOf(page_url).empty() || !IsOn()) {
    return;
  }
  Profile* profile =
      Profile::FromBrowserContext(contents->GetBrowserContext());
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&Check, profile->GetWeakPtr(),
                     contents->GetPrimaryMainFrame()
                         ->GetStoragePartition()
                         ->GetConfig(),
                     page_url),
      Delay());
}

void ForgetUnlessKnown(content::WebContents* contents, const GURL& page_url) {
  const std::string site = SiteOf(page_url);
  if (site.empty()) {
    return;
  }
  content::StoragePartition* partition =
      contents->GetPrimaryMainFrame()->GetStoragePartition();
  partition->GetCookieManagerForBrowserProcess()->GetCookieList(
      page_url, net::CookieOptions::MakeAllInclusive(),
      net::CookiePartitionKeyCollection(),
      base::BindOnce(
          [](SiteKey key, const net::CookieAccessResultList& included,
             const net::CookieAccessResultList& excluded) {
            if (included.empty()) {
              ToForget().insert(std::move(key));
            }
          },
          KeyOf(contents, site)));
}

}  // namespace oblada::barrier
