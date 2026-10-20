#include "oblada/browser/import/import_sources.h"

#include <algorithm>
#include <map>
#include <set>
#include <string_view>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/shell_integration.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_LINUX)
#include "base/environment.h"
#include "base/nix/xdg_util.h"
#endif

namespace oblada::import {

namespace {

using user_data_importer::AUTOFILL_FORM_DATA;
using user_data_importer::FAVORITES;
using user_data_importer::HISTORY;
using user_data_importer::PASSWORDS;

// Where a browser keeps its data, by platform.
enum class Root {
  kAppData,         // macOS: ~/Library/Application Support.
  kLocalAppData,    // Windows: %LOCALAPPDATA%.
  kRoamingAppData,  // Windows: %APPDATA%.
  kConfig,          // Linux: $XDG_CONFIG_HOME (~/.config).
  kHome,            // ~.
};

base::FilePath RootPath(Root root) {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch(kImportRootSwitch)) {
    return command_line.GetSwitchValuePath(kImportRootSwitch);
  }
  base::FilePath path;
  switch (root) {
#if BUILDFLAG(IS_MAC)
    case Root::kAppData:
      base::PathService::Get(base::DIR_APP_DATA, &path);
      break;
#endif
#if BUILDFLAG(IS_WIN)
    case Root::kLocalAppData:
      base::PathService::Get(base::DIR_LOCAL_APP_DATA, &path);
      break;
    case Root::kRoamingAppData:
      base::PathService::Get(base::DIR_ROAMING_APP_DATA, &path);
      break;
#endif
#if BUILDFLAG(IS_LINUX)
    case Root::kConfig: {
      std::unique_ptr<base::Environment> env = base::Environment::Create();
      path = base::nix::GetXDGDirectory(env.get(),
                                        base::nix::kXdgConfigHomeEnvVar,
                                        base::nix::kDotConfigDir);
      break;
    }
#endif
    case Root::kHome:
      path = base::GetHomeDir();
      break;
    default:
      break;
  }
  return path;
}

struct Browser {
  const char* name;
  Family family;
  Root root;
  // The user data folder (Chromium) or the folder of profiles.ini (Firefox).
  const base::FilePath::CharType* dir;
  const char* app_id;
  // Chromium's family: where the passwords' key is kept.
  const char* safe_storage;
  const char* safe_storage_account;
};

// The browsers looked for, in the order they are shown.
constexpr Browser kBrowsers[] = {
#if BUILDFLAG(IS_MAC)
    {"Chrome", Family::kChromium, Root::kAppData,
     FILE_PATH_LITERAL("Google/Chrome"), "com.google.Chrome",
     "Chrome Safe Storage", "Chrome"},
    {"Arc", Family::kChromium, Root::kAppData,
     FILE_PATH_LITERAL("Arc/User Data"), "company.thebrowser.Browser",
     "Arc Safe Storage", "Arc"},
    {"Brave", Family::kChromium, Root::kAppData,
     FILE_PATH_LITERAL("BraveSoftware/Brave-Browser"), "com.brave.Browser",
     "Brave Safe Storage", "Brave"},
    {"Edge", Family::kChromium, Root::kAppData,
     FILE_PATH_LITERAL("Microsoft Edge"), "com.microsoft.edgemac",
     "Microsoft Edge Safe Storage", "Microsoft Edge"},
    {"Vivaldi", Family::kChromium, Root::kAppData, FILE_PATH_LITERAL("Vivaldi"),
     "com.vivaldi.Vivaldi", "Vivaldi Safe Storage", "Vivaldi"},
    {"Opera", Family::kChromium, Root::kAppData,
     FILE_PATH_LITERAL("com.operasoftware.Opera"), "com.operasoftware.Opera",
     "Opera Safe Storage", "Opera"},
    {"Chromium", Family::kChromium, Root::kAppData,
     FILE_PATH_LITERAL("Chromium"), "org.chromium.Chromium",
     "Chromium Safe Storage", "Chromium"},
    {"Firefox", Family::kFirefox, Root::kAppData, FILE_PATH_LITERAL("Firefox"),
     "org.mozilla.firefox", "", ""},
#elif BUILDFLAG(IS_WIN)
    {"Chrome", Family::kChromium, Root::kLocalAppData,
     FILE_PATH_LITERAL("Google\\Chrome\\User Data"), "chrome", "", ""},
    {"Brave", Family::kChromium, Root::kLocalAppData,
     FILE_PATH_LITERAL("BraveSoftware\\Brave-Browser\\User Data"), "brave", "",
     ""},
    {"Edge", Family::kChromium, Root::kLocalAppData,
     FILE_PATH_LITERAL("Microsoft\\Edge\\User Data"), "msedge", "", ""},
    {"Vivaldi", Family::kChromium, Root::kLocalAppData,
     FILE_PATH_LITERAL("Vivaldi\\User Data"), "vivaldi", "", ""},
    {"Opera", Family::kChromium, Root::kRoamingAppData,
     FILE_PATH_LITERAL("Opera Software\\Opera Stable"), "opera", "", ""},
    {"Chromium", Family::kChromium, Root::kLocalAppData,
     FILE_PATH_LITERAL("Chromium\\User Data"), "chromium", "", ""},
    {"Firefox", Family::kFirefox, Root::kRoamingAppData,
     FILE_PATH_LITERAL("Mozilla\\Firefox"), "firefox", "", ""},
#elif BUILDFLAG(IS_LINUX)
    {"Chrome", Family::kChromium, Root::kConfig,
     FILE_PATH_LITERAL("google-chrome"), "google-chrome", "Chrome Safe Storage",
     "chrome"},
    {"Brave", Family::kChromium, Root::kConfig,
     FILE_PATH_LITERAL("BraveSoftware/Brave-Browser"), "brave-browser",
     "Brave Safe Storage", "brave"},
    {"Edge", Family::kChromium, Root::kConfig,
     FILE_PATH_LITERAL("microsoft-edge"), "microsoft-edge",
     "Microsoft Edge Safe Storage", "microsoft-edge"},
    {"Vivaldi", Family::kChromium, Root::kConfig, FILE_PATH_LITERAL("vivaldi"),
     "vivaldi-stable", "Vivaldi Safe Storage", "vivaldi"},
    {"Opera", Family::kChromium, Root::kConfig, FILE_PATH_LITERAL("opera"),
     "opera", "Opera Safe Storage", "opera"},
    {"Chromium", Family::kChromium, Root::kConfig, FILE_PATH_LITERAL("chromium"),
     "chromium", "Chromium Safe Storage", "chromium"},
    {"Firefox", Family::kFirefox, Root::kHome,
     FILE_PATH_LITERAL(".mozilla/firefox"), "firefox", "", ""},
    {"Firefox", Family::kFirefox, Root::kHome,
     FILE_PATH_LITERAL("snap/firefox/common/.mozilla/firefox"), "firefox", "",
     ""},
    {"Firefox", Family::kFirefox, Root::kHome,
     FILE_PATH_LITERAL(".var/app/org.mozilla.firefox/.mozilla/firefox"),
     "firefox", "", ""},
#endif
};

std::string LowerName(const Browser& browser) {
  return base::ToLowerASCII(browser.name);
}

uint16_t ChromiumItems(const base::FilePath& profile) {
  uint16_t items = 0;
  if (base::PathExists(profile.Append(FILE_PATH_LITERAL("Bookmarks")))) {
    items |= FAVORITES;
  }
  if (base::PathExists(profile.Append(FILE_PATH_LITERAL("History")))) {
    items |= HISTORY;
  }
  if (base::PathExists(profile.Append(FILE_PATH_LITERAL("Login Data")))) {
    items |= PASSWORDS;
  }
  return items;
}

// A browser of Chromium's family: a source for each of its profiles, as listed
// in its Local State (or the folder itself, for those with only one, as Opera).
void FindChromiumProfiles(const Browser& browser,
                          const base::FilePath& user_data,
                          std::vector<Source>* sources) {
  auto add = [&](const base::FilePath& dir, const std::string& name) {
    const uint16_t items = ChromiumItems(dir);
    if (!items) {
      return;
    }
    Source source;
    source.id = LowerName(browser) + "/" + dir.BaseName().AsUTF8Unsafe();
    source.browser = browser.name;
    source.profile = name;
    source.family = Family::kChromium;
    source.app_id = browser.app_id;
    source.dir = dir;
    source.safe_storage = browser.safe_storage;
    source.safe_storage_account = browser.safe_storage_account;
    source.items = items;
    sources->push_back(std::move(source));
  };

  if (ChromiumItems(user_data)) {
    add(user_data, std::string());
    return;
  }
  std::string local_state;
  std::optional<base::DictValue> state;
  if (base::ReadFileToString(user_data.Append(FILE_PATH_LITERAL("Local State")),
                             &local_state)) {
    state = base::JSONReader::ReadDict(local_state, base::JSON_PARSE_RFC);
  }
  const base::DictValue* profiles =
      state ? state->FindDictByDottedPath("profile.info_cache") : nullptr;
  if (!profiles) {
    add(user_data.Append(FILE_PATH_LITERAL("Default")), std::string());
    return;
  }
  // In the browser's own order, where it has one.
  std::vector<std::string> order;
  if (const base::ListValue* list =
          state->FindListByDottedPath("profile.profiles_order")) {
    for (const base::Value& dir : *list) {
      if (dir.is_string() && profiles->Find(dir.GetString())) {
        order.push_back(dir.GetString());
      }
    }
  }
  for (const auto [dir, info] : *profiles) {
    if (!std::ranges::contains(order, dir)) {
      order.push_back(dir);
    }
  }
  const size_t before = sources->size();
  for (const std::string& dir : order) {
    const base::DictValue* info = profiles->FindDict(dir);
    const std::string* name = info ? info->FindString("name") : nullptr;
    add(user_data.Append(base::FilePath::FromUTF8Unsafe(dir)),
        name ? *name : dir);
  }
  // A profile's name is only worth showing when there are several.
  if (sources->size() == before + 1) {
    sources->back().profile.clear();
  }
}

// profiles.ini's sections, as name → (key → value).
using Ini = std::map<std::string, std::map<std::string, std::string>>;

Ini ParseIni(const std::string& text) {
  Ini ini;
  std::string section;
  for (std::string_view line : base::SplitStringPiece(
           text, "\r\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (line.starts_with('[') && line.ends_with(']')) {
      section = std::string(line.substr(1, line.size() - 2));
      continue;
    }
    const size_t equals = line.find('=');
    if (equals != std::string_view::npos && !section.empty()) {
      ini[section][std::string(line.substr(0, equals))] =
          std::string(line.substr(equals + 1));
    }
  }
  return ini;
}

// A browser of Firefox's family: the profiles it uses (its installs' default
// ones, or the one marked as default), as listed in its profiles.ini.
void FindFirefoxProfiles(const Browser& browser,
                         const base::FilePath& root,
                         std::vector<Source>* sources) {
  std::string text;
  if (!base::ReadFileToString(root.Append(FILE_PATH_LITERAL("profiles.ini")),
                              &text)) {
    return;
  }
  const Ini ini = ParseIni(text);
  std::set<std::string> paths;
  for (const auto& [section, values] : ini) {
    if (section.starts_with("Install") && values.contains("Default")) {
      paths.insert(values.at("Default"));
    }
  }
  if (paths.empty()) {
    for (const auto& [section, values] : ini) {
      if (section.starts_with("Profile") && values.contains("Path") &&
          values.contains("Default") && values.at("Default") == "1") {
        paths.insert(values.at("Path"));
      }
    }
  }
  const size_t before = sources->size();
  for (const auto& [section, values] : ini) {
    if (!section.starts_with("Profile") || !values.contains("Path") ||
        !paths.contains(values.at("Path"))) {
      continue;
    }
    const std::string& path = values.at("Path");
    const bool relative =
        !values.contains("IsRelative") || values.at("IsRelative") != "0";
    const base::FilePath dir =
        relative ? root.Append(base::FilePath::FromUTF8Unsafe(path))
                 : base::FilePath::FromUTF8Unsafe(path);
    if (!base::PathExists(dir.Append(FILE_PATH_LITERAL("places.sqlite")))) {
      continue;
    }
    Source source;
    source.id = LowerName(browser) + "/" + dir.BaseName().AsUTF8Unsafe();
    source.browser = browser.name;
    source.profile = values.contains("Name") ? values.at("Name") : path;
    source.family = Family::kFirefox;
    source.app_id = browser.app_id;
    source.dir = dir;
    source.items = FAVORITES | HISTORY | AUTOFILL_FORM_DATA;
#if !BUILDFLAG(IS_MAC)
    // Read with Firefox's own libraries, which macOS's code signing doesn't
    // let another app load.
    source.items |= PASSWORDS;
#endif
    sources->push_back(std::move(source));
  }
  if (sources->size() == before + 1) {
    sources->back().profile.clear();
  }
}

}  // namespace

Source::Source() = default;
Source::Source(const Source&) = default;
Source& Source::operator=(const Source&) = default;
Source::~Source() = default;

std::vector<Source> FindSources() {
  base::ScopedBlockingCall blocking(FROM_HERE, base::BlockingType::MAY_BLOCK);
  std::vector<Source> sources;
  std::set<base::FilePath> seen;
  for (const Browser& browser : kBrowsers) {
    const base::FilePath root = RootPath(browser.root);
    if (root.empty()) {
      continue;
    }
    const base::FilePath dir = root.Append(browser.dir);
    if (!seen.insert(dir).second || !base::DirectoryExists(dir)) {
      continue;
    }
    if (browser.family == Family::kChromium) {
      FindChromiumProfiles(browser, dir, &sources);
    } else {
      FindFirefoxProfiles(browser, dir, &sources);
    }
  }
#if BUILDFLAG(IS_MAC)
  // Always there; its data comes from the file it exports it to.
  Source safari;
  safari.id = "safari";
  safari.browser = "Safari";
  safari.family = Family::kSafari;
  safari.app_id = "com.apple.Safari";
  safari.items = FAVORITES | HISTORY | PASSWORDS;
  sources.push_back(std::move(safari));
#endif

  // The default browser, by its name ("Google Chrome", "Arc"…).
  const std::string default_browser = base::ToLowerASCII(base::UTF16ToUTF8(
      shell_integration::GetApplicationNameForScheme(
          GURL("https://example.com/"))));
  if (!default_browser.empty()) {
    for (Source& source : sources) {
      if (default_browser.find(base::ToLowerASCII(source.browser)) !=
          std::string::npos) {
        source.is_default = true;
      }
    }
  }
  return sources;
}

}  // namespace oblada::import
