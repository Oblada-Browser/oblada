#ifndef OBLADA_BROWSER_IMPORT_HOST_IMPORT_H_
#define OBLADA_BROWSER_IMPORT_HOST_IMPORT_H_

#include <cstdint>
#include <memory>
#include <optional>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/importer/importer_progress_observer.h"
#include "components/user_data_importer/common/importer_data_types.h"

class ExternalProcessImporterHost;
class Profile;

namespace oblada::import {

struct Source;

// Imports with Chromium's own importers, which read in a utility process: a
// Firefox profile, or a bookmarks HTML file. `done` gets the items
// (user_data_importer::ImportItem bits) imported, or nothing if the Firefox
// profile couldn't be read: Firefox keeps it locked while open.
class HostImport : public importer::ImporterProgressObserver {
 public:
  using DoneCallback = base::OnceCallback<void(std::optional<uint16_t>)>;

  static std::unique_ptr<HostImport> FromFirefox(Profile* profile,
                                                 const Source& source,
                                                 uint16_t items,
                                                 DoneCallback done);
  static std::unique_ptr<HostImport> FromBookmarksFile(
      Profile* profile,
      const base::FilePath& file,
      DoneCallback done);

  HostImport(const HostImport&) = delete;
  HostImport& operator=(const HostImport&) = delete;
  ~HostImport() override;

  // importer::ImporterProgressObserver:
  void ImportStarted() override {}
  void ImportItemStarted(user_data_importer::ImportItem item) override {}
  void ImportItemEnded(user_data_importer::ImportItem item) override;
  void ImportEnded() override;

 private:
  HostImport(Profile* profile, DoneCallback done);

  // Starts, if the profile isn't locked (see FirefoxCheck in the .cc).
  void Start(user_data_importer::SourceProfile source,
             uint16_t items,
             bool readable);

  base::WeakPtr<Profile> profile_;
  DoneCallback done_;
  // Deletes itself once done.
  raw_ptr<ExternalProcessImporterHost> host_ = nullptr;
  uint16_t imported_ = 0;
  base::WeakPtrFactory<HostImport> weak_factory_{this};
};

}  // namespace oblada::import

#endif  // OBLADA_BROWSER_IMPORT_HOST_IMPORT_H_
