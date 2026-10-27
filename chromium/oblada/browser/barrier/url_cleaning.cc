#include "oblada/browser/barrier/url_cleaning.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/containers/flat_set.h"
#include "base/json/json_reader.h"
#include "base/no_destructor.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "extensions/common/url_pattern.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/base/url_util.h"
#include "oblada/browser/barrier/data_lists.h"
#include "third_party/re2/src/re2/re2.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace oblada::barrier {

namespace {

std::vector<URLPattern> ParsePatterns(const base::ListValue* list) {
  std::vector<URLPattern> patterns;
  if (!list) {
    return patterns;
  }
  for (const base::Value& value : *list) {
    URLPattern pattern(URLPattern::SCHEME_HTTP | URLPattern::SCHEME_HTTPS);
    if (value.is_string() && pattern.Parse(value.GetString()) ==
                                 URLPattern::ParseResult::kSuccess) {
      patterns.push_back(std::move(pattern));
    }
  }
  return patterns;
}

bool MatchesAny(const std::vector<URLPattern>& patterns, const GURL& url) {
  for (const URLPattern& pattern : patterns) {
    if (pattern.MatchesURL(url)) {
      return true;
    }
  }
  return false;
}

bool SameSite(const GURL& a, const GURL& b) {
  return net::registry_controlled_domains::SameDomainOrHost(
      a, b, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
}

// --- Brave's query filter ---

struct QueryRule {
  std::vector<URLPattern> include;
  std::vector<URLPattern> exclude;
  base::flat_set<std::string> params;
};

std::vector<QueryRule> ParseQueryRules(std::string_view json) {
  std::vector<QueryRule> rules;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return rules;
  }
  for (const base::Value& value : parsed->GetList()) {
    const base::DictValue* dict = value.GetIfDict();
    if (!dict) {
      continue;
    }
    QueryRule rule;
    rule.include = ParsePatterns(dict->FindList("include"));
    rule.exclude = ParsePatterns(dict->FindList("exclude"));
    if (const base::ListValue* params = dict->FindList("params")) {
      for (const base::Value& param : *params) {
        if (param.is_string()) {
          rule.params.insert(param.GetString());
        }
      }
    }
    if (!rule.include.empty() && !rule.params.empty()) {
      rules.push_back(std::move(rule));
    }
  }
  return rules;
}

std::shared_ptr<const std::vector<QueryRule>> QueryRules() {
  static base::NoDestructor<
      data_lists::Parsed<std::vector<QueryRule>, &ParseQueryRules>>
      rules(data_lists::DataList::kQueryFilter);
  return rules->Get();
}

// Parameters Brave strips but on some pages, where they're needed
// (brave-core's components/query_filter/browser/utils.cc).
bool IsConditionallyKept(std::string_view name, const GURL& url) {
  const std::string_view spec = url.spec();
  if (name == "mkt_tok") {
    return RE2::PartialMatch(spec, R"([uU]nsubscribe|emailWebview)");
  }
  if (name == "ck_subscriber_id") {
    return url.path().find("/unsubscribe") != std::string_view::npos;
  }
  if (name == "h_sid" || name == "h_slt") {
    return url.path().find("/email/") != std::string_view::npos;
  }
  return false;
}

bool IsStripped(std::string_view name, const GURL& url) {
  static constexpr std::string_view kConditional[] = {
      "mkt_tok", "ck_subscriber_id", "h_sid", "h_slt"};
  for (std::string_view conditional : kConditional) {
    if (name == conditional) {
      return !IsConditionallyKept(name, url);
    }
  }
  const auto rules = QueryRules();
  for (const QueryRule& rule : *rules) {
    if (rule.params.contains(name) && MatchesAny(rule.include, url) &&
        !MatchesAny(rule.exclude, url)) {
      return true;
    }
  }
  return false;
}

// --- Brave's debouncing ---

struct DebounceRule {
  std::vector<URLPattern> include;
  std::vector<URLPattern> exclude;
  enum class Action { kRedirect, kBase64Redirect, kRegexPath, kRegexTemplate };
  Action action = Action::kRedirect;
  std::string param;
  std::string prepend_scheme;
  std::string template_url;
  std::unique_ptr<RE2> regex;
  // Only with AMP pages left for the original.
  bool de_amp = false;
};

std::vector<DebounceRule> ParseDebounceRules(std::string_view json) {
  std::vector<DebounceRule> rules;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return rules;
  }
  for (const base::Value& value : parsed->GetList()) {
    const base::DictValue* dict = value.GetIfDict();
    const std::string* action = dict ? dict->FindString("action") : nullptr;
    const std::string* param = dict ? dict->FindString("param") : nullptr;
    if (!action || !param) {
      continue;
    }
    DebounceRule rule;
    rule.include = ParsePatterns(dict->FindList("include"));
    rule.exclude = ParsePatterns(dict->FindList("exclude"));
    rule.param = *param;
    if (const std::string* scheme = dict->FindString("prepend_scheme")) {
      rule.prepend_scheme = *scheme;
    }
    if (const std::string* pref = dict->FindString("pref")) {
      // Brave's only one, for AMP pages; others aren't known here.
      if (*pref != "brave.de_amp.enabled") {
        continue;
      }
      rule.de_amp = true;
    }
    if (*action == "redirect") {
      rule.action = DebounceRule::Action::kRedirect;
    } else if (*action == "base64,redirect") {
      rule.action = DebounceRule::Action::kBase64Redirect;
    } else if (*action == "regex-path" || *action == "regex-path-template") {
      rule.action = *action == "regex-path"
                        ? DebounceRule::Action::kRegexPath
                        : DebounceRule::Action::kRegexTemplate;
      rule.regex = std::make_unique<RE2>(rule.param);
      if (!rule.regex->ok()) {
        continue;
      }
      if (const std::string* url = dict->FindString("redirect_url_template")) {
        rule.template_url = *url;
      }
    } else {
      continue;
    }
    if (!rule.include.empty()) {
      rules.push_back(std::move(rule));
    }
  }
  return rules;
}

std::shared_ptr<const std::vector<DebounceRule>> DebounceRules() {
  static base::NoDestructor<
      data_lists::Parsed<std::vector<DebounceRule>, &ParseDebounceRules>>
      rules(data_lists::DataList::kDebounce);
  return rules->Get();
}

std::optional<GURL> Apply(const DebounceRule& rule, const GURL& url) {
  std::string target;
  switch (rule.action) {
    case DebounceRule::Action::kRedirect:
    case DebounceRule::Action::kBase64Redirect: {
      std::string value;
      if (!net::GetValueForKeyInQuery(url, rule.param, &value)) {
        return std::nullopt;
      }
      if (rule.action == DebounceRule::Action::kBase64Redirect &&
          !base::Base64Decode(value, &value)) {
        return std::nullopt;
      }
      target = std::move(value);
      break;
    }
    case DebounceRule::Action::kRegexPath:
    case DebounceRule::Action::kRegexTemplate: {
      const int groups = rule.regex->NumberOfCapturingGroups();
      if (groups < 1) {
        return std::nullopt;
      }
      std::vector<std::string> captures(groups);
      std::vector<RE2::Arg> args;
      std::vector<const RE2::Arg*> arg_pointers;
      args.reserve(groups);
      for (std::string& capture : captures) {
        args.emplace_back(&capture);
      }
      for (const RE2::Arg& arg : args) {
        arg_pointers.push_back(&arg);
      }
      if (!RE2::FullMatchN(url.path(), *rule.regex, arg_pointers.data(),
                           groups)) {
        return std::nullopt;
      }
      if (rule.action == DebounceRule::Action::kRegexTemplate) {
        target = rule.template_url;
        for (int i = groups; i >= 1; --i) {
          base::ReplaceSubstringsAfterOffset(
              &target, 0, base::StrCat({"$", base::NumberToString(i)}),
              captures[i - 1]);
        }
      } else {
        target = base::StrCat(captures);
      }
      break;
    }
  }
  target = base::UnescapeURLComponent(
      target, base::UnescapeRule::URL_SPECIAL_CHARS_EXCEPT_PATH_SEPARATORS |
                  base::UnescapeRule::PATH_SEPARATORS);
  if (!rule.prepend_scheme.empty() && !GURL(target).has_scheme()) {
    target = base::StrCat({rule.prepend_scheme, "://", target});
  }
  GURL result(target);
  if (!result.SchemeIsHTTPOrHTTPS()) {
    return std::nullopt;
  }
  return result;
}

}  // namespace

std::optional<GURL> StripTrackingParameters(
    const GURL& url,
    const std::optional<url::Origin>& initiator) {
  if (!url.SchemeIsHTTPOrHTTPS() || !url.has_query()) {
    return std::nullopt;
  }
  if (initiator && !initiator->opaque() &&
      SameSite(initiator->GetURL(), url)) {
    return std::nullopt;
  }
  // Its links are signed, parameters included.
  if (url.DomainIs("urldefense.com")) {
    return std::nullopt;
  }
  std::vector<std::string_view> kept;
  bool stripped = false;
  for (std::string_view pair : base::SplitStringPiece(
           url.query(), "&", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
    const std::string name = base::UnescapeURLComponent(
        pair.substr(0, pair.find('=')),
        base::UnescapeRule::URL_SPECIAL_CHARS_EXCEPT_PATH_SEPARATORS);
    if (!name.empty() && IsStripped(name, url)) {
      stripped = true;
    } else {
      kept.push_back(pair);
    }
  }
  if (!stripped) {
    return std::nullopt;
  }
  GURL::Replacements replacements;
  const std::string query = base::JoinString(kept, "&");
  if (query.empty()) {
    replacements.ClearQuery();
  } else {
    replacements.SetQueryStr(query);
  }
  return url.ReplaceComponents(replacements);
}

std::optional<GURL> DebounceTarget(const GURL& url, bool de_amp) {
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return std::nullopt;
  }
  const auto rules = DebounceRules();
  for (const DebounceRule& rule : *rules) {
    if ((rule.de_amp && !de_amp) || !MatchesAny(rule.include, url) ||
        MatchesAny(rule.exclude, url)) {
      continue;
    }
    std::optional<GURL> target = Apply(rule, url);
    // Only to another site: a site's own redirects are its own business.
    if (target && !SameSite(*target, url)) {
      return target;
    }
  }
  return std::nullopt;
}

}  // namespace oblada::barrier
