#include "oblada/browser/import/firefox_containers.h"

#include <algorithm>
#include <optional>
#include <string_view>

#include "base/files/file_util.h"
#include "base/i18n/case_conversion.h"
#include "base/json/json_reader.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/values.h"
#include "oblada/browser/containers/containers.h"
#include "oblada/browser/l10n.h"

namespace oblada::import {

namespace {

// Firefox's own containers, by the id of their name's string.
struct Default {
  std::string_view l10n_id;
  std::string_view id;
};
constexpr Default kDefaults[] = {
    {"userContextPersonal.label", "personal"},
    {"userContextWork.label", "work"},
    {"userContextBanking.label", "banking"},
    {"userContextShopping.label", "shopping"},
};

bool SameName(std::string_view a, std::string_view b) {
  return base::i18n::FoldCase(base::UTF8ToUTF16(a)) ==
         base::i18n::FoldCase(base::UTF8ToUTF16(b));
}

}  // namespace

std::vector<FirefoxContainer> ReadFirefoxContainers(
    const base::FilePath& profile_dir) {
  base::ScopedBlockingCall blocking(FROM_HERE, base::BlockingType::MAY_BLOCK);
  std::vector<FirefoxContainer> found;
  std::string text;
  if (!base::ReadFileToString(
          profile_dir.Append(FILE_PATH_LITERAL("containers.json")), &text)) {
    return found;
  }
  std::optional<base::DictValue> file =
      base::JSONReader::ReadDict(text, base::JSON_PARSE_RFC);
  const base::ListValue* identities =
      file ? file->FindList("identities") : nullptr;
  if (!identities) {
    return found;
  }
  for (const base::Value& value : *identities) {
    const base::DictValue* identity = value.GetIfDict();
    // Firefox's own (for thumbnails, add-ons' storage…) aren't public.
    if (!identity || !identity->FindBool("public").value_or(false)) {
      continue;
    }
    FirefoxContainer container;
    if (const std::string* name = identity->FindString("name")) {
      container.name = *name;
    } else if (const std::string* l10n_id = identity->FindString("l10nID")) {
      for (const Default& d : kDefaults) {
        if (d.l10n_id == *l10n_id) {
          container.default_id = d.id;
        }
      }
    }
    if (container.name.empty() && container.default_id.empty()) {
      continue;
    }
    const std::string* color = identity->FindString("color");
    const std::string* icon = identity->FindString("icon");
    container.color = color ? *color : std::string();
    container.icon = icon ? *icon : std::string();
    found.push_back(std::move(container));
  }
  return found;
}

int AddFirefoxContainers(Profile* profile,
                         const std::vector<FirefoxContainer>& firefox_ones) {
  int count = 0;
  for (const FirefoxContainer& firefox : firefox_ones) {
    const std::vector<containers::Container> existing =
        containers::GetContainers(profile);
    std::optional<containers::Container> match;
    for (const containers::Container& container : existing) {
      if (!container.temporary &&
          ((!firefox.default_id.empty() && container.id == firefox.default_id) ||
           (!firefox.name.empty() && SameName(container.name, firefox.name)))) {
        match = container;
        break;
      }
    }
    // Firefox's colors are Oblada's; its icons are among Oblada's names.
    std::string color = firefox.color;
    if (!std::ranges::contains(containers::ColorNames(), color)) {
      color = std::string(containers::ColorNames().front());
    }
    const std::string icon = firefox.icon.empty() ? "circle" : firefox.icon;
    if (match) {
      match->color = color;
      match->icon = icon;
      containers::UpdateContainer(profile, *match);
    } else {
      const std::string name =
          firefox.name.empty()
              ? L10nUTF8("import.container_" + firefox.default_id)
              : firefox.name;
      containers::AddContainer(profile, name, color, icon);
    }
    ++count;
  }
  return count;
}

}  // namespace oblada::import
