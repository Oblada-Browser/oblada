#include "oblada/browser/barrier/data_lists.h"

#include <array>
#include <optional>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/task/thread_pool.h"
#include "base/timer/timer.h"
#include "chrome/browser/browser_process.h"
#include "chrome/common/chrome_paths.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "net/base/load_flags.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "oblada/barrier_lists.h"
#include "oblada/browser/barrier/barrier.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "url/gurl.h"

namespace oblada::barrier::data_lists {

namespace {

constexpr char kBaseUrl[] =
    "https://raw.githubusercontent.com/brave/adblock-lists/master/"
    "brave-lists/";
constexpr char kDataSwitch[] = "oblada-barrier-data";
constexpr char kCatalogSwitch[] = "oblada-barrier-catalog";

// Local State: when they were last all downloaded.
constexpr char kUpdatedPref[] = "oblada.barrier.data_lists_updated";

constexpr base::TimeDelta kFirstCheck = base::Seconds(10);
constexpr base::TimeDelta kCheckInterval = base::Hours(1);
constexpr base::TimeDelta kExpiry = base::Days(1);
constexpr size_t kMaxSize = 8 * 1024 * 1024;

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("oblada_barrier_data_lists", R"(
      semantics {
        sender: "Oblada Barrier"
        description:
          "Downloads Brave's lists of tracking parameters, tracking redirects, "
          "sensitive sites, fingerprinting protection's exceptions and sites "
          "not upgraded to HTTPS, from Brave's repository on GitHub."
        trigger: "Shortly after the browser starts, then daily."
        data: "None."
        destination: OTHER
      }
      policy {
        cookies_allowed: NO
        setting: "Settings > Oblada > DANGEROUS turns the Barrier off."
        policy_exception_justification: "Not implemented."
      })");

struct ListInfo {
  DataList list;
  // In Brave's repository, and on disk.
  const char* file;
  const char* built_in;
  bool json;
};

constexpr std::array<ListInfo, 5> kLists = {{
    {DataList::kQueryFilter, "query-filter.json", kQueryFilterList, true},
    {DataList::kDebounce, "debounce.json", kDebounceList, true},
    {DataList::kSensitiveSites, "request-otr.json", kSensitiveSitesList, true},
    {DataList::kWebcompatExceptions, "webcompat-exceptions.json",
     kWebcompatExceptionsList, true},
    {DataList::kHttpsUpgradeExceptions, "https-upgrade-exceptions-list.txt",
     kHttpsUpgradeExceptionsList, false},
}};

const ListInfo& InfoOf(DataList list) {
  return kLists[static_cast<size_t>(list)];
}

// Whether `text` can stand for `info`'s list: JSON lists must parse.
bool IsValid(const ListInfo& info, const std::string& text) {
  if (text.empty() || text.size() > kMaxSize) {
    return false;
  }
  if (!info.json) {
    return true;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(text, base::JSON_PARSE_RFC);
  return parsed && (parsed->is_list() || parsed->is_dict());
}

base::FilePath DataDir() {
  base::FilePath user_data;
  base::PathService::Get(chrome::DIR_USER_DATA, &user_data);
  return user_data.Append(FILE_PATH_LITERAL("Oblada Barrier"))
      .Append(FILE_PATH_LITERAL("data"));
}

// The lists' texts, downloaded ones replacing those built in.
class Texts {
 public:
  static Texts& Get() {
    static base::NoDestructor<Texts> texts;
    return *texts;
  }

  std::string GetText(DataList list) {
    base::AutoLock lock(lock_);
    const std::optional<std::string>& text =
        texts_[static_cast<size_t>(list)];
    return text ? *text : std::string(InfoOf(list).built_in);
  }

  int GetVersion(DataList list) {
    base::AutoLock lock(lock_);
    return versions_[static_cast<size_t>(list)];
  }

  void SetText(DataList list, std::string text) {
    base::AutoLock lock(lock_);
    texts_[static_cast<size_t>(list)] = std::move(text);
    ++versions_[static_cast<size_t>(list)];
  }

 private:
  friend class base::NoDestructor<Texts>;
  Texts() = default;

  base::Lock lock_;
  std::array<std::optional<std::string>, kLists.size()> texts_
      GUARDED_BY(lock_);
  std::array<int, kLists.size()> versions_ GUARDED_BY(lock_) = {};
};

// The lists found on disk, by index in kLists.
std::vector<std::pair<size_t, std::string>> ReadLists(base::FilePath dir) {
  std::vector<std::pair<size_t, std::string>> lists;
  for (size_t i = 0; i < kLists.size(); ++i) {
    std::string text;
    if (base::ReadFileToString(dir.AppendASCII(kLists[i].file), &text) &&
        IsValid(kLists[i], text)) {
      lists.emplace_back(i, std::move(text));
    }
  }
  return lists;
}

class Updater {
 public:
  static Updater& Get() {
    static base::NoDestructor<Updater> updater;
    return *updater;
  }

  void Start() {
    dir_ = DataDir();
    file_task_runner_ = base::ThreadPool::CreateSequencedTaskRunner(
        {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
         base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN});
    const base::CommandLine& command_line =
        *base::CommandLine::ForCurrentProcess();
    test_base_ = GURL(command_line.GetSwitchValueASCII(kDataSwitch));
    file_task_runner_->PostTaskAndReplyWithResult(
        FROM_HERE, base::BindOnce(&ReadLists, dir_),
        base::BindOnce(&Updater::OnRead, base::Unretained(this)));
    if (!test_base_.is_valid() && command_line.HasSwitch(kCatalogSwitch)) {
      return;
    }
    timer_.Start(FROM_HERE, kCheckInterval,
                 base::BindRepeating(&Updater::Check, base::Unretained(this)));
    first_check_.Start(
        FROM_HERE, test_base_.is_valid() ? base::TimeDelta() : kFirstCheck,
        base::BindOnce(&Updater::Check, base::Unretained(this)));
  }

 private:
  friend class base::NoDestructor<Updater>;
  Updater() = default;

  void OnRead(std::vector<std::pair<size_t, std::string>> lists) {
    for (auto& [index, text] : lists) {
      // Not over one downloaded meanwhile.
      if (Texts::Get().GetVersion(kLists[index].list) == 0) {
        Texts::Get().SetText(kLists[index].list, std::move(text));
      }
    }
  }

  void Check() {
    if (!IsOn() || loader_ ||
        (!test_base_.is_valid() &&
         base::Time::Now() -
                 g_browser_process->local_state()->GetTime(kUpdatedPref) <
             kExpiry)) {
      return;
    }
    next_ = 0;
    all_ok_ = true;
    DownloadNext();
  }

  void DownloadNext() {
    if (next_ >= kLists.size()) {
      if (all_ok_ && !test_base_.is_valid()) {
        g_browser_process->local_state()->SetTime(kUpdatedPref,
                                                  base::Time::Now());
      }
      return;
    }
    const GURL base = test_base_.is_valid() ? test_base_ : GURL(kBaseUrl);
    auto request = std::make_unique<network::ResourceRequest>();
    request->url = base.Resolve(kLists[next_].file);
    request->credentials_mode = network::mojom::CredentialsMode::kOmit;
    request->load_flags = net::LOAD_BYPASS_CACHE | net::LOAD_DISABLE_CACHE;
    loader_ = network::SimpleURLLoader::Create(std::move(request),
                                               kTrafficAnnotation);
    loader_->SetRetryOptions(
        1, network::SimpleURLLoader::RETRY_ON_NETWORK_CHANGE);
    loader_->DownloadToString(
        g_browser_process->shared_url_loader_factory().get(),
        base::BindOnce(&Updater::OnDownloaded, base::Unretained(this)),
        kMaxSize);
  }

  void OnDownloaded(std::optional<std::string> body) {
    const ListInfo& info = kLists[next_];
    const bool ok =
        body && loader_->NetError() == net::OK && IsValid(info, *body);
    loader_.reset();
    if (ok) {
      if (*body != Texts::Get().GetText(info.list)) {
        file_task_runner_->PostTask(
            FROM_HERE, base::BindOnce(
                           [](base::FilePath file, std::string text) {
                             base::CreateDirectory(file.DirName());
                             base::ImportantFileWriter::WriteFileAtomically(
                                 file, text);
                           },
                           dir_.AppendASCII(info.file), *body));
        Texts::Get().SetText(info.list, std::move(*body));
      }
    } else {
      all_ok_ = false;
    }
    ++next_;
    DownloadNext();
  }

  base::FilePath dir_;
  scoped_refptr<base::SequencedTaskRunner> file_task_runner_;
  GURL test_base_;
  size_t next_ = 0;
  bool all_ok_ = true;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  base::RepeatingTimer timer_;
  base::OneShotTimer first_check_;
};

}  // namespace

std::string GetText(DataList list) {
  return Texts::Get().GetText(list);
}

int GetVersion(DataList list) {
  return Texts::Get().GetVersion(list);
}

void RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterTimePref(kUpdatedPref, base::Time());
}

void Start() {
  Updater::Get().Start();
}

}  // namespace oblada::barrier::data_lists
