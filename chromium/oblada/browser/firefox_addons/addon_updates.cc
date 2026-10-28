#include "oblada/browser/firefox_addons/addon_updates.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/escape.h"
#include "base/strings/string_number_conversions.h"
#include "base/supports_user_data.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "base/version.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/storage_partition.h"
#include "net/base/load_flags.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "oblada/browser/firefox_addons/firefox_addons.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "url/gurl.h"

namespace oblada::firefox_addons {

namespace {

constexpr char kIntervalSwitch[] = "oblada-addon-update-interval";
constexpr base::TimeDelta kFirstCheck = base::Minutes(3);
constexpr base::TimeDelta kInterval = base::Hours(12);

constexpr char kAmoApi[] = "https://addons.mozilla.org/api/v5/addons/addon/";
constexpr size_t kMaxInfoSize = 1024 * 1024;

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("oblada_firefox_addon_updates", R"(
      semantics {
        sender: "Oblada Firefox add-on updates"
        description:
          "Looks for new versions of the Firefox add-ons the user installed, on "
          "addons.mozilla.org or where the add-on says, and downloads them."
        trigger: "A few minutes after the browser starts, then twice a day."
        data: "The add-on's id."
        destination: OTHER
      }
      policy {
        cookies_allowed: NO
        setting: "Removing the add-on stops its updates."
        policy_exception_justification: "Not implemented."
      })");

std::unique_ptr<network::SimpleURLLoader> CreateLoader(const GURL& url) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->load_flags = net::LOAD_DISABLE_CACHE;
  return network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
}

struct Release {
  base::Version version;
  GURL xpi;
};

// The newest version an update manifest (Firefox's format, see
// https://extensionworkshop.com/documentation/manage/updating-your-extension/) or
// addons.mozilla.org's API offers.
std::optional<Release> ParseRelease(const std::string& json,
                                    const std::string& gecko_id,
                                    bool from_amo) {
  std::optional<base::DictValue> info =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!info) {
    return std::nullopt;
  }
  if (from_amo) {
    const std::string* version = info->FindStringByDottedPath("current_version.version");
    const std::string* url = info->FindStringByDottedPath("current_version.file.url");
    if (!version || !url) {
      return std::nullopt;
    }
    return Release{base::Version(*version), GURL(*url)};
  }
  const base::DictValue* addons = info->FindDict("addons");
  const base::DictValue* addon = addons ? addons->FindDict(gecko_id) : nullptr;
  const base::ListValue* updates = addon ? addon->FindList("updates") : nullptr;
  std::optional<Release> newest;
  if (!updates) {
    return newest;
  }
  for (const base::Value& update : *updates) {
    const base::DictValue* dict = update.GetIfDict();
    const std::string* version = dict ? dict->FindString("version") : nullptr;
    const std::string* link = dict ? dict->FindString("update_link") : nullptr;
    if (!version || !link) {
      continue;
    }
    Release release{base::Version(*version), GURL(*link)};
    if (release.version.IsValid() &&
        (!newest || release.version > newest->version)) {
      newest = std::move(release);
    }
  }
  return newest;
}

class AddonUpdater : public base::SupportsUserData::Data {
 public:
  explicit AddonUpdater(Profile* profile) : profile_(profile) {
    base::TimeDelta first = kFirstCheck;
    base::TimeDelta interval = kInterval;
    int seconds = 0;
    if (base::StringToInt(base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
                              kIntervalSwitch),
                          &seconds) &&
        seconds > 0) {
      first = interval = base::Seconds(seconds);
    }
    first_check_.Start(FROM_HERE, first,
                       base::BindOnce(&AddonUpdater::Check, base::Unretained(this)));
    timer_.Start(FROM_HERE, interval,
                 base::BindRepeating(&AddonUpdater::Check, base::Unretained(this)));
  }

  static const void* UserDataKey() { return &kKey; }

 private:
  static constexpr int kKey = 0;

  void Check() {
    for (InstalledAddon& addon : GetInstalledAddons(profile_)) {
      // From addons.mozilla.org, where add-ons come from (an update_url
      // elsewhere, which addons.mozilla.org's add-ons don't have, is ignored),
      // or from the tests' server.
      const bool from_amo =
          addon.update_url.is_empty() || !IsAddonSource(addon.update_url);
      const GURL url =
          from_amo ? GURL(kAmoApi + base::EscapeAllExceptUnreserved(addon.gecko_id) + "/")
                   : addon.update_url;
      std::unique_ptr<network::SimpleURLLoader> loader = CreateLoader(url);
      network::SimpleURLLoader* raw = loader.get();
      raw->DownloadToString(
          Factory(),
          base::BindOnce(&AddonUpdater::OnInfo, weak_factory_.GetWeakPtr(),
                         std::move(loader), std::move(addon), from_amo),
          kMaxInfoSize);
    }
  }

  void OnInfo(std::unique_ptr<network::SimpleURLLoader> loader,
              InstalledAddon addon,
              bool from_amo,
              std::optional<std::string> json) {
    std::optional<Release> release =
        json ? ParseRelease(*json, addon.gecko_id, from_amo) : std::nullopt;
    if (!release || !release->version.IsValid() || !addon.version.IsValid() ||
        release->version <= addon.version || !IsAddonSource(release->xpi)) {
      return;
    }
    LOG(WARNING) << "Updating Firefox add-on " << addon.gecko_id << " to "
                 << release->version.GetString();
    std::unique_ptr<network::SimpleURLLoader> download = CreateLoader(release->xpi);
    network::SimpleURLLoader* raw = download.get();
    raw->DownloadToTempFile(
        Factory(), base::BindOnce(&AddonUpdater::OnDownloaded,
                                  weak_factory_.GetWeakPtr(), std::move(download),
                                  addon.gecko_id));
  }

  void OnDownloaded(std::unique_ptr<network::SimpleURLLoader> loader,
                    std::string gecko_id,
                    base::FilePath xpi) {
    if (xpi.empty()) {
      return;
    }
    // Redirected elsewhere: not addons.mozilla.org's.
    if (!IsAddonSource(loader->GetFinalURL())) {
      base::ThreadPool::PostTask(FROM_HERE, {base::MayBlock()},
                                 base::GetDeleteFileCallback(xpi));
      return;
    }
    InstallUpdate(profile_, xpi, gecko_id);
  }

  network::mojom::URLLoaderFactory* Factory() {
    if (!factory_) {
      factory_ = profile_->GetDefaultStoragePartition()
                     ->GetURLLoaderFactoryForBrowserProcess();
    }
    return factory_.get();
  }

  const raw_ptr<Profile> profile_;
  scoped_refptr<network::SharedURLLoaderFactory> factory_;
  base::OneShotTimer first_check_;
  base::RepeatingTimer timer_;
  base::WeakPtrFactory<AddonUpdater> weak_factory_{this};
};

}  // namespace

void StartAddonUpdates(Profile* profile) {
  profile = profile->GetOriginalProfile();
  if (!profile->IsRegularProfile() ||
      profile->GetUserData(AddonUpdater::UserDataKey())) {
    return;
  }
  profile->SetUserData(AddonUpdater::UserDataKey(),
                       std::make_unique<AddonUpdater>(profile));
}

}  // namespace oblada::firefox_addons
