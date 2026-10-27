#include "oblada/browser/barrier/sensitive_sites.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/no_destructor.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "extensions/common/url_pattern.h"
#include "net/base/url_util.h"
#include "oblada/browser/barrier/barrier.h"
#include "oblada/browser/barrier/data_lists.h"
#include "oblada/browser/barrier/domain_block.h"
#include "oblada/browser/containers/containers.h"
#include "oblada/browser/l10n.h"

namespace oblada::barrier {

namespace {

// The page's answer: the session's token, then ".here" or ".temporary".
constexpr char kAnswerParam[] = "oblada-barrier-sensitive";

struct SensitiveRule {
  std::vector<URLPattern> include;
  std::vector<URLPattern> exclude;
};

std::vector<URLPattern> Patterns(const base::ListValue* list) {
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

std::vector<SensitiveRule> ParseRules(std::string_view json) {
  std::vector<SensitiveRule> rules;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (parsed && parsed->is_list()) {
    for (const base::Value& value : parsed->GetList()) {
      if (const base::DictValue* dict = value.GetIfDict()) {
        rules.push_back({Patterns(dict->FindList("include")),
                         Patterns(dict->FindList("exclude"))});
      }
    }
  }
  return rules;
}

std::shared_ptr<const std::vector<SensitiveRule>> Rules() {
  static base::NoDestructor<
      data_lists::Parsed<std::vector<SensitiveRule>, &ParseRules>>
      rules(data_lists::DataList::kSensitiveSites);
  return rules->Get();
}

bool IsSensitive(const GURL& url) {
  const auto rules = Rules();
  for (const SensitiveRule& rule : *rules) {
    auto matches = [&](const std::vector<URLPattern>& patterns) {
      for (const URLPattern& pattern : patterns) {
        if (pattern.MatchesURL(url)) {
          return true;
        }
      }
      return false;
    };
    if (matches(rule.include) && !matches(rule.exclude)) {
      return true;
    }
  }
  return false;
}

// Sites the user chose to open here, this session.
std::set<std::string>& OpenedHere() {
  static base::NoDestructor<std::set<std::string>> sites;
  return *sites;
}

bool InTemporaryContainer(content::WebContents* contents) {
  const std::string id = containers::GetContainerId(contents);
  if (id.empty()) {
    return false;
  }
  std::optional<containers::Container> container = containers::FindContainer(
      Profile::FromBrowserContext(contents->GetBrowserContext()), id);
  return container && container->temporary;
}

// Reopens `url` in a new temporary container, in place of `contents`' tab.
void ReopenInTemporaryContainer(content::WebContents* contents,
                                const GURL& url) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<content::WebContents> contents, GURL url) {
                       if (!contents) {
                         return;
                       }
                       const containers::Container container =
                           containers::AddTemporaryContainer(
                               Profile::FromBrowserContext(
                                   contents->GetBrowserContext()));
                       containers::ReopenInContainer(contents.get(),
                                                     container.id, url,
                                                     /*keep_original=*/false);
                     },
                     contents->GetWeakPtr(), url));
}

}  // namespace

SensitiveSiteDecision CheckSensitiveSite(content::WebContents* contents,
                                         const GURL& url) {
  SensitiveSiteDecision decision;
  if (!contents || !IsOn()) {
    return decision;
  }
  // The question's answer.
  std::string answer;
  if (net::GetValueForKeyInQuery(url, kAnswerParam, &answer) &&
      !SessionToken().empty() && answer.starts_with(SessionToken())) {
    const GURL original = WithoutQueryParameter(url, kAnswerParam);
    if (answer == SessionToken() + ".temporary") {
      ReopenInTemporaryContainer(contents, original);
      decision.action = SensitiveSiteDecision::Action::kStop;
    } else {
      OpenedHere().insert(SiteOf(original));
      decision.action = SensitiveSiteDecision::Action::kGo;
      decision.go_to = original;
    }
    return decision;
  }
  Profile* profile = Profile::FromBrowserContext(contents->GetBrowserContext());
  const std::string choice =
      profile->GetPrefs()->GetString(kSensitiveSitesPref);
  if (choice == "never" || profile->IsOffTheRecord() ||
      InTemporaryContainer(contents) || OpenedHere().contains(SiteOf(url)) ||
      !IsSensitive(url)) {
    return decision;
  }
  if (choice == "always") {
    ReopenInTemporaryContainer(contents, url);
    decision.action = SensitiveSiteDecision::Action::kStop;
  } else {
    decision.action = SensitiveSiteDecision::Action::kAsk;
  }
  return decision;
}

std::string SensitiveSitePage(const GURL& url) {
  const auto text = [](const char* key) {
    return base::EscapeForHTML(L10nUTF8(key));
  };
  const auto answer = [&](const char* choice) {
    return JavaScriptUrl(net::AppendOrReplaceQueryParameter(
        url, kAnswerParam, base::StrCat({SessionToken(), choice})));
  };
  return base::StrCat({
      R"(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width">
<meta name="color-scheme" content="light dark">
<title>)",
      text("barrier.sensitive.title"),
      R"(</title><style>
body { font: 15px system-ui, sans-serif; margin: 0; display: grid;
  place-items: center; min-height: 100vh; background: Canvas;
  color: CanvasText; }
main { max-width: 520px; padding: 32px; }
h1 { font-size: 22px; font-weight: 600; margin: 0 0 12px; }
p { line-height: 1.5; opacity: 0.85; }
.buttons { display: flex; gap: 10px; margin-top: 24px; flex-wrap: wrap; }
button { font: inherit; border: none; border-radius: 10px; padding: 9px 16px;
  cursor: pointer; background: color-mix(in srgb, CanvasText 10%, Canvas);
  color: CanvasText; }
button.primary { background: #3a82f7; color: white; font-weight: 600; }
</style></head><body><main>
<h1>)",
      text("barrier.sensitive.title"),
      R"(</h1><p>)",
      text("barrier.sensitive.text"),
      R"(</p><div class="buttons">
<button class="primary" id="temporary">)",
      text("barrier.sensitive.temporary"),
      R"(</button><button id="here">)",
      text("barrier.sensitive.here"),
      R"(</button></div></main><script>
document.getElementById('temporary').onclick = () => location.replace()",
      answer(".temporary"),
      R"();
document.getElementById('here').onclick = () => location.replace()",
      answer(".here"),
      R"();
</script></body></html>)",
  });
}

}  // namespace oblada::barrier
