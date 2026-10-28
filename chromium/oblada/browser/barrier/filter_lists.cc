#include "oblada/browser/barrier/filter_lists.h"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>

#include "base/check.h"
#include "base/command_line.h"
#include "base/containers/circular_deque.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/hash/sha1.h"
#include "base/json/json_reader.h"
#include "base/json/values_util.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/common/chrome_paths.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "net/base/load_flags.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/url_request/redirect_info.h"
#include "oblada/barrier_lists.h"
#include "oblada/barrier_resources.h"
#include "oblada/browser/barrier/barrier.h"
#include "oblada/browser/barrier/engine_host.h"
#include "oblada/components/barrier/adblock/adblock_engine.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"

namespace oblada::barrier::filter_lists {

namespace {

constexpr char kCatalogUrl[] =
    "https://raw.githubusercontent.com/brave/adblock-resources/master/"
    "filter_lists/list_catalog.json";
constexpr char kCatalogSwitch[] = "oblada-barrier-catalog";

// Local State.
// The lists turned on or off, by uuid, against their defaults.
constexpr char kChoicesPref[] = "oblada.barrier.lists";
// Whether the browser's language's lists were turned on.
constexpr char kRegionalChosenPref[] = "oblada.barrier.regional_lists_chosen";
// Subscriptions: {url, enabled, title, updated, failed}.
constexpr char kSubscriptionsPref[] = "oblada.barrier.subscriptions";
constexpr char kCustomFiltersPref[] = "oblada.barrier.custom_filters";
// Each source downloaded, by URL: {updated, expires (hours), failed}.
constexpr char kSourcesPref[] = "oblada.barrier.sources";
constexpr char kCatalogUpdatedPref[] = "oblada.barrier.catalog_updated";

// The catalog's lists of social media embeds, turned on by Settings > Oblada
// > Privacy rather than as the catalog says.
constexpr std::pair<std::string_view, const char*> kEmbedLists[] = {
    {"A5E6EC21-F01F-4547-9F0A-1EE1C3F2AE8D", kFacebookEmbedsPref},
    {"84960ADD-1CC1-419F-81FC-9F116F5205CC", kXEmbedsPref},
    {"FB626316-6CC8-4447-884B-F5A37C29B0AE", kLinkedInEmbedsPref},
};

// The version of Brave's engine (third_party/rust/chromium_crates_io's
// Cargo.toml): engines kept on disk from another one are compiled again.
constexpr char kEngineVersion[] = "adblock 0.13.3";

constexpr base::TimeDelta kFirstCheck = base::Seconds(5);
constexpr base::TimeDelta kCheckInterval = base::Hours(1);
constexpr base::TimeDelta kDefaultExpiry = base::Days(1);
constexpr base::TimeDelta kCatalogExpiry = base::Days(1);
constexpr size_t kMaxListSize = 32 * 1024 * 1024;

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("oblada_barrier_filter_lists", R"(
      semantics {
        sender: "Oblada Barrier"
        description:
          "Downloads the filter lists Oblada blocks trackers and ads with "
          "(Brave's catalog of them, and each list from its own address: "
          "EasyList, uBlock Origin's lists, Brave's, the lists the user "
          "subscribed to)."
        trigger:
          "Shortly after the browser starts, then as each list says it should "
          "be downloaded again (daily if it doesn't); when a list is turned "
          "on; when the user asks to update them."
        data: "None."
        destination: OTHER
      }
      policy {
        cookies_allowed: NO
        setting: "Settings > Oblada > Barrier > Filter lists."
        policy_exception_justification: "Not implemented."
      })");

struct CatalogEntry {
  std::string uuid;
  std::string title;
  std::string description;
  std::vector<std::string> langs;
  bool hidden = false;
  bool default_enabled = false;
  bool first_party_protections = false;
  uint8_t permissions = 0;
  std::vector<GURL> sources;
};

std::vector<CatalogEntry> ParseCatalog(std::string_view json) {
  std::vector<CatalogEntry> catalog;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return catalog;
  }
  for (const base::Value& value : parsed->GetList()) {
    const base::DictValue* dict = value.GetIfDict();
    const std::string* uuid = dict ? dict->FindString("uuid") : nullptr;
    const base::ListValue* sources = dict ? dict->FindList("sources") : nullptr;
    if (!uuid || !sources) {
      continue;
    }
    // Lists for other platforms (iOS's).
    if (const base::ListValue* platforms = dict->FindList("platforms")) {
      if (!platforms->empty()) {
        continue;
      }
    }
    CatalogEntry entry;
    entry.uuid = *uuid;
    if (const std::string* title = dict->FindString("title")) {
      entry.title = *title;
    }
    if (const std::string* desc = dict->FindString("desc")) {
      entry.description = *desc;
    }
    if (const base::ListValue* langs = dict->FindList("langs")) {
      for (const base::Value& lang : *langs) {
        if (lang.is_string()) {
          entry.langs.push_back(lang.GetString());
        }
      }
    }
    entry.hidden = dict->FindBool("hidden").value_or(false);
    entry.default_enabled = dict->FindBool("default_enabled").value_or(false);
    entry.first_party_protections =
        dict->FindBool("first_party_protections").value_or(false);
    entry.permissions =
        static_cast<uint8_t>(dict->FindInt("permission_mask").value_or(0));
    for (const base::Value& source : *sources) {
      const std::string* url =
          source.is_dict() ? source.GetDict().FindString("url") : nullptr;
      if (url && IsListUrlAllowed(GURL(*url))) {
        entry.sources.emplace_back(*url);
      }
    }
    catalog.push_back(std::move(entry));
  }
  return catalog;
}

PrefService* LocalState() {
  return g_browser_process->local_state();
}

base::FilePath BarrierDir() {
  base::FilePath user_data;
  base::PathService::Get(chrome::DIR_USER_DATA, &user_data);
  return user_data.Append(FILE_PATH_LITERAL("Oblada Barrier"));
}

// A source's file: a hash of its URL.
base::FilePath SourceFile(const base::FilePath& dir, const GURL& url) {
  return dir.Append(FILE_PATH_LITERAL("lists"))
      .AppendASCII(base::HexEncode(base::SHA1Hash(
                       base::as_byte_span(url.spec())))
                       .substr(0, 16) +
                   ".txt");
}

// What engines are built from.
struct EngineInputs {
  struct Source {
    base::FilePath file;
    uint8_t permissions = 0;
  };
  std::vector<Source> default_sources;
  std::vector<Source> additional_sources;
  std::string custom_filters;
};

struct BuiltEngines {
  std::unique_ptr<adblock::Engine> default_engine;
  std::unique_ptr<adblock::Engine> additional_engine;
};

// The engines for `inputs`: from the copies on disk when they were built from
// the same lists, else compiled (and kept on disk).
BuiltEngines BuildEngines(const base::FilePath& dir, EngineInputs inputs) {
  std::vector<adblock::Engine::List> default_lists;
  std::vector<adblock::Engine::List> additional_lists;
  std::string key;
  auto read = [&](const std::vector<EngineInputs::Source>& sources,
                  std::vector<adblock::Engine::List>& lists) {
    for (const auto& source : sources) {
      std::string text;
      if (base::ReadFileToString(source.file, &text)) {
        key += base::HexEncode(base::SHA1Hash(base::as_byte_span(text)));
        key += base::NumberToString(source.permissions);
        lists.push_back({std::move(text), source.permissions});
      }
    }
    key += "|";
  };
  read(inputs.default_sources, default_lists);
  read(inputs.additional_sources, additional_lists);
  if (!inputs.custom_filters.empty()) {
    key += base::HexEncode(
        base::SHA1Hash(base::as_byte_span(inputs.custom_filters)));
    additional_lists.push_back({std::move(inputs.custom_filters), 0});
  }
  // A different Oblada may have another version of the engine.
  key += kEngineVersion;

  const base::FilePath key_file = dir.Append(FILE_PATH_LITERAL("engines.key"));
  const base::FilePath default_file =
      dir.Append(FILE_PATH_LITERAL("engine-default.dat"));
  const base::FilePath additional_file =
      dir.Append(FILE_PATH_LITERAL("engine-additional.dat"));

  BuiltEngines engines;
  std::string old_key;
  if (base::ReadFileToString(key_file, &old_key) && old_key == key) {
    auto load = [](const base::FilePath& file) -> std::unique_ptr<adblock::Engine> {
      std::optional<std::vector<uint8_t>> data = base::ReadFileToBytes(file);
      return data ? adblock::Engine::Deserialize(*data) : nullptr;
    };
    engines.default_engine = load(default_file);
    engines.additional_engine = load(additional_file);
  }
  if (!engines.default_engine || !engines.additional_engine) {
    engines.default_engine = adblock::Engine::FromLists(default_lists);
    engines.additional_engine = adblock::Engine::FromLists(additional_lists);
    base::CreateDirectory(dir);
    base::ImportantFileWriter::WriteFileAtomically(
        default_file, base::as_string_view(engines.default_engine->Serialize()));
    base::ImportantFileWriter::WriteFileAtomically(
        additional_file,
        base::as_string_view(engines.additional_engine->Serialize()));
    base::ImportantFileWriter::WriteFileAtomically(key_file, key);
  }
  engines.default_engine->UseResources(kEngineResources);
  engines.additional_engine->UseResources(kEngineResources);
  return engines;
}

class Lists {
 public:
  static Lists& Get() {
    static base::NoDestructor<Lists> lists;
    return *lists;
  }

  void Start() {
    dir_ = BarrierDir();
    // Lists are written, and engines built from them, in order.
    file_task_runner_ = base::ThreadPool::CreateSequencedTaskRunner(
        {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
         base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN});
    const base::CommandLine& command_line =
        *base::CommandLine::ForCurrentProcess();
    test_catalog_ = GURL(command_line.GetSwitchValueASCII(kCatalogSwitch));
    if (!test_catalog_.is_valid()) {
      std::string json;
      if (!base::ReadFileToString(CatalogFile(), &json) ||
          (catalog_ = ParseCatalog(json)).empty()) {
        catalog_ = ParseCatalog(kListCatalog);
      }
      ChooseRegionalLists();
    }

    registrar_.Init(LocalState());
    for (const char* pref :
         {kChoicesPref, kSubscriptionsPref, kCustomFiltersPref, kEnabledPref,
          kFacebookEmbedsPref, kXEmbedsPref, kLinkedInEmbedsPref}) {
      registrar_.Add(pref, base::BindRepeating(&Lists::OnChoicesChanged,
                                               base::Unretained(this)));
    }

    Rebuild();
    timer_.Start(FROM_HERE, kCheckInterval,
                 base::BindRepeating(&Lists::CheckForUpdates,
                                     base::Unretained(this), false));
    check_timer_.Start(
        FROM_HERE, test_catalog_.is_valid() ? base::TimeDelta() : kFirstCheck,
        base::BindOnce(&Lists::CheckForUpdates, base::Unretained(this), false));
  }

  std::vector<ListInfo> GetLists() const {
    std::vector<ListInfo> lists;
    for (const CatalogEntry& entry : catalog_) {
      if (entry.hidden) {
        continue;
      }
      ListInfo info;
      info.uuid = entry.uuid;
      info.title = entry.title;
      info.description = entry.description;
      info.default_enabled = entry.default_enabled;
      info.enabled = IsEnabled(entry);
      info.langs = entry.langs;
      info.updated = UpdatedTime(entry.sources);
      lists.push_back(std::move(info));
    }
    return lists;
  }

  void UpdateNow() { CheckForUpdates(/*all=*/true); }

  base::RepeatingClosureList& observers() { return observers_; }

  // What a catalog list's source may inject: trusted scriptlets only for
  // uBlock Origin's and Brave's own lists, the ones they trust with them, and
  // only if the catalog built into Oblada says so. Anyone running another
  // list's host, or Brave's catalog downloaded, could otherwise have every
  // page run what they like. The test catalog's lists get what it says.
  uint8_t Permissions(const CatalogEntry& entry, const GURL& source) const {
    if (test_catalog_.is_valid()) {
      return entry.permissions;
    }
    static constexpr std::string_view kTrustedSources[] = {
        "https://raw.githubusercontent.com/uBlockOrigin/uAssets/",
        "https://raw.githubusercontent.com/brave/adblock-lists/",
    };
    if (std::ranges::none_of(kTrustedSources, [&](std::string_view prefix) {
          return source.spec().starts_with(prefix);
        })) {
      return 0;
    }
    static const base::NoDestructor<std::map<std::string, uint8_t>> built_in(
        [] {
          std::map<std::string, uint8_t> permissions;
          for (const CatalogEntry& built_in_entry : ParseCatalog(kListCatalog)) {
            permissions[built_in_entry.uuid] = built_in_entry.permissions;
          }
          return permissions;
        }());
    const auto it = built_in->find(entry.uuid);
    return it == built_in->end()
               ? 0
               : it->second & adblock::Engine::kTrusted;
  }

  void Rebuild() {
    if (building_) {
      rebuild_again_ = true;
      return;
    }
    building_ = true;
    EngineInputs inputs;
    if (IsOn()) {
      for (const CatalogEntry& entry : catalog_) {
        if (!IsEnabled(entry)) {
          continue;
        }
        for (const GURL& source : entry.sources) {
          (entry.first_party_protections ? inputs.default_sources
                                         : inputs.additional_sources)
              .push_back({SourceFile(dir_, source), Permissions(entry, source)});
        }
      }
      for (const Subscription& subscription : filter_lists::GetSubscriptions()) {
        if (subscription.enabled) {
          inputs.additional_sources.push_back(
              {SourceFile(dir_, GURL(subscription.url)), 0});
        }
      }
      inputs.custom_filters = LocalState()->GetString(kCustomFiltersPref);
    }
    file_task_runner_->PostTaskAndReplyWithResult(
        FROM_HERE, base::BindOnce(&BuildEngines, dir_, std::move(inputs)),
        base::BindOnce(&Lists::OnBuilt, base::Unretained(this)));
  }

  // Downloads the catalog and the lists that are due (or all of them).
  void CheckForUpdates(bool all) {
    if (!IsOn()) {
      return;
    }
    if (test_catalog_.is_valid()) {
      if (catalog_.empty() || all) {
        Enqueue(test_catalog_);
      }
    } else if (all || base::Time::Now() - LocalState()->GetTime(
                                              kCatalogUpdatedPref) >
                          kCatalogExpiry) {
      Enqueue(GURL(kCatalogUrl));
    }
    for (const GURL& url : NeededSources()) {
      if (all || IsDue(url)) {
        Enqueue(url);
      }
    }
    DownloadNext();
  }

 private:
  friend class base::NoDestructor<Lists>;
  Lists() = default;

  base::FilePath CatalogFile() const {
    return dir_.Append(FILE_PATH_LITERAL("list_catalog.json"));
  }

  bool IsEnabled(const CatalogEntry& entry) const {
    for (const auto& [uuid, pref] : kEmbedLists) {
      if (base::EqualsCaseInsensitiveASCII(entry.uuid, uuid)) {
        return LocalState()->GetBoolean(pref);
      }
    }
    if (entry.hidden) {
      return entry.default_enabled;
    }
    return LocalState()
        ->GetDict(kChoicesPref)
        .FindBool(entry.uuid)
        .value_or(entry.default_enabled);
  }

  // Turns the lists for the browser's language on, once, as Brave does.
  void ChooseRegionalLists() {
    if (LocalState()->GetBoolean(kRegionalChosenPref)) {
      return;
    }
    LocalState()->SetBoolean(kRegionalChosenPref, true);
    const std::string locale = g_browser_process->GetApplicationLocale();
    const std::string language = locale.substr(0, locale.find('-'));
    ScopedDictPrefUpdate update(LocalState(), kChoicesPref);
    for (const CatalogEntry& entry : catalog_) {
      for (const std::string& lang : entry.langs) {
        if (base::EqualsCaseInsensitiveASCII(lang, language)) {
          update->Set(entry.uuid, true);
        }
      }
    }
  }

  std::vector<GURL> NeededSources() const {
    std::vector<GURL> sources;
    for (const CatalogEntry& entry : catalog_) {
      if (IsEnabled(entry)) {
        sources.insert(sources.end(), entry.sources.begin(),
                       entry.sources.end());
      }
    }
    for (const Subscription& subscription : filter_lists::GetSubscriptions()) {
      if (subscription.enabled) {
        sources.emplace_back(subscription.url);
      }
    }
    return sources;
  }

  const base::DictValue* SourceState(const GURL& url) const {
    return LocalState()->GetDict(kSourcesPref).FindDict(url.spec());
  }

  bool IsDue(const GURL& url) const {
    const base::DictValue* state = SourceState(url);
    if (!state || !base::PathExists(SourceFile(dir_, url))) {
      return true;
    }
    const std::optional<base::Time> updated =
        base::ValueToTime(state->Find("updated"));
    const int expires = state->FindInt("expires").value_or(0);
    const base::TimeDelta expiry =
        expires > 0 ? base::Hours(expires) : kDefaultExpiry;
    return !updated || base::Time::Now() - *updated >= expiry;
  }

  base::Time UpdatedTime(const std::vector<GURL>& sources) const {
    base::Time oldest;
    for (const GURL& url : sources) {
      const base::DictValue* state = SourceState(url);
      std::optional<base::Time> updated =
          state ? base::ValueToTime(state->Find("updated")) : std::nullopt;
      if (!updated) {
        return base::Time();
      }
      if (oldest.is_null() || *updated < oldest) {
        oldest = *updated;
      }
    }
    return oldest;
  }

  void Enqueue(const GURL& url) {
    if (url != current_ && std::ranges::find(queue_, url) == queue_.end()) {
      queue_.push_back(url);
    }
  }

  void DownloadNext() {
    if (loader_ || queue_.empty()) {
      return;
    }
    current_ = queue_.front();
    queue_.pop_front();
    auto request = std::make_unique<network::ResourceRequest>();
    request->url = current_;
    request->credentials_mode = network::mojom::CredentialsMode::kOmit;
    request->load_flags = net::LOAD_BYPASS_CACHE | net::LOAD_DISABLE_CACHE;
    loader_ = network::SimpleURLLoader::Create(std::move(request),
                                               kTrafficAnnotation);
    loader_->SetRetryOptions(
        1, network::SimpleURLLoader::RETRY_ON_NETWORK_CHANGE);
    // A list redirected to plain HTTP fails, as one listed there would.
    loader_->SetOnRedirectCallback(base::BindRepeating(
        [](Lists* lists, const GURL&, const net::RedirectInfo& redirect,
           const network::mojom::URLResponseHead&, std::vector<std::string>*) {
          if (!IsListUrlAllowed(redirect.new_url)) {
            lists->OnDownloaded(std::nullopt);
          }
        },
        base::Unretained(this)));
    loader_->DownloadToString(
        g_browser_process->shared_url_loader_factory().get(),
        base::BindOnce(&Lists::OnDownloaded, base::Unretained(this)),
        kMaxListSize);
  }

  void OnDownloaded(std::optional<std::string> body) {
    const GURL url = current_;
    const bool ok = body && loader_->NetError() == net::OK;
    loader_.reset();
    current_ = GURL();

    if (url == test_catalog_ || url == GURL(kCatalogUrl)) {
      std::vector<CatalogEntry> catalog =
          ok ? ParseCatalog(*body) : std::vector<CatalogEntry>();
      if (!catalog.empty()) {
        catalog_ = std::move(catalog);
        if (url == GURL(kCatalogUrl)) {
          LocalState()->SetTime(kCatalogUpdatedPref, base::Time::Now());
          file_task_runner_->PostTask(
              FROM_HERE,
              base::BindOnce(
                  [](base::FilePath file, std::string json) {
                    base::CreateDirectory(file.DirName());
                    base::ImportantFileWriter::WriteFileAtomically(file, json);
                  },
                  CatalogFile(), std::move(*body)));
        }
        ChooseRegionalLists();
        // Lists newly in the catalog.
        for (const GURL& source : NeededSources()) {
          if (IsDue(source)) {
            Enqueue(source);
          }
        }
        changed_ = true;
      }
    } else {
      ScopedDictPrefUpdate update(LocalState(), kSourcesPref);
      base::DictValue* state = update->EnsureDict(url.spec());
      if (ok) {
        state->Set("updated", base::TimeToValue(base::Time::Now()));
        state->Set("expires",
                   static_cast<int>(adblock::ReadListMetadata(*body).expires_hours));
        state->Remove("failed");
        UpdateSubscription(url, *body);
        file_task_runner_->PostTask(
            FROM_HERE,
            base::BindOnce(
                [](base::FilePath file, std::string text) {
                  base::CreateDirectory(file.DirName());
                  base::ImportantFileWriter::WriteFileAtomically(file, text);
                },
                SourceFile(dir_, url), std::move(*body)));
        changed_ = true;
      } else {
        state->Set("failed", true);
        UpdateSubscription(url, std::nullopt);
      }
    }

    if (!queue_.empty()) {
      DownloadNext();
      return;
    }
    if (changed_) {
      changed_ = false;
      // After the files are written, on the same sequence.
      Rebuild();
    }
    observers_.Notify();
  }

  // A subscription's title and state, once downloaded (or not).
  void UpdateSubscription(const GURL& url,
                          const std::optional<std::string>& body) {
    ScopedListPrefUpdate update(LocalState(), kSubscriptionsPref);
    for (base::Value& value : *update) {
      base::DictValue* dict = value.GetIfDict();
      const std::string* sub_url = dict ? dict->FindString("url") : nullptr;
      if (!sub_url || GURL(*sub_url) != url) {
        continue;
      }
      if (body) {
        const std::string title = adblock::ReadListMetadata(*body).title;
        if (!title.empty()) {
          dict->Set("title", title);
        }
        dict->Set("updated", base::TimeToValue(base::Time::Now()));
        dict->Remove("failed");
      } else {
        dict->Set("failed", true);
      }
    }
  }

  void OnBuilt(BuiltEngines engines) {
    EngineHost::Get().SetEngines(std::move(engines.default_engine),
                                 std::move(engines.additional_engine));
    building_ = false;
    if (rebuild_again_) {
      rebuild_again_ = false;
      Rebuild();
    }
  }

  void OnChoicesChanged() {
    // Lists newly turned on need downloading first; the others are there.
    CheckForUpdates(false);
    Rebuild();
    observers_.Notify();
  }

  base::FilePath dir_;
  scoped_refptr<base::SequencedTaskRunner> file_task_runner_;
  GURL test_catalog_;
  std::vector<CatalogEntry> catalog_;
  base::circular_deque<GURL> queue_;
  GURL current_;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  bool changed_ = false;
  bool building_ = false;
  bool rebuild_again_ = false;
  base::RepeatingTimer timer_;
  base::OneShotTimer check_timer_;
  PrefChangeRegistrar registrar_;
  base::RepeatingClosureList observers_;
};

}  // namespace

void RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(kChoicesPref);
  registry->RegisterBooleanPref(kRegionalChosenPref, false);
  registry->RegisterListPref(kSubscriptionsPref);
  registry->RegisterStringPref(kCustomFiltersPref, std::string());
  registry->RegisterDictionaryPref(kSourcesPref);
  registry->RegisterTimePref(kCatalogUpdatedPref, base::Time());
}

void Start() {
  Lists::Get().Start();
}

std::vector<ListInfo> GetLists() {
  return Lists::Get().GetLists();
}

void SetListEnabled(const std::string& uuid, bool enabled) {
  ScopedDictPrefUpdate(LocalState(), kChoicesPref)->Set(uuid, enabled);
}

std::vector<Subscription> GetSubscriptions() {
  std::vector<Subscription> subscriptions;
  for (const base::Value& value : LocalState()->GetList(kSubscriptionsPref)) {
    const base::DictValue* dict = value.GetIfDict();
    const std::string* url = dict ? dict->FindString("url") : nullptr;
    if (!url) {
      continue;
    }
    Subscription subscription;
    subscription.url = *url;
    subscription.enabled = dict->FindBool("enabled").value_or(true);
    if (const std::string* title = dict->FindString("title")) {
      subscription.title = *title;
    }
    subscription.updated =
        base::ValueToTime(dict->Find("updated")).value_or(base::Time());
    subscription.failed = dict->FindBool("failed").value_or(false);
    subscriptions.push_back(std::move(subscription));
  }
  return subscriptions;
}

bool Subscribe(const std::string& url) {
  const GURL gurl(url);
  if (!IsListUrlAllowed(gurl)) {
    return false;
  }
  for (const Subscription& subscription : GetSubscriptions()) {
    if (GURL(subscription.url) == gurl) {
      return true;
    }
  }
  ScopedListPrefUpdate(LocalState(), kSubscriptionsPref)
      ->Append(base::DictValue().Set("url", gurl.spec()).Set("enabled", true));
  return true;
}

void Unsubscribe(const std::string& url) {
  ScopedListPrefUpdate(LocalState(), kSubscriptionsPref)
      ->EraseIf([&](const base::Value& value) {
        const std::string* sub_url =
            value.is_dict() ? value.GetDict().FindString("url") : nullptr;
        return sub_url && *sub_url == url;
      });
}

void SetSubscriptionEnabled(const std::string& url, bool enabled) {
  ScopedListPrefUpdate update(LocalState(), kSubscriptionsPref);
  for (base::Value& value : *update) {
    base::DictValue* dict = value.GetIfDict();
    const std::string* sub_url = dict ? dict->FindString("url") : nullptr;
    if (sub_url && *sub_url == url) {
      dict->Set("enabled", enabled);
    }
  }
}

std::string GetCustomFilters() {
  return LocalState()->GetString(kCustomFiltersPref);
}

void SetCustomFilters(const std::string& filters) {
  LocalState()->SetString(kCustomFiltersPref, filters);
}

void AddCustomFilter(const std::string& filter) {
  std::string filters = GetCustomFilters();
  if (!filters.empty() && !filters.ends_with('\n')) {
    filters += '\n';
  }
  SetCustomFilters(filters + filter + "\n");
}

void UpdateNow() {
  Lists::Get().UpdateNow();
}

base::CallbackListSubscription ObserveLists(base::RepeatingClosure changed) {
  return Lists::Get().observers().Add(std::move(changed));
}

}  // namespace oblada::barrier::filter_lists
