#include "oblada/browser/barrier/domain_block.h"

#include <set>
#include <string>

#include "base/no_destructor.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/unguessable_token.h"
#include "net/base/url_util.h"
#include "oblada/browser/barrier/barrier.h"
#include "oblada/browser/l10n.h"
#include "url/gurl.h"

namespace oblada::barrier {

namespace {

// The query parameter the warning's "Open anyway" adds, with a token of the
// session's, which pages can't guess.
constexpr char kProceedParam[] = "oblada-barrier-proceed";

std::string& Token() {
  static base::NoDestructor<std::string> token;
  return *token;
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
  Token() = base::UnguessableToken::Create().ToString();
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

const std::string& SessionToken() {
  return Token();
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
      JavaScriptUrl(
          net::AppendOrReplaceQueryParameter(url, kProceedParam, Token())));
  return page;
}

bool IsProceeding(const GURL& url, GURL* original) {
  std::string token;
  if (Token().empty() ||
      !net::GetValueForKeyInQuery(url, kProceedParam, &token) ||
      token != Token()) {
    return false;
  }
  *original = WithoutQueryParameter(url, kProceedParam);
  Proceeded().insert(SiteOf(url));
  return true;
}

bool HasProceeded(const GURL& url) {
  return Proceeded().contains(SiteOf(url));
}

}  // namespace oblada::barrier
