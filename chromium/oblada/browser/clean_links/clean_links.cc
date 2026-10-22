#include "oblada/browser/clean_links/clean_links.h"

#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/check.h"
#include "base/json/json_reader.h"
#include "base/no_destructor.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "oblada/browser/clean_links/mozilla_list.h"
#include "oblada/clean_links_lists.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "url/gurl.h"

namespace oblada::clean_links {

namespace {

// The tracking parameters of Firefox's lists, by name in lowercase: on every
// site (with Mozilla's list's), on a host's pages, on a site's (a registrable
// domain, as "ebay.com" for www.ebay.com).
struct Rules {
  Names global;
  std::map<std::string, Names, std::less<>> by_host;
  std::map<std::string, Names, std::less<>> by_site;

  bool Tracks(std::string_view host,
              std::string_view site,
              std::string_view name) const {
    const auto in = [name](const auto& map, std::string_view key) {
      auto it = map.find(key);
      return it != map.end() && it->second.contains(name);
    };
    return global.contains(name) || MozillaListParams().contains(name) ||
           in(by_host, host) || in(by_site, site);
  }
};

// Adds a Firefox list's rules to `rules`: a site's in both lists has the
// parameters of both, as in Firefox.
void AddList(std::string_view json, Rules& rules) {
  std::optional<base::DictValue> list =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  CHECK(list);
  for (const auto [key, value] : *list) {
    const base::DictValue* rule = value.GetIfDict();
    const base::ListValue* params = rule ? rule->FindList("queryParams") : nullptr;
    if (!params) {
      continue;
    }
    Names names;
    for (const base::Value& param : *params) {
      if (param.is_string()) {
        names.insert(base::ToLowerASCII(param.GetString()));
      }
    }
    if (rule->FindBool("isGlobal").value_or(false)) {
      rules.global.insert(names.begin(), names.end());
    }
    for (auto [field, map] : {std::pair{"hosts", &rules.by_host},
                              std::pair{"schemelessSites", &rules.by_site}}) {
      if (const base::ListValue* places = rule->FindList(field)) {
        for (const base::Value& place : *places) {
          if (place.is_string()) {
            (*map)[base::ToLowerASCII(place.GetString())].insert(names.begin(),
                                                                 names.end());
          }
        }
      }
    }
  }
}

const Rules& GetRules() {
  static const base::NoDestructor<Rules> rules([] {
    Rules rules;
    AddList(kStripOnShareList, rules);
    AddList(kStripOnShareLgplList, rules);
    return rules;
  }());
  return *rules;
}

// `url` without its tracking parameters, and unless `nested`, without those of
// the links in its parameters' values (one level deep, as Firefox); adds how
// many it removed to `removed`. As Firefox, parameters are told by their name
// as written, in any case, and the others are kept as they were.
GURL Strip(const GURL& url, bool nested, int& removed) {
  if (!IsCleanable(url) || !url.has_query()) {
    return url;
  }
  const Rules& rules = GetRules();
  const std::string_view host = url.host();
  std::string site = net::registry_controlled_domains::GetDomainAndRegistry(
      url, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  if (site.empty()) {
    site = host;
  }
  const int removed_before = removed;
  std::vector<std::string> kept;
  for (std::string_view param :
       base::SplitStringPiece(url.query(), "&", base::KEEP_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    const size_t equals = param.find('=');
    const std::string_view name = param.substr(0, equals);
    if (rules.Tracks(host, site, base::ToLowerASCII(name))) {
      ++removed;
      continue;
    }
    if (!nested && equals != std::string_view::npos) {
      int removed_in_link = 0;
      const GURL link(base::UnescapeBinaryURLComponent(
          param.substr(equals + 1), base::UnescapeRule::REPLACE_PLUS_WITH_SPACE));
      const GURL clean_link = Strip(link, /*nested=*/true, removed_in_link);
      if (removed_in_link) {
        removed += removed_in_link;
        kept.push_back(base::StrCat(
            {name, "=",
             base::EscapeQueryParamValue(clean_link.spec(), /*use_plus=*/true)}));
        continue;
      }
    }
    kept.emplace_back(param);
  }
  if (removed == removed_before) {
    return url;
  }
  const std::string query = base::JoinString(kept, "&");
  GURL::Replacements replacements;
  if (query.empty()) {
    replacements.ClearQuery();
  } else {
    replacements.SetQueryStr(query);
  }
  return url.ReplaceComponents(replacements);
}

}  // namespace

bool IsCleanable(const GURL& url) {
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS();
}

bool HasTracking(const GURL& url) {
  int removed = 0;
  Strip(url, /*nested=*/false, removed);
  return removed > 0;
}

GURL Clean(const GURL& url) {
  int removed = 0;
  return Strip(url, /*nested=*/false, removed);
}

void CopyClean(const GURL& url, Profile* profile) {
  ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
  writer.WriteText(base::UTF8ToUTF16(Clean(url).spec()));
  if (profile && profile->IsOffTheRecord()) {
    writer.MarkAsOffTheRecord();
  }
}

}  // namespace oblada::clean_links
