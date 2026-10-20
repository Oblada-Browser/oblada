#ifndef OBLADA_BROWSER_IMPORT_SAFARI_IMPORT_H_
#define OBLADA_BROWSER_IMPORT_SAFARI_IMPORT_H_

#include <memory>
#include <optional>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "components/user_data_importer/utility/safari_data_import_client.h"

class Profile;

namespace password_manager {
class SavedPasswordsPresenter;
}  // namespace password_manager

namespace user_data_importer {
class SafariDataImporter;
}  // namespace user_data_importer

namespace oblada::import {

// Imports what Safari exports to a file (File › Export Browsing Data to File…:
// a ZIP of its bookmarks, history, passwords and cards), with Chromium's
// importer for it. Two steps: Prepare reads the file, Complete imports what
// was in it.
class SafariImport : public user_data_importer::SafariDataImportClient {
 public:
  struct Counts {
    int bookmarks = 0;
    int history = 0;
    int passwords = 0;
    int cards = 0;
  };
  // Nothing if the file couldn't be read.
  using Callback = base::OnceCallback<void(std::optional<Counts>)>;

  explicit SafariImport(Profile* profile);
  SafariImport(const SafariImport&) = delete;
  SafariImport& operator=(const SafariImport&) = delete;
  ~SafariImport() override;

  // `found` gets what is in `file` (history is an estimate).
  void Prepare(const base::FilePath& file, Callback found);
  // `imported` gets what was imported.
  void Complete(Callback imported);

  // user_data_importer::SafariDataImportClient:
  void OnTotalFailure() override;
  void OnBookmarksReady(user_data_importer::CountOrError result) override;
  void OnHistoryReady(user_data_importer::CountOrError result) override;
  void OnPasswordsReady(
      base::expected<password_manager::ImportResults,
                     user_data_importer::ImportPreparationError> results)
      override;
  void OnPaymentCardsReady(user_data_importer::CountOrError result) override;
  void OnBookmarksImported(size_t count) override;
  void OnHistoryImported(size_t count) override;
  void OnPasswordsImported(
      const password_manager::ImportResults& results) override;
  void OnPaymentCardsImported(size_t count) override;
  base::WeakPtr<SafariDataImportClient> AsWeakPtr() override;

 private:
  // Counts one of the four kinds of data in; runs the callback after the last.
  void Counted();

  std::unique_ptr<password_manager::SavedPasswordsPresenter> presenter_;
  std::unique_ptr<user_data_importer::SafariDataImporter> importer_;
  Counts counts_;
  int pending_ = 0;
  Callback callback_;
  base::WeakPtrFactory<SafariImport> weak_factory_{this};
};

}  // namespace oblada::import

#endif  // OBLADA_BROWSER_IMPORT_SAFARI_IMPORT_H_
