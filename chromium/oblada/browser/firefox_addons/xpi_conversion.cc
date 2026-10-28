#include "oblada/browser/firefox_addons/xpi_conversion.h"

#include <algorithm>
#include <set>
#include <utility>

#include "base/containers/fixed_flat_map.h"
#include "base/containers/fixed_flat_set.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/json/string_escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "extensions/common/command.h"
#include "oblada/browser/firefox_addons/compat_script.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace oblada::firefox_addons {

namespace {

constexpr char kBackgroundWorkerName[] = "oblada-background.js";

// Firefox permissions under another name here.
constexpr auto kRenamedPermissions =
    base::MakeFixedFlatMap<std::string_view, std::string_view>({
        {"menus", "contextMenus"},
    });

// Firefox APIs Chromium doesn't have.
constexpr auto kFirefoxOnlyPermissions = base::MakeFixedFlatSet<std::string_view>({
    "browserSettings",
    "captivePortal",
    "dns",
    "find",
    "geckoProfiler",
    "menus.overrideContext",
    "mozillaAddons",
    "networkStatus",
    "normandyAddonStudy",
    "pkcs11",
    "tabHide",
    "telemetry",
    "theme",
    "urlbar",
    "webRequestFilterResponse",
    "webRequestFilterResponse.serviceWorkerScript",
});

// Firefox's containers API, which Oblada has (see
// //oblada/common/extensions/api/contextual_identities.json): not a permission here but
// a manifest key, which makes the API available.
constexpr char kContextualIdentitiesPermission[] = "contextualIdentities";
constexpr char kContextualIdentitiesKey[] = "contextual_identities";

// Manifest keys only Firefox understands; some would make Chromium refuse the add-on.
constexpr auto kFirefoxOnlyKeys = base::MakeFixedFlatSet<std::string_view>({
    "applications",
    "browser_specific_settings",
    "developer",
    "dictionaries",
    "protocol_handlers",
    "sidebar_action",
    "theme_experiment",
    "user_scripts",
});

// Chromium refuses add-ons with more keyboard shortcuts than this.
constexpr int kMaxShortcuts = 4;

// Keeps what Chromium accepts of the add-on's commands: their shortcuts in its syntax,
// at most kMaxShortcuts of them (the toolbar button's first). The others stay, without
// a shortcut; one can be given in oblada://extensions/shortcuts.
void ConvertCommands(base::DictValue& manifest, std::vector<std::string>& warnings) {
  base::DictValue* commands = manifest.FindDict("commands");
  if (!commands) {
    return;
  }
  commands->Remove("_execute_sidebar_action");
  if (!manifest.Find("page_action")) {
    commands->Remove("_execute_page_action");
  }
  std::vector<std::string> names;
  for (const auto [name, value] : *commands) {
    names.push_back(name);
  }
  std::ranges::stable_partition(
      names, [](const std::string& name) { return name.starts_with("_execute_"); });
  int shortcuts = 0;
  int index = 0;
  for (const std::string& name : names) {
    base::DictValue* command = commands->FindDict(name);
    std::u16string error;
    if (command && command->Find("suggested_key") &&
        (shortcuts == kMaxShortcuts ||
         !extensions::Command().Parse(*command, name, index, &error))) {
      command->Remove("suggested_key");
      warnings.push_back(base::StrCat({"Command \"", name, "\" has no shortcut"}));
    }
    extensions::Command parsed;
    if (!command || !parsed.Parse(*command, name, index, &error)) {
      commands->Remove(name);
      warnings.push_back(base::StrCat({"Command \"", name, "\" isn't supported"}));
      continue;
    }
    ++index;
    if (parsed.accelerator().key_code() != ui::VKEY_UNKNOWN &&
        !parsed.accelerator().IsMediaKey()) {
      ++shortcuts;
    }
  }
}

bool IsSvg(std::string_view path) {
  return base::EndsWith(path, ".svg", base::CompareCase::INSENSITIVE_ASCII);
}

// Drops SVG entries from an icon dictionary ({"16": "a.png", ...}).
void RemoveSvgIcons(base::DictValue& icons) {
  std::vector<std::string> svg;
  for (const auto [size, path] : icons) {
    if (!path.is_string() || IsSvg(path.GetString())) {
      svg.push_back(size);
    }
  }
  for (const std::string& size : svg) {
    icons.Remove(size);
  }
}

void RemoveSvgIconsFromKey(base::DictValue& dict, std::string_view key) {
  if (const std::string* path = dict.FindString(key); path && IsSvg(*path)) {
    dict.Remove(key);
  } else if (base::DictValue* icons = dict.FindDict(key)) {
    RemoveSvgIcons(*icons);
    if (icons->empty()) {
      dict.Remove(key);
    }
  }
}

void ConvertPermissions(base::DictValue& manifest,
                        std::string_view key,
                        std::vector<std::string>& warnings) {
  base::ListValue* list = manifest.FindList(key);
  if (!list) {
    return;
  }
  base::ListValue converted;
  bool contextual_identities = false;
  for (const base::Value& permission : *list) {
    if (!permission.is_string()) {
      continue;
    }
    std::string_view name = permission.GetString();
    if (name == kContextualIdentitiesPermission) {
      contextual_identities = true;
      continue;
    }
    if (kFirefoxOnlyPermissions.contains(name)) {
      warnings.push_back(base::StrCat({"Uses the Firefox-only \"", name, "\" API"}));
      continue;
    }
    if (auto renamed = kRenamedPermissions.find(name);
        renamed != kRenamedPermissions.end()) {
      name = renamed->second;
    }
    if (!converted.contains(name)) {
      converted.Append(name);
    }
  }
  *list = std::move(converted);
  if (contextual_identities) {
    manifest.Set(kContextualIdentitiesKey, true);  // After the last use of `list`.
  }
}

// Adds kCompatScriptName in front of a list of scripts.
void PrependCompatScript(base::ListValue& scripts) {
  if (!scripts.contains(kCompatScriptName)) {
    scripts.Insert(scripts.begin(), base::Value(kCompatScriptName));
  }
}

// browser_specific_settings.gecko.<name> (or the older applications.gecko.<name>).
std::string GeckoSetting(const base::DictValue& manifest, std::string_view name) {
  for (const char* key : {"browser_specific_settings", "applications"}) {
    if (const std::string* value = manifest.FindStringByDottedPath(
            base::StrCat({key, ".gecko.", name}))) {
      return *value;
    }
  }
  return std::string();
}

// Chromium only allows [A-Za-z0-9_] in the names of an extension's messages; Firefox
// add-ons also use e.g. "-". The compat script renames them likewise in i18n calls.
std::string MessageName(std::string_view name) {
  std::string result(name);
  for (char& c : result) {
    if (!base::IsAsciiAlphaNumeric(c) && c != '_') {
      c = '_';
    }
  }
  return result;
}

// Renames the messages of every locale (see MessageName).
bool ConvertMessages(const base::FilePath& dir) {
  base::FileEnumerator locales(dir.AppendASCII("_locales"), /*recursive=*/false,
                               base::FileEnumerator::DIRECTORIES);
  for (base::FilePath locale = locales.Next(); !locale.empty();
       locale = locales.Next()) {
    const base::FilePath file = locale.AppendASCII("messages.json");
    std::string text;
    if (!base::ReadFileToString(file, &text)) {
      continue;
    }
    std::optional<base::DictValue> messages =
        base::JSONReader::ReadDict(text, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
    if (!messages) {
      continue;
    }
    base::DictValue renamed;
    for (auto [name, message] : *messages) {
      const std::string new_name = MessageName(name);
      if (!renamed.Find(new_name)) {
        renamed.Set(new_name, std::move(message));
      }
    }
    std::string converted;
    if (!base::JSONWriter::Write(renamed, &converted) ||
        !base::WriteFile(file, converted)) {
      return false;
    }
  }
  return true;
}

// The (lowercase) names of the messages of the add-on's default locale.
std::set<std::string> DefaultMessageNames(const base::FilePath& dir,
                                          const base::DictValue& manifest) {
  std::set<std::string> names;
  const std::string* locale = manifest.FindString("default_locale");
  std::string text;
  if (!locale || !base::ReadFileToString(dir.AppendASCII("_locales")
                                             .AppendASCII(*locale)
                                             .AppendASCII("messages.json"),
                                         &text)) {
    return names;
  }
  if (std::optional<base::DictValue> messages = base::JSONReader::ReadDict(text, base::JSON_PARSE_CHROMIUM_EXTENSIONS)) {
    for (const auto [name, message] : *messages) {
      names.insert(base::ToLowerASCII(name));
    }
  }
  return names;
}

// The manifest's __MSG_<name>__ references, renamed likewise. Firefox leaves those to
// messages it doesn't have alone; Chromium refuses them, so they become their name.
void ConvertMessageReferences(std::string& manifest,
                              const std::set<std::string>& defined) {
  constexpr std::string_view kStart = "__MSG_";
  for (size_t start = manifest.find(kStart); start != std::string::npos;) {
    const size_t name = start + kStart.size();
    const size_t end = manifest.find("__", name);
    if (end == std::string::npos) {
      break;
    }
    const std::string renamed =
        MessageName(std::string_view(manifest).substr(name, end - name));
    if (defined.contains(base::ToLowerASCII(renamed)) || renamed.starts_with("@@")) {
      manifest.replace(name, end - name, renamed);
      start = manifest.find(kStart, name + renamed.size() + 2);
    } else {
      manifest.replace(start, end + 2 - start, renamed);
      start = manifest.find(kStart, start + renamed.size());
    }
  }
}

// Loads kCompatScriptName first in every HTML page of the add-on (popups, options,
// background pages).
bool AddCompatScriptToPages(const base::FilePath& dir) {
  constexpr char kTag[] = "<script src=\"/oblada-compat.js\"></script>";
  // In every folder, not only those matching the pattern.
  base::FileEnumerator pages(dir, /*recursive=*/true, base::FileEnumerator::FILES,
                             FILE_PATH_LITERAL("*.htm*"),
                             base::FileEnumerator::FolderSearchPolicy::ALL);
  for (base::FilePath page = pages.Next(); !page.empty(); page = pages.Next()) {
    std::string html;
    if (!base::ReadFileToString(page, &html)) {
      return false;
    }
    if (html.find(kTag) != std::string::npos) {
      continue;
    }
    // After <head ...> (or <html ...>) if there is one, else at the very start.
    const std::string lower = base::ToLowerASCII(html);
    size_t at = 0;
    for (const char* element : {"<head", "<html"}) {
      if (size_t start = lower.find(element); start != std::string::npos) {
        if (size_t end = lower.find('>', start); end != std::string::npos) {
          at = end + 1;
          break;
        }
      }
    }
    html.insert(at, kTag);
    if (!base::WriteFile(page, html)) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool ShouldUnpack(const base::FilePath& path) {
  // Signatures (META-INF) are Mozilla's; files starting with "_" are reserved by
  // Chromium, except for translations.
  const std::vector<base::FilePath::StringType> parts = path.GetComponents();
  if (parts.empty()) {
    return false;
  }
  const std::string first = base::FilePath(parts[0]).AsUTF8Unsafe();
  return first != "META-INF" && (!first.starts_with('_') || first == "_locales");
}

base::expected<ConvertedAddon, std::string> Convert(const base::FilePath& dir,
                                                    std::string_view fallback_id) {
  std::string text;
  if (!base::ReadFileToString(dir.AppendASCII("manifest.json"), &text)) {
    return base::unexpected("This isn't a Firefox add-on (no manifest.json)");
  }
  // Firefox allows comments in manifest.json.
  std::optional<base::DictValue> manifest =
      base::JSONReader::ReadDict(text, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!manifest) {
    return base::unexpected("The add-on's manifest.json is invalid");
  }
  const std::optional<int> version = manifest->FindInt("manifest_version");
  if (!version || (*version != 2 && *version != 3) || !manifest->FindString("name")) {
    return base::unexpected("The add-on's manifest.json is invalid");
  }
  if (manifest->Find("theme")) {
    return base::unexpected("Firefox themes aren't supported");
  }
  if (manifest->Find("dictionaries")) {
    return base::unexpected("Firefox dictionaries aren't supported");
  }

  ConvertedAddon addon;
  addon.gecko_id = GeckoSetting(*manifest, "id");
  if (addon.gecko_id.empty()) {
    addon.gecko_id = std::string(fallback_id);
  }
  addon.update_url = GeckoSetting(*manifest, "update_url");
  // Tells Oblada it is a Firefox add-on (see cookie_store_ids.h).
  manifest->Set("firefox_addon", true);

  ConvertPermissions(*manifest, "permissions", addon.warnings);
  ConvertPermissions(*manifest, "optional_permissions", addon.warnings);
  if (manifest->Find("sidebar_action")) {
    addon.warnings.push_back("Its sidebar isn't supported yet");
  }
  for (std::string_view key : kFirefoxOnlyKeys) {
    manifest->Remove(key);
  }
  // Chromium has one toolbar button per extension: the add-on's page action (an icon
  // in Firefox's address bar) goes when it has a toolbar button too.
  if ((manifest->Find("browser_action") || manifest->Find("action")) &&
      manifest->Find("page_action")) {
    manifest->Remove("page_action");
    addon.warnings.push_back("Its address bar button isn't supported");
  }
  ConvertCommands(*manifest, addon.warnings);

  RemoveSvgIconsFromKey(*manifest, "icons");
  for (const char* action : {"action", "browser_action", "page_action"}) {
    if (base::DictValue* dict = manifest->FindDict(action)) {
      RemoveSvgIconsFromKey(*dict, "default_icon");
      dict->Remove("theme_icons");
    }
  }

  if (base::ListValue* content_scripts = manifest->FindList("content_scripts")) {
    for (base::Value& entry : *content_scripts) {
      base::DictValue* script = entry.GetIfDict();
      base::ListValue* js = script ? script->FindList("js") : nullptr;
      const std::string* world = script ? script->FindString("world") : nullptr;
      if (js && !js->empty() && !(world && *world == "MAIN")) {
        PrependCompatScript(*js);
      }
    }
  }

  if (base::DictValue* background = manifest->FindDict("background")) {
    base::ListValue* scripts = background->FindList("scripts");
    if (*version == 3 && scripts && !background->Find("service_worker")) {
      // Firefox keeps background pages in Manifest V3; Chromium needs a worker.
      std::string imports = base::StrCat(
          {"importScripts(", base::GetQuotedJSONString(std::string("/") +
                                                       kCompatScriptName)});
      for (const base::Value& script : *scripts) {
        if (script.is_string()) {
          std::string path(script.GetString());
          imports += ", " + base::GetQuotedJSONString(
                                "/" + std::string(base::TrimString(path, "/",
                                                                   base::TRIM_LEADING)));
        }
      }
      imports += ");\n";
      if (!base::WriteFile(dir.AppendASCII(kBackgroundWorkerName), imports)) {
        return base::unexpected("Couldn't write the add-on's files");
      }
      *background = base::DictValue().Set("service_worker", kBackgroundWorkerName);
      addon.warnings.push_back("Its background page runs as a service worker");
    } else if (scripts) {
      PrependCompatScript(*scripts);
    }
  }

  std::string converted;
  if (!base::WriteFile(dir.AppendASCII(kCompatScriptName), kCompatScript) ||
      !base::JSONWriter::WriteWithOptions(
          *manifest, base::JSONWriter::OPTIONS_PRETTY_PRINT, &converted)) {
    return base::unexpected("Couldn't write the add-on's files");
  }
  if (!ConvertMessages(dir)) {
    return base::unexpected("Couldn't write the add-on's files");
  }
  ConvertMessageReferences(converted, DefaultMessageNames(dir, *manifest));
  if (!base::WriteFile(dir.AppendASCII("manifest.json"), converted) ||
      !base::WriteFile(dir.AppendASCII("oblada-firefox-manifest.json"), text) ||
      !AddCompatScriptToPages(dir)) {
    return base::unexpected("Couldn't write the add-on's files");
  }
  return addon;
}

}  // namespace oblada::firefox_addons
