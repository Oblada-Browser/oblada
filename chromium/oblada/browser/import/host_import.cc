#include "oblada/browser/import/host_import.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/memory/ptr_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/importer/external_process_importer_host.h"
#include "chrome/browser/importer/firefox_profile_lock.h"
#include "chrome/browser/importer/profile_writer.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/importer/firefox_importer_utils.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "oblada/browser/import/import_sources.h"

namespace oblada::import {

namespace {

// Whether Firefox's profile can be read (it isn't open), and the app it was
// last used with (Firefox's importer loads some of its libraries).
std::pair<bool, base::FilePath> FirefoxCheck(const base::FilePath& dir) {
  FirefoxProfileLock lock(dir);
  int version = 0;
  base::FilePath app;
  GetFirefoxVersionAndPathFromProfile(dir, &version, &app);
  return {lock.HasAcquired(), app};
}

}  // namespace

// static
std::unique_ptr<HostImport> HostImport::FromFirefox(Profile* profile,
                                                    const Source& source,
                                                    uint16_t items,
                                                    DoneCallback done) {
  auto import = base::WrapUnique(new HostImport(profile, std::move(done)));
  user_data_importer::SourceProfile firefox;
  firefox.importer_name = base::UTF8ToUTF16(source.browser);
  firefox.importer_type = user_data_importer::TYPE_FIREFOX;
  firefox.source_path = source.dir;
  firefox.services_supported = source.items;
  firefox.locale = g_browser_process->GetApplicationLocale();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&FirefoxCheck, source.dir),
      base::BindOnce(
          [](base::WeakPtr<HostImport> import,
             user_data_importer::SourceProfile firefox, uint16_t items,
             std::pair<bool, base::FilePath> check) {
            if (import) {
              firefox.app_path = check.second;
              import->Start(std::move(firefox), items, check.first);
            }
          },
          import->weak_factory_.GetWeakPtr(), std::move(firefox),
          items & source.items));
  return import;
}

// static
std::unique_ptr<HostImport> HostImport::FromBookmarksFile(
    Profile* profile,
    const base::FilePath& file,
    DoneCallback done) {
  auto import = base::WrapUnique(new HostImport(profile, std::move(done)));
  user_data_importer::SourceProfile bookmarks;
  bookmarks.importer_type = user_data_importer::TYPE_BOOKMARKS_FILE;
  bookmarks.source_path = file;
  bookmarks.services_supported = user_data_importer::FAVORITES;
  // Not right away: `done` may run, and the caller isn't holding it yet.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&HostImport::Start, import->weak_factory_.GetWeakPtr(),
                     std::move(bookmarks), user_data_importer::FAVORITES,
                     true));
  return import;
}

HostImport::HostImport(Profile* profile, DoneCallback done)
    : profile_(profile->GetWeakPtr()), done_(std::move(done)) {}

HostImport::~HostImport() {
  if (host_) {
    host_->set_observer(nullptr);
  }
}

void HostImport::Start(user_data_importer::SourceProfile source,
                       uint16_t items,
                       bool readable) {
  if (!profile_ || !items) {
    return;
  }
  if (!readable) {
    std::move(done_).Run(std::nullopt);
    return;
  }
  host_ = new ExternalProcessImporterHost();
  // No dialogs of its own: the page tells the user what happens.
  host_->set_headless();
  host_->set_observer(this);
  host_->StartImportSettings(source, profile_.get(), items,
                             new ProfileWriter(profile_.get()));
}

void HostImport::ImportItemEnded(user_data_importer::ImportItem item) {
  imported_ |= item;
}

void HostImport::ImportEnded() {
  host_ = nullptr;
  if (done_) {
    std::move(done_).Run(imported_);
  }
}

}  // namespace oblada::import
