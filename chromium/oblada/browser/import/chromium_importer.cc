#include "oblada/browser/import/chromium_importer.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/json/json_reader.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/importer/profile_writer.h"
#include "chrome/browser/profiles/profile.h"
#include "components/history/core/browser/history_backend.h"
#include "components/history/core/browser/history_types.h"
#include "components/history/core/browser/url_row.h"
#include "components/password_manager/core/browser/password_form.h"
#include "components/password_manager/core/browser/password_string.h"
#include "components/user_data_importer/common/imported_bookmark_entry.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "oblada/browser/import/import_sources.h"
#include "oblada/browser/import/password_decryptor.h"
#include "oblada/browser/l10n.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "url/gurl.h"

namespace oblada::import {

namespace {

using BookmarkEntry = user_data_importer::ImportedBookmarkEntry;
using password_manager::PasswordForm;

// History beyond this many pages (the most recent ones) is left behind.
constexpr int kMaxHistory = 100000;

// What was read from the browser's files, to be added to the profile.
struct Data {
  std::vector<BookmarkEntry> bookmarks;
  std::optional<history::URLRows> history;
  std::optional<std::vector<PasswordForm>> passwords;
  int passwords_unreadable = 0;
};

// Chromium's times: microseconds since 1601.
base::Time FromChromiumTime(int64_t microseconds) {
  return microseconds ? base::Time::FromDeltaSinceWindowsEpoch(
                            base::Microseconds(microseconds))
                      : base::Time();
}

// The bookmarks in `folder` (a node of the Bookmarks file), `path` being the
// folders it is in.
void ReadFolder(const base::DictValue& folder,
                bool in_toolbar,
                std::vector<std::u16string>* path,
                std::vector<BookmarkEntry>* out) {
  const base::ListValue* children = folder.FindList("children");
  if (!children) {
    return;
  }
  for (const base::Value& child : *children) {
    if (!child.is_dict()) {
      continue;
    }
    const base::DictValue& node = child.GetDict();
    const std::string* type = node.FindString("type");
    const std::string* name = node.FindString("name");
    const std::string* added = node.FindString("date_added");
    int64_t added_time = 0;
    if (!type || (added && !base::StringToInt64(*added, &added_time))) {
      continue;
    }
    BookmarkEntry entry;
    entry.in_toolbar = in_toolbar;
    entry.path = *path;
    entry.title = name ? base::UTF8ToUTF16(*name) : std::u16string();
    entry.creation_time = FromChromiumTime(added_time);
    if (*type == "url") {
      const std::string* url = node.FindString("url");
      entry.url = GURL(url ? *url : std::string());
      if (entry.url.is_valid()) {
        out->push_back(std::move(entry));
      }
    } else if (*type == "folder") {
      const base::ListValue* contents = node.FindList("children");
      if (!contents || contents->empty()) {
        entry.is_folder = true;
        out->push_back(std::move(entry));
        continue;
      }
      path->push_back(entry.title);
      ReadFolder(node, in_toolbar, path, out);
      path->pop_back();
    }
  }
}

std::vector<BookmarkEntry> ReadBookmarks(const base::FilePath& dir) {
  std::vector<BookmarkEntry> bookmarks;
  std::string text;
  if (!base::ReadFileToString(dir.Append(FILE_PATH_LITERAL("Bookmarks")),
                              &text)) {
    return bookmarks;
  }
  std::optional<base::DictValue> file =
      base::JSONReader::ReadDict(text, base::JSON_PARSE_RFC);
  const base::DictValue* roots = file ? file->FindDict("roots") : nullptr;
  if (!roots) {
    return bookmarks;
  }
  std::vector<std::u16string> path;
  // The bar's own folder starts the path of what is in it, as with Chromium's
  // importers: dropped if they go straight to the bar.
  if (const base::DictValue* bar = roots->FindDict("bookmark_bar")) {
    const std::string* name = bar->FindString("name");
    path.push_back(name ? base::UTF8ToUTF16(*name) : u"Bookmarks bar");
    ReadFolder(*bar, /*in_toolbar=*/true, &path, &bookmarks);
    path.pop_back();
  }
  if (const base::DictValue* other = roots->FindDict("other")) {
    ReadFolder(*other, /*in_toolbar=*/false, &path, &bookmarks);
  }
  // The phone's, in a folder of their own.
  if (const base::DictValue* synced = roots->FindDict("synced")) {
    const std::string* name = synced->FindString("name");
    path.push_back(name ? base::UTF8ToUTF16(*name) : u"Mobile");
    ReadFolder(*synced, /*in_toolbar=*/false, &path, &bookmarks);
  }
  return bookmarks;
}

// Opens a copy of the database `name` in `dir`: the browser may be running,
// and keeping it locked.
bool OpenCopy(const base::FilePath& dir,
              const base::FilePath::StringType& name,
              const base::ScopedTempDir& temp,
              sql::Database* db) {
  const base::FilePath copy = temp.GetPath().Append(name);
  if (!base::CopyFile(dir.Append(name), copy)) {
    return false;
  }
  // With what isn't in it yet.
  for (const base::FilePath::CharType* suffix :
       {FILE_PATH_LITERAL("-journal"), FILE_PATH_LITERAL("-wal")}) {
    const base::FilePath journal = dir.Append(name + suffix);
    if (base::PathExists(journal)) {
      base::CopyFile(journal, temp.GetPath().Append(name + suffix));
    }
  }
  return db->Open(copy);
}

history::URLRows ReadHistory(const base::FilePath& dir,
                             const base::ScopedTempDir& temp) {
  history::URLRows rows;
  // Tagged as Chromium's own importers' databases.
  sql::Database db("FirefoxImporter");
  if (!OpenCopy(dir, FILE_PATH_LITERAL("History"), temp, &db)) {
    return rows;
  }
  // History drops what is older than it keeps anyway.
  const base::Time oldest =
      base::Time::Now() -
      base::Days(history::HistoryBackend::kExpireDaysThreshold);
  sql::Statement statement(db.GetUniqueStatement(
      "SELECT url, title, visit_count, typed_count, last_visit_time FROM urls "
      "WHERE hidden = 0 AND last_visit_time > ? "
      "ORDER BY last_visit_time DESC LIMIT ?"));
  statement.BindInt64(
      0, oldest.ToDeltaSinceWindowsEpoch().InMicroseconds());
  statement.BindInt(1, kMaxHistory);
  while (statement.Step()) {
    const GURL url(statement.ColumnStringView(0));
    if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
      continue;
    }
    history::URLRow row(url);
    row.set_title(statement.ColumnString16(1));
    row.set_visit_count(statement.ColumnInt(2));
    row.set_typed_count(statement.ColumnInt(3));
    row.set_last_visit(FromChromiumTime(statement.ColumnInt64(4)));
    rows.push_back(std::move(row));
  }
  return rows;
}

void ReadPasswords(const Source& source,
                   const base::ScopedTempDir& temp,
                   Data* data) {
  data->passwords.emplace();
  sql::Database db("FirefoxImporter");
  if (!OpenCopy(source.dir, FILE_PATH_LITERAL("Login Data"), temp, &db)) {
    return;
  }
  sql::Statement statement(db.GetUniqueStatement(
      "SELECT origin_url, action_url, username_element, username_value, "
      "password_element, password_value, signon_realm, date_created, scheme "
      "FROM logins WHERE blacklisted_by_user = 0"));
  // Only asked for (which may ask the user) when there are passwords.
  std::unique_ptr<PasswordDecryptor> decryptor;
  while (statement.Step()) {
    if (!decryptor) {
      decryptor = PasswordDecryptor::Create(source);
    }
    std::optional<std::string> password =
        decryptor->Decrypt(statement.ColumnBlob(5));
    PasswordForm form;
    form.url = GURL(statement.ColumnStringView(0));
    form.action = GURL(statement.ColumnStringView(1));
    form.username_element = statement.ColumnString16(2);
    form.username_value = statement.ColumnString16(3);
    form.password_element = statement.ColumnString16(4);
    form.signon_realm = statement.ColumnString(6);
    form.date_created = FromChromiumTime(statement.ColumnInt64(7));
    const int scheme = statement.ColumnInt(8);
    if (!password || password->empty() || !form.url.is_valid() ||
        form.signon_realm.empty() ||
        scheme < static_cast<int>(PasswordForm::Scheme::kMinValue) ||
        scheme > static_cast<int>(PasswordForm::Scheme::kMaxValue)) {
      data->passwords_unreadable++;
      continue;
    }
    form.scheme = static_cast<PasswordForm::Scheme>(scheme);
    form.password_value =
        password_manager::PasswordString(base::UTF8ToUTF16(*password));
    data->passwords->push_back(std::move(form));
  }
}

Data Read(const Source& source, uint16_t items) {
  base::ScopedBlockingCall blocking(FROM_HERE, base::BlockingType::MAY_BLOCK);
  Data data;
  if (items & user_data_importer::FAVORITES) {
    data.bookmarks = ReadBookmarks(source.dir);
  }
  base::ScopedTempDir temp;
  if (!temp.CreateUniqueTempDir()) {
    return data;
  }
  if (items & user_data_importer::HISTORY) {
    data.history = ReadHistory(source.dir, temp);
  }
  if (items & user_data_importer::PASSWORDS) {
    ReadPasswords(source, temp, &data);
  }
  return data;
}

// Adds `data` to the profile, once its bookmarks have loaded.
void Write(base::WeakPtr<Profile> profile,
           std::string browser,
           uint16_t items,
           Data data,
           base::OnceCallback<void(const Imported&)> done,
           int attempts_left = 100) {
  if (!profile) {
    return;
  }
  auto writer = base::MakeRefCounted<ProfileWriter>(profile.get());
  if ((items & user_data_importer::FAVORITES) &&
      !writer->BookmarkModelIsLoaded()) {
    if (attempts_left > 0) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&Write, profile, std::move(browser), items,
                         std::move(data), std::move(done), attempts_left - 1),
          base::Milliseconds(100));
      return;
    }
    items &= ~user_data_importer::FAVORITES;
  }

  Imported imported;
  if (items & user_data_importer::FAVORITES) {
    imported.bookmarks = 0;
    for (const BookmarkEntry& entry : data.bookmarks) {
      imported.bookmarks += !entry.is_folder;
    }
    if (!data.bookmarks.empty()) {
      std::u16string folder = L10n("import.folder");
      const size_t at = folder.find(u"$1");
      if (at != std::u16string::npos) {
        folder.replace(at, 2, base::UTF8ToUTF16(browser));
      }
      writer->AddBookmarks(data.bookmarks, folder);
    }
  }
  if (data.history) {
    imported.history = static_cast<int>(data.history->size());
    // History keeps no source of its own for browsers of Chromium's family:
    // any of the imported ones does.
    writer->AddHistoryPage(*data.history, history::SOURCE_FIREFOX_IMPORTED);
  }
  if (data.passwords) {
    imported.passwords = static_cast<int>(data.passwords->size());
    imported.passwords_unreadable = data.passwords_unreadable;
    for (const PasswordForm& form : *data.passwords) {
      writer->AddPasswordForm(form);
    }
  }
  std::move(done).Run(imported);
}

}  // namespace

void ImportFromChromium(Profile* profile,
                        const Source& source,
                        uint16_t items,
                        base::OnceCallback<void(const Imported&)> done) {
  items &= source.items;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&Read, source, items),
      base::BindOnce(
          [](base::WeakPtr<Profile> profile, std::string browser,
             uint16_t items, base::OnceCallback<void(const Imported&)> done,
             Data data) {
            Write(profile, std::move(browser), items, std::move(data),
                  std::move(done));
          },
          profile->GetWeakPtr(),
          source.profile.empty()
              ? source.browser
              : source.browser + " (" + source.profile + ")",
          items, std::move(done)));
}

}  // namespace oblada::import
