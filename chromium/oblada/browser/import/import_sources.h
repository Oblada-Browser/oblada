#ifndef OBLADA_BROWSER_IMPORT_IMPORT_SOURCES_H_
#define OBLADA_BROWSER_IMPORT_IMPORT_SOURCES_H_

#include <cstdint>
#include <string>
#include <vector>

#include "base/files/file_path.h"

// The other browsers on this computer that Oblada can import from:
// - those built on Chromium (Chrome, Arc, Brave, Edge…), whose profiles' files
//   Oblada reads itself (see chromium_importer.h);
// - Firefox, which Chromium's own importer reads;
// - on macOS, Safari, from the file it exports its data to (see
//   safari_import.h).
namespace oblada::import {

// For tests: looks for other browsers' data in this folder rather than in the
// user's own (as if it were all of them: Application Support, AppData, ~).
inline constexpr char kImportRootSwitch[] = "oblada-import-root";

enum class Family { kChromium, kFirefox, kSafari };

struct Source {
  Source();
  Source(const Source&);
  Source& operator=(const Source&);
  ~Source();

  // Names the source between the page and the browser ("chrome/Default").
  std::string id;
  // The browser's name ("Chrome"), and the profile's if it has several.
  std::string browser;
  std::string profile;
  Family family = Family::kChromium;
  // Identifies the app, for its icon (see app_icon.h): its bundle identifier
  // on macOS.
  std::string app_id;
  // The profile's folder (none for Safari).
  base::FilePath dir;
  // Chromium's family: the name its passwords' key is kept under (see
  // password_decryptor.h), as a service ("Chrome Safe Storage") and account
  // ("Chrome").
  std::string safe_storage;
  std::string safe_storage_account;
  // What it has to import: user_data_importer::ImportItem bits.
  uint16_t items = 0;
  // Whether it is the default browser: likely the one the user comes from.
  bool is_default = false;
};

// Looks for them. Blocks: it reads their files.
std::vector<Source> FindSources();

}  // namespace oblada::import

#endif  // OBLADA_BROWSER_IMPORT_IMPORT_SOURCES_H_
