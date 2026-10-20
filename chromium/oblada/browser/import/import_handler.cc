#include "oblada/browser/import/import_handler.h"

#include <algorithm>
#include <utility>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/select_file_policy/chrome_select_file_policy.h"
#include "components/bookmarks/common/bookmark_pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "oblada/browser/containers/containers.h"
#include "oblada/browser/import/app_icon.h"
#include "oblada/browser/import/chromium_importer.h"
#include "oblada/browser/import/host_import.h"
#include "oblada/browser/import/safari_import.h"
#include "oblada/browser/l10n.h"
#include "oblada/browser/ui/icons.h"
#include "ui/gfx/color_utils.h"
#include "ui/shell_dialogs/selected_file_info.h"

namespace oblada::import {

namespace {

using user_data_importer::AUTOFILL_FORM_DATA;
using user_data_importer::FAVORITES;
using user_data_importer::HISTORY;
using user_data_importer::PASSWORDS;

// The page's names for the kinds of data.
constexpr std::pair<const char*, uint16_t> kItems[] = {
    {"bookmarks", FAVORITES},
    {"history", HISTORY},
    {"passwords", PASSWORDS},
    {"autofill", AUTOFILL_FORM_DATA},
};

base::ListValue ItemNames(uint16_t items) {
  base::ListValue names;
  for (const auto& [name, item] : kItems) {
    if (items & item) {
      names.Append(name);
    }
  }
  return names;
}

uint16_t ItemsNamed(const base::ListValue& names) {
  uint16_t items = 0;
  for (const base::Value& name : names) {
    for (const auto& [item_name, item] : kItems) {
      if (name.is_string() && name.GetString() == item_name) {
        items |= item;
      }
    }
  }
  return items;
}

const char* FamilyName(Family family) {
  switch (family) {
    case Family::kChromium:
      return "chromium";
    case Family::kFirefox:
      return "firefox";
    case Family::kSafari:
      return "safari";
  }
}

}  // namespace

ImportHandler::ImportHandler() = default;

ImportHandler::~ImportHandler() {
  if (dialog_) {
    dialog_->ListenerDestroyed();
  }
}

void ImportHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "getImportSources",
      base::BindRepeating(&ImportHandler::HandleGetSources,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "importFrom", base::BindRepeating(&ImportHandler::HandleImportFrom,
                                        base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "chooseImportFile", base::BindRepeating(&ImportHandler::HandleChooseFile,
                                              base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getFirefoxContainers",
      base::BindRepeating(&ImportHandler::HandleGetFirefoxContainers,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "importFirefoxContainers",
      base::BindRepeating(&ImportHandler::HandleImportFirefoxContainers,
                          base::Unretained(this)));
}

void ImportHandler::OnJavascriptDisallowed() {
  // Imports under way go on; only their reports are lost.
  weak_factory_.InvalidateWeakPtrs();
}

void ImportHandler::HandleGetSources(const base::ListValue& args) {
  AllowJavascript();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_BLOCKING},
      base::BindOnce(&FindSources),
      base::BindOnce(&ImportHandler::OnSourcesFound,
                     weak_factory_.GetWeakPtr(), args[0].Clone()));
}

void ImportHandler::OnSourcesFound(base::Value callback_id,
                                   std::vector<Source> sources) {
  sources_ = std::move(sources);
  base::ListValue list;
  for (const Source& source : sources_) {
    list.Append(base::DictValue()
                    .Set("id", source.id)
                    .Set("browser", source.browser)
                    .Set("profile", source.profile)
                    .Set("family", FamilyName(source.family))
                    .Set("icon", AppIconDataUrl(source.app_id))
                    .Set("isDefault", source.is_default)
                    // Its passwords' key is in the Keychain: reading them, macOS
                    // asks for the user's password.
                    .Set("asksKeychain", !source.safe_storage.empty())
                    .Set("items", ItemNames(source.items)));
  }
  ResolveJavascriptCallback(callback_id, list);
}

void ImportHandler::HandleImportFrom(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_list()) {
    return;
  }
  const std::string& id = args[0].GetString();
  Profile* profile = Profile::FromWebUI(web_ui());
  if (id == "safari") {
    if (!safari_) {
      return;
    }
    Report(id, "running");
    KeepBookmarkBar();
    safari_->Complete(base::BindOnce(
        &ImportHandler::OnSafariCounted, weak_factory_.GetWeakPtr(), "done"));
    return;
  }
  auto source = std::ranges::find(sources_, id, &Source::id);
  if (source == sources_.end()) {
    return;
  }
  const uint16_t items = ItemsNamed(args[1].GetList()) & source->items;
  if (!items) {
    return;
  }
  Report(id, "running");
  KeepBookmarkBar();
  if (source->family == Family::kChromium) {
    ImportFromChromium(profile, *source, items,
                       base::BindOnce(&ImportHandler::OnChromiumImported,
                                      weak_factory_.GetWeakPtr(), id));
  } else if (source->family == Family::kFirefox) {
    host_imports_[id] = HostImport::FromFirefox(
        profile, *source, items,
        base::BindOnce(&ImportHandler::OnHostImported,
                       weak_factory_.GetWeakPtr(), id));
  }
}

void ImportHandler::HandleChooseFile(const base::ListValue& args) {
  AllowJavascript();
  if (args.empty() || !args[0].is_string() || dialog_) {
    return;
  }
  choosing_ = args[0].GetString();
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch(kImportFileSwitch)) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&ImportHandler::OnFileChosen,
                                  weak_factory_.GetWeakPtr(),
                                  command_line.GetSwitchValuePath(
                                      kImportFileSwitch)));
    return;
  }
  content::WebContents* contents = web_ui()->GetWebContents();
  dialog_ = ui::SelectFileDialog::Create(
      this, std::make_unique<ChromeSelectFilePolicy>(contents));
  ui::SelectFileDialog::FileTypeInfo types;
  types.extensions = {{choosing_ == "safari" ? FILE_PATH_LITERAL("zip")
                                             : FILE_PATH_LITERAL("html")}};
  dialog_->SelectFile(ui::SelectFileDialog::SELECT_OPEN_FILE,
                      L10n(choosing_ == "safari" ? "welcome.import_safari_choose"
                                                 : "welcome.import_file"),
                      base::FilePath(), &types, 1, base::FilePath::StringType(),
                      contents->GetTopLevelNativeWindow());
}

void ImportHandler::FileSelected(const ui::SelectedFileInfo& file, int index) {
  dialog_ = nullptr;
  OnFileChosen(file.path());
}

void ImportHandler::FileSelectionCanceled() {
  dialog_ = nullptr;
  Report(choosing_, "idle");
}

void ImportHandler::OnFileChosen(const base::FilePath& file) {
  Profile* profile = Profile::FromWebUI(web_ui());
  Report(choosing_, "running");
  if (choosing_ == "safari") {
    safari_ = std::make_unique<SafariImport>(profile);
    safari_->Prepare(file, base::BindOnce(&ImportHandler::OnSafariCounted,
                                          weak_factory_.GetWeakPtr(), "found"));
  } else {
    KeepBookmarkBar();
    host_imports_[choosing_] = HostImport::FromBookmarksFile(
        profile, file,
        base::BindOnce(&ImportHandler::OnHostImported,
                       weak_factory_.GetWeakPtr(), choosing_));
  }
}

void ImportHandler::OnChromiumImported(std::string id,
                                       const Imported& imported) {
  RestoreBookmarkBar();
  base::DictValue counts;
  for (const auto& [name, count] :
       {std::pair("bookmarks", imported.bookmarks),
        std::pair("history", imported.history),
        std::pair("passwords", imported.passwords)}) {
    if (count >= 0) {
      counts.Set(name, count);
    }
  }
  Report(id, "done", std::move(counts), imported.passwords_unreadable);
}

void ImportHandler::OnHostImported(std::string id,
                                   std::optional<uint16_t> items) {
  // Called by it: let it return first.
  base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
      FROM_HERE, std::move(host_imports_[id]));
  host_imports_.erase(id);
  RestoreBookmarkBar();
  if (!items) {
    Report(id, "locked");
    return;
  }
  // Chromium's importers don't count: the kinds imported, as -1.
  base::DictValue counts;
  for (const base::Value& name : ItemNames(*items)) {
    counts.Set(name.GetString(), -1);
  }
  Report(id, *items ? "done" : "failed", std::move(counts));
}

void ImportHandler::OnSafariCounted(std::string state,
                                    std::optional<SafariImport::Counts> counts) {
  if (state == "done") {
    RestoreBookmarkBar();
  }
  if (!counts) {
    Report("safari", "failed");
    return;
  }
  Report("safari", state,
         base::DictValue()
             .Set("bookmarks", counts->bookmarks)
             .Set("history", counts->history)
             .Set("passwords", counts->passwords)
             .Set("cards", counts->cards));
}

void ImportHandler::HandleGetFirefoxContainers(const base::ListValue& args) {
  AllowJavascript();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_BLOCKING},
      base::BindOnce([] {
        FoundContainers found;
        for (Source& source : FindSources()) {
          if (source.family != Family::kFirefox) {
            continue;
          }
          std::vector<FirefoxContainer> source_containers =
              ReadFirefoxContainers(source.dir);
          if (!source_containers.empty()) {
            found.emplace_back(std::move(source), std::move(source_containers));
          }
        }
        return found;
      }),
      base::BindOnce(&ImportHandler::OnFirefoxContainersFound,
                     weak_factory_.GetWeakPtr(), args[0].Clone()));
}

void ImportHandler::OnFirefoxContainersFound(base::Value callback_id,
                                             FoundContainers found) {
  base::ListValue list;
  for (auto& [source, source_containers] : found) {
    base::ListValue shown;
    for (const FirefoxContainer& container : source_containers) {
      shown.Append(
          base::DictValue()
              .Set("name", container.name.empty()
                               ? L10nUTF8("import.container_" +
                                          container.default_id)
                               : container.name)
              .Set("css", color_utils::SkColorToRgbaString(
                              containers::ColorFor(container.color)))
              .Set("mask", icons::MaskUrl(container.icon, 20)));
    }
    list.Append(base::DictValue()
                    .Set("id", source.id)
                    .Set("browser", source.browser)
                    .Set("profile", source.profile)
                    .Set("containers", std::move(shown)));
    firefox_containers_[source.id] = std::move(source_containers);
  }
  ResolveJavascriptCallback(callback_id, list);
}

void ImportHandler::HandleImportFirefoxContainers(
    const base::ListValue& args) {
  AllowJavascript();
  int count = 0;
  if (args.size() > 1 && args[1].is_string()) {
    auto found = firefox_containers_.find(args[1].GetString());
    if (found != firefox_containers_.end()) {
      count = AddFirefoxContainers(Profile::FromWebUI(web_ui()), found->second);
    }
  }
  ResolveJavascriptCallback(args[0], base::Value(count));
}

void ImportHandler::KeepBookmarkBar() {
  if (!bookmark_bar_shown_) {
    bookmark_bar_shown_ = Profile::FromWebUI(web_ui())->GetPrefs()->GetBoolean(
        bookmarks::prefs::kShowBookmarkBar);
  }
}

void ImportHandler::RestoreBookmarkBar() {
  if (bookmark_bar_shown_) {
    Profile::FromWebUI(web_ui())->GetPrefs()->SetBoolean(
        bookmarks::prefs::kShowBookmarkBar, *bookmark_bar_shown_);
  }
}

void ImportHandler::Report(const std::string& id,
                           const std::string& state,
                           base::DictValue counts,
                           int unreadable) {
  if (!IsJavascriptAllowed()) {
    return;
  }
  FireWebUIListener("import-progress", base::DictValue()
                                           .Set("id", id)
                                           .Set("state", state)
                                           .Set("counts", std::move(counts))
                                           .Set("unreadable", unreadable));
}

}  // namespace oblada::import
