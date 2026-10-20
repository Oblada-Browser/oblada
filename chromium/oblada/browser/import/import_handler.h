#ifndef OBLADA_BROWSER_IMPORT_IMPORT_HANDLER_H_
#define OBLADA_BROWSER_IMPORT_IMPORT_HANDLER_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "oblada/browser/import/firefox_containers.h"
#include "oblada/browser/import/import_sources.h"
#include "oblada/browser/import/safari_import.h"
#include "ui/shell_dialogs/select_file_dialog.h"

namespace oblada::import {

class HostImport;
struct Imported;

// For tests: the file "chooseImportFile" picks, instead of asking the user.
inline constexpr char kImportFileSwitch[] = "oblada-import-file";

// The messages of a page's import panel (oblada://welcome's):
// - "getImportSources" → the browsers found: [{id, browser, profile, family,
//   icon, isDefault, items: ["bookmarks", "history", …]}];
// - "importFrom" (id, items) imports from one; "chooseImportFile" (kind:
//   "safari" or "bookmarks") asks for a file, and Safari's is then imported
//   with "importFrom" ("safari").
// What happens is told with "import-progress" events: {id, state ("running",
// "found", "done", "locked", "failed" or "idle"), counts: {bookmarks: n…},
// unreadable}.
// And containers: "getFirefoxContainers" → the Firefox profiles that have
// some: [{id, browser, profile, containers: [{name, css, mask}]}];
// "importFirefoxContainers" (id) adds them, → how many.
class ImportHandler : public content::WebUIMessageHandler,
                      public ui::SelectFileDialog::Listener {
 public:
  ImportHandler();
  ImportHandler(const ImportHandler&) = delete;
  ImportHandler& operator=(const ImportHandler&) = delete;
  ~ImportHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;
  void OnJavascriptDisallowed() override;

  // ui::SelectFileDialog::Listener:
  void FileSelected(const ui::SelectedFileInfo& file, int index) override;
  void FileSelectionCanceled() override;

 private:
  void HandleGetSources(const base::ListValue& args);
  void HandleImportFrom(const base::ListValue& args);
  void HandleChooseFile(const base::ListValue& args);
  void HandleGetFirefoxContainers(const base::ListValue& args);
  void HandleImportFirefoxContainers(const base::ListValue& args);

  using FoundContainers =
      std::vector<std::pair<Source, std::vector<FirefoxContainer>>>;
  void OnFirefoxContainersFound(base::Value callback_id, FoundContainers found);

  void OnSourcesFound(base::Value callback_id, std::vector<Source> sources);
  void OnFileChosen(const base::FilePath& file);
  void OnChromiumImported(std::string id, const Imported& imported);
  void OnHostImported(std::string id, std::optional<uint16_t> items);
  void OnSafariCounted(std::string state,
                       std::optional<SafariImport::Counts> counts);

  // Imported bookmarks turn the bookmarks bar on (see ProfileWriter), which
  // Oblada's sidebar doesn't want: it is left as it was before.
  void KeepBookmarkBar();
  void RestoreBookmarkBar();

  void Report(const std::string& id,
              const std::string& state,
              base::DictValue counts = {},
              int unreadable = 0);

  std::vector<Source> sources_;
  std::optional<bool> bookmark_bar_shown_;
  // The containers of each Firefox source, by its id.
  std::map<std::string, std::vector<FirefoxContainer>> firefox_containers_;
  // The import of each source, while it runs.
  std::map<std::string, std::unique_ptr<HostImport>> host_imports_;
  std::unique_ptr<SafariImport> safari_;
  scoped_refptr<ui::SelectFileDialog> dialog_;
  // What the file being chosen is for: "safari" or "bookmarks".
  std::string choosing_;
  base::WeakPtrFactory<ImportHandler> weak_factory_{this};
};

}  // namespace oblada::import

#endif  // OBLADA_BROWSER_IMPORT_IMPORT_HANDLER_H_
