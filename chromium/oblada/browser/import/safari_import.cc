#include "oblada/browser/import/safari_import.h"

#include <utility>

#include "chrome/browser/affiliations/affiliation_service_factory.h"
#include "chrome/browser/autofill/personal_data_manager_factory.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/password_manager/factories/account_password_store_factory.h"
#include "chrome/browser/password_manager/factories/profile_password_store_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/reading_list/reading_list_model_factory.h"
#include "chrome/browser/sync/sync_service_factory.h"
#include "components/autofill/core/browser/data_manager/personal_data_manager.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/password_manager/core/browser/ui/saved_passwords_presenter.h"
#include "components/user_data_importer/content/content_bookmark_parser.h"
#include "components/user_data_importer/utility/safari_data_importer.h"

namespace oblada::import {

namespace {

// Bookmarks, history, passwords and payment cards.
constexpr int kKinds = 4;

int Count(const user_data_importer::CountOrError& result) {
  return result.has_value() ? static_cast<int>(result.value()) : 0;
}

}  // namespace

SafariImport::SafariImport(Profile* profile) {
  presenter_ = std::make_unique<password_manager::SavedPasswordsPresenter>(
      AffiliationServiceFactory::GetForProfile(profile),
      ProfilePasswordStoreFactory::GetForProfile(
          profile, ServiceAccessType::EXPLICIT_ACCESS),
      AccountPasswordStoreFactory::GetForProfile(
          profile, ServiceAccessType::EXPLICIT_ACCESS));
  presenter_->Init();
  importer_ = std::make_unique<user_data_importer::SafariDataImporter>(
      this, presenter_.get(),
      &autofill::PersonalDataManagerFactory::GetForBrowserContext(profile)
           ->payments_data_manager(),
      HistoryServiceFactory::GetForProfile(profile,
                                           ServiceAccessType::EXPLICIT_ACCESS),
      BookmarkModelFactory::GetForBrowserContext(profile),
      ReadingListModelFactory::GetForBrowserContext(profile),
      SyncServiceFactory::GetForProfile(profile), profile->GetPrefs(),
      std::make_unique<user_data_importer::ContentBookmarkParser>(),
      g_browser_process->GetApplicationLocale());
}

SafariImport::~SafariImport() = default;

void SafariImport::Prepare(const base::FilePath& file, Callback found) {
  counts_ = Counts();
  pending_ = kKinds;
  callback_ = std::move(found);
  importer_->PrepareImport(file);
}

void SafariImport::Complete(Callback imported) {
  counts_ = Counts();
  pending_ = kKinds;
  callback_ = std::move(imported);
  // Passwords conflicting with ones already saved keep the saved ones.
  importer_->CompleteImport({});
}

void SafariImport::Counted() {
  if (--pending_ == 0 && callback_) {
    std::move(callback_).Run(counts_);
  }
}

void SafariImport::OnTotalFailure() {
  pending_ = 0;
  if (callback_) {
    std::move(callback_).Run(std::nullopt);
  }
}

void SafariImport::OnBookmarksReady(user_data_importer::CountOrError result) {
  counts_.bookmarks = Count(result);
  Counted();
}

void SafariImport::OnHistoryReady(user_data_importer::CountOrError result) {
  counts_.history = Count(result);
  Counted();
}

void SafariImport::OnPasswordsReady(
    base::expected<password_manager::ImportResults,
                   user_data_importer::ImportPreparationError> results) {
  counts_.passwords =
      results.has_value() ? static_cast<int>(results->number_to_import) : 0;
  Counted();
}

void SafariImport::OnPaymentCardsReady(
    user_data_importer::CountOrError result) {
  counts_.cards = Count(result);
  Counted();
}

void SafariImport::OnBookmarksImported(size_t count) {
  counts_.bookmarks = static_cast<int>(count);
  Counted();
}

void SafariImport::OnHistoryImported(size_t count) {
  counts_.history = static_cast<int>(count);
  Counted();
}

void SafariImport::OnPasswordsImported(
    const password_manager::ImportResults& results) {
  counts_.passwords = static_cast<int>(results.number_imported);
  Counted();
}

void SafariImport::OnPaymentCardsImported(size_t count) {
  counts_.cards = static_cast<int>(count);
  Counted();
}

base::WeakPtr<user_data_importer::SafariDataImportClient>
SafariImport::AsWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

}  // namespace oblada::import
