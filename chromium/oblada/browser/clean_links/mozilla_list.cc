#include "oblada/browser/clean_links/mozilla_list.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/memory/scoped_refptr.h"
#include "base/no_destructor.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "net/base/load_flags.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "oblada/clean_links_lists.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "url/gurl.h"

namespace oblada::clean_links {

namespace {

// The list last downloaded, and when.
constexpr char kListPref[] = "oblada.clean_links.mozilla_list";
constexpr char kDownloadedPref[] = "oblada.clean_links.mozilla_list_downloaded";

constexpr char kListUrl[] =
    "https://firefox.settings.services.mozilla.com/v1/buckets/main/"
    "collections/query-stripping/records";
// For tests: where to download the list from instead, as soon as the browser
// starts.
constexpr char kListUrlSwitch[] = "oblada-mozilla-list-url";

constexpr base::TimeDelta kFirstCheck = base::Minutes(1);
constexpr base::TimeDelta kInterval = base::Days(1);
constexpr base::TimeDelta kRetry = base::Hours(1);
constexpr size_t kMaxAnswerSize = 256 * 1024;
constexpr size_t kMaxParams = 1000;

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("oblada_clean_links_mozilla_list", R"(
      semantics {
        sender: "Oblada clean links"
        description:
          "Downloads Mozilla's list of the parameters sites add to links to "
          "track who follows them (the query-stripping collection of "
          "Firefox's Remote Settings), which Oblada removes from the links it "
          "copies clean, as Firefox does."
        trigger:
          "A minute after the browser starts, if the list wasn't downloaded "
          "in the last day, then once a day."
        data: "None."
        destination: OTHER
      }
      policy {
        cookies_allowed: NO
        setting: "None."
        policy_exception_justification: "Not implemented."
      })");

// Not something else: no spaces nor separators of a query.
bool IsParamName(std::string_view name) {
  return !name.empty() && name.size() <= 100 &&
         name.find_first_of(" \t\r\n&=#?") == std::string_view::npos;
}

Names& Params() {
  static base::NoDestructor<Names> params(
      ParseMozillaList(kMozillaList).value_or(Names()));
  return *params;
}

// Downloads the list as it's due, and keeps it.
class Updater {
 public:
  Updater() {
    const base::CommandLine* command_line =
        base::CommandLine::ForCurrentProcess();
    if (command_line->HasSwitch(kListUrlSwitch)) {
      url_ = GURL(command_line->GetSwitchValueASCII(kListUrlSwitch));
      Schedule(base::TimeDelta());
      return;
    }
    url_ = GURL(kListUrl);
    const base::Time downloaded =
        g_browser_process->local_state()->GetTime(kDownloadedPref);
    Schedule(std::clamp(downloaded + kInterval - base::Time::Now(),
                        kFirstCheck, kInterval));
  }

 private:
  void Schedule(base::TimeDelta delay) {
    timer_.Start(FROM_HERE, delay,
                 base::BindOnce(&Updater::Download, base::Unretained(this)));
  }

  // Over https only, but on this machine (tests).
  bool IsAllowed() const {
    return url_.SchemeIs(url::kHttpsScheme) ||
           (url_.SchemeIs(url::kHttpScheme) &&
            (url_.host() == "127.0.0.1" || url_.host() == "localhost"));
  }

  void Download() {
    if (!IsAllowed()) {
      return;
    }
    auto request = std::make_unique<network::ResourceRequest>();
    request->url = url_;
    request->credentials_mode = network::mojom::CredentialsMode::kOmit;
    request->load_flags = net::LOAD_DISABLE_CACHE;
    loader_ = network::SimpleURLLoader::Create(std::move(request),
                                               kTrafficAnnotation);
    factory_ = g_browser_process->shared_url_loader_factory();
    loader_->DownloadToString(
        factory_.get(),
        base::BindOnce(&Updater::OnDownloaded, base::Unretained(this)),
        kMaxAnswerSize);
  }

  void OnDownloaded(std::optional<std::string> answer) {
    loader_.reset();
    std::optional<Names> params =
        answer ? ParseMozillaList(*answer) : std::nullopt;
    if (!params) {
      Schedule(kRetry);
      return;
    }
    base::ListValue list;
    for (const std::string& param : *params) {
      list.Append(param);
    }
    PrefService* local_state = g_browser_process->local_state();
    local_state->SetList(kListPref, std::move(list));
    local_state->SetTime(kDownloadedPref, base::Time::Now());
    Params() = std::move(*params);
    Schedule(kInterval);
  }

  GURL url_;
  base::OneShotTimer timer_;
  scoped_refptr<network::SharedURLLoaderFactory> factory_;
  std::unique_ptr<network::SimpleURLLoader> loader_;
};

}  // namespace

void RegisterMozillaListPrefs(PrefRegistrySimple* registry) {
  registry->RegisterListPref(kListPref);
  registry->RegisterTimePref(kDownloadedPref, base::Time());
}

void StartMozillaListUpdates() {
  PrefService* local_state = g_browser_process->local_state();
  if (!local_state) {
    return;
  }
  // The list last downloaded, rather than the one built in.
  if (local_state->HasPrefPath(kDownloadedPref)) {
    Names params;
    for (const base::Value& param : local_state->GetList(kListPref)) {
      if (param.is_string() && IsParamName(param.GetString())) {
        params.insert(param.GetString());
      }
    }
    Params() = std::move(params);
  }
  static base::NoDestructor<Updater> updater;
}

const Names& MozillaListParams() {
  return Params();
}

std::optional<Names> ParseMozillaList(std::string_view json) {
  std::optional<base::DictValue> answer =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  const base::ListValue* records = answer ? answer->FindList("data") : nullptr;
  if (!records) {
    return std::nullopt;
  }
  Names params;
  for (const base::Value& record : *records) {
    const base::DictValue* fields = record.GetIfDict();
    const base::Value* strip = fields ? fields->Find("stripList") : nullptr;
    if (!strip) {
      continue;
    }
    // A list, or as it once was, names separated by spaces.
    std::vector<std::string> names;
    if (strip->is_list()) {
      for (const base::Value& name : strip->GetList()) {
        if (name.is_string()) {
          names.push_back(name.GetString());
        }
      }
    } else if (strip->is_string()) {
      names = base::SplitString(strip->GetString(), " ", base::TRIM_WHITESPACE,
                                base::SPLIT_WANT_NONEMPTY);
    }
    for (const std::string& name : names) {
      if (IsParamName(name) && params.size() < kMaxParams) {
        params.insert(base::ToLowerASCII(name));
      }
    }
  }
  return params;
}

}  // namespace oblada::clean_links
