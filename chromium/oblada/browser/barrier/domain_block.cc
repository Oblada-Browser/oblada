#include "oblada/browser/barrier/domain_block.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>

#include "base/no_destructor.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/synchronization/lock.h"
#include "base/time/time.h"
#include "base/unguessable_token.h"
#include "net/base/url_util.h"
#include "oblada/browser/barrier/barrier.h"
#include "oblada/browser/l10n.h"
#include "url/gurl.h"

namespace oblada::barrier {

namespace {

// The query parameter the warning's "Open anyway" adds (AnswerUrl()).
constexpr char kProceedParam[] = "oblada-barrier-proceed";
constexpr char kProceedAnswer[] = "proceed";

// The answers AnswerUrl() made, by token, until they're taken or too old.
struct Answer {
  GURL page;
  std::string answer;
  base::TimeTicks made;
};
constexpr base::TimeDelta kAnswerLifetime = base::Hours(1);
constexpr size_t kMaxAnswers = 1000;

base::Lock& AnswersLock() {
  static base::NoDestructor<base::Lock> lock;
  return *lock;
}

std::map<std::string, Answer>& Answers() {
  static base::NoDestructor<std::map<std::string, Answer>> answers;
  return *answers;
}

// The page, with $HOST and $PROCEED to fill in.
std::string& Template() {
  static base::NoDestructor<std::string> page;
  return *page;
}

std::set<std::string>& Proceeded() {
  static base::NoDestructor<std::set<std::string>> sites;
  return *sites;
}

}  // namespace

void InitDomainBlock() {
  const auto text = [](const char* key) {
    return base::EscapeForHTML(L10nUTF8(key));
  };
  Template() = base::StrCat({
      R"(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width">
<meta name="color-scheme" content="light dark">
<title>)",
      text("barrier.blocked_page.title"),
      R"(</title><style>
body { font: 15px system-ui, sans-serif; margin: 0; display: grid;
  place-items: center; min-height: 100vh; background: Canvas;
  color: CanvasText; }
main { max-width: 520px; padding: 32px; }
h1 { font-size: 22px; font-weight: 600; margin: 0 0 12px; }
p { line-height: 1.5; opacity: 0.85; }
.host { font-weight: 600; word-break: break-all; }
.buttons { display: flex; gap: 10px; margin-top: 24px; }
button { font: inherit; border: none; border-radius: 10px; padding: 9px 16px;
  cursor: pointer; background: color-mix(in srgb, CanvasText 10%, Canvas);
  color: CanvasText; }
button.primary { background: #3a82f7; color: white; font-weight: 600; }
</style></head><body><main>
<h1>)",
      text("barrier.blocked_page.title"),
      R"(</h1><p><span class="host">$HOST</span> )",
      text("barrier.blocked_page.text"),
      R"(</p><p>)",
      text("barrier.blocked_page.note"),
      R"(</p><div class="buttons">
<button class="primary" id="back">)",
      text("barrier.blocked_page.back"),
      R"(</button><button id="proceed">)",
      text("barrier.blocked_page.proceed"),
      R"(</button></div></main><script>
document.getElementById('back').onclick = () => {
  if (history.length > 1) { history.back(); } else { location.replace('about:blank'); }
};
document.getElementById('proceed').onclick = () => location.replace($PROCEED);
</script></body></html>)",
  });
}

GURL AnswerUrl(const GURL& url, std::string_view param, std::string_view answer) {
  const GURL page = WithoutQueryParameter(url, param);
  const std::string token = base::UnguessableToken::Create().ToString();
  const base::TimeTicks now = base::TimeTicks::Now();
  base::AutoLock lock(AnswersLock());
  std::map<std::string, Answer>& answers = Answers();
  std::erase_if(answers, [&](const auto& entry) {
    return now - entry.second.made > kAnswerLifetime;
  });
  if (answers.size() >= kMaxAnswers) {
    answers.erase(std::ranges::min_element(
        answers, {}, [](const auto& entry) { return entry.second.made; }));
  }
  answers.emplace(token, Answer{page, std::string(answer), now});
  return net::AppendOrReplaceQueryParameter(page, param, token);
}

std::optional<std::string> TakeAnswer(const GURL& url,
                                      std::string_view param,
                                      GURL* original) {
  std::string token;
  if (!net::GetValueForKeyInQuery(url, param, &token)) {
    return std::nullopt;
  }
  const GURL page = WithoutQueryParameter(url, param);
  base::AutoLock lock(AnswersLock());
  const auto it = Answers().find(token);
  if (it == Answers().end() || it->second.page != page ||
      base::TimeTicks::Now() - it->second.made > kAnswerLifetime) {
    return std::nullopt;
  }
  std::string answer = std::move(it->second.answer);
  Answers().erase(it);
  *original = page;
  return answer;
}

std::string JavaScriptUrl(const GURL& url) {
  // Only URL characters, and no quotes, backslashes or angle brackets.
  std::string js = "'";
  for (char c : url.spec()) {
    if (base::IsAsciiAlphaNumeric(c) ||
        std::string_view(":/?#&=%.-_~+,;@!$*()").find(c) !=
            std::string_view::npos) {
      js += c;
    }
  }
  js += "'";
  return js;
}

GURL WithoutQueryParameter(const GURL& url, std::string_view name) {
  GURL stripped = net::AppendOrReplaceQueryParameter(url, name, std::nullopt);
  if (stripped.has_query() && stripped.query().empty()) {
    GURL::Replacements replacements;
    replacements.ClearQuery();
    stripped = stripped.ReplaceComponents(replacements);
  }
  return stripped;
}

std::string DomainBlockPage(const GURL& url) {
  std::string page = Template();
  base::ReplaceFirstSubstringAfterOffset(
      &page, 0, "$HOST", base::EscapeForHTML(url.host()));
  base::ReplaceFirstSubstringAfterOffset(
      &page, 0, "$PROCEED",
      JavaScriptUrl(AnswerUrl(url, kProceedParam, kProceedAnswer)));
  return page;
}

bool IsProceeding(const GURL& url, GURL* original) {
  if (TakeAnswer(url, kProceedParam, original) != kProceedAnswer) {
    return false;
  }
  Proceeded().insert(SiteOf(*original));
  return true;
}

bool HasProceeded(const GURL& url) {
  return Proceeded().contains(SiteOf(url));
}

}  // namespace oblada::barrier
