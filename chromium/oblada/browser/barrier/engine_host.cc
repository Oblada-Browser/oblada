#include "oblada/browser/barrier/engine_host.h"

#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "oblada/components/barrier/adblock/adblock_engine.h"
#include "third_party/blink/public/mojom/loader/resource_load_info.mojom-shared.h"

namespace oblada::barrier {

namespace {

using blink::mojom::ResourceType;

// Whether `host` is the page's own site (eTLD+1), or one of its hosts.
bool IsFirstParty(const GURL& url, std::string_view page_host) {
  return net::registry_controlled_domains::SameDomainOrHost(
      url, GURL(base::StrCat({"https://", page_host, "/"})),
      net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
}

void AddAll(const base::ListValue* list, std::set<std::string>& out) {
  if (!list) {
    return;
  }
  for (const base::Value& value : *list) {
    if (value.is_string()) {
      out.insert(value.GetString());
    }
  }
}

// A $csp rule's policies without the directives that send reports: a list
// could otherwise have every page report its address to wherever it says
// (uBlock Origin refuses such rules too).
std::string WithoutReports(std::string_view policies) {
  std::vector<std::string> kept;
  for (std::string_view policy : base::SplitStringPiece(
           policies, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    std::vector<std::string_view> directives;
    for (std::string_view directive : base::SplitStringPiece(
             policy, ";", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
      const std::string_view name =
          directive.substr(0, directive.find_first_of(" \t"));
      if (!base::EqualsCaseInsensitiveASCII(name, "report-uri") &&
          !base::EqualsCaseInsensitiveASCII(name, "report-to")) {
        directives.push_back(directive);
      }
    }
    if (!directives.empty()) {
      kept.push_back(base::JoinString(directives, "; "));
    }
  }
  return base::JoinString(kept, ", ");
}

}  // namespace

std::string_view AdblockRequestType(int resource_type) {
  switch (static_cast<ResourceType>(resource_type)) {
    case ResourceType::kMainFrame:
    case ResourceType::kNavigationPreloadMainFrame:
      return "main_frame";
    case ResourceType::kSubFrame:
    case ResourceType::kNavigationPreloadSubFrame:
      return "sub_frame";
    case ResourceType::kStylesheet:
      return "stylesheet";
    case ResourceType::kScript:
    case ResourceType::kWorker:
    case ResourceType::kSharedWorker:
    case ResourceType::kServiceWorker:
      return "script";
    case ResourceType::kImage:
    case ResourceType::kFavicon:
      return "image";
    case ResourceType::kFontResource:
      return "font";
    case ResourceType::kObject:
    case ResourceType::kPluginResource:
      return "object";
    case ResourceType::kMedia:
      return "media";
    case ResourceType::kXhr:
    case ResourceType::kJson:
      return "xmlhttprequest";
    case ResourceType::kPing:
      return "ping";
    case ResourceType::kCspReport:
      return "csp_report";
    case ResourceType::kPrefetch:
    case ResourceType::kSubResource:
      return "other";
  }
  return "other";
}

// static
EngineHost& EngineHost::Get() {
  static base::NoDestructor<EngineHost> host;
  return *host;
}

EngineHost::EngineHost()
    : task_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::TaskPriority::USER_BLOCKING,
           base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN})) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

EngineHost::~EngineHost() = default;

void EngineHost::SetEngines(
    std::unique_ptr<adblock::Engine> default_engine,
    std::unique_ptr<adblock::Engine> additional_engine) {
  task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&EngineHost::SetEnginesOnSequence, base::Unretained(this),
                     std::move(default_engine), std::move(additional_engine)));
}

void EngineHost::SetEnginesOnSequence(
    std::unique_ptr<adblock::Engine> default_engine,
    std::unique_ptr<adblock::Engine> additional_engine) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  default_engine_ = std::move(default_engine);
  additional_engine_ = std::move(additional_engine);
}

bool EngineHost::HasRulesOnSequence() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return default_engine_ || additional_engine_;
}

// As Brave's AdBlockEngineWrapper::ShouldStartRequest(): the default engine
// first, but for the page's own requests out of aggressive blocking; then the
// additional engine, which knows what the default one matched.
EngineHost::Decision EngineHost::Check(const Request& request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Decision decision;
  if (!request.url.SchemeIsHTTPOrHTTPS() && !request.url.SchemeIsWSOrWSS()) {
    return decision;
  }
  const bool first_party = IsFirstParty(request.url, request.page_host);
  const std::string_view host = request.url.host();
  adblock::Engine::Request engine_request{
      .url = request.url.spec(),
      .hostname = host,
      .source_hostname = request.page_host,
      .type = request.type,
      .third_party = !first_party,
      .method = request.method,
  };

  adblock::Engine::Match match;
  if (default_engine_) {
    match = default_engine_->Check(engine_request);
    if (first_party && !request.aggressive && !match.important) {
      // Its rules block third parties only, out of aggressive blocking.
      match.matched = false;
      match.should_block = false;
      match.redirect.clear();
    }
  }
  if (additional_engine_ && !match.important) {
    engine_request.previously_matched_rule = match.matched;
    engine_request.force_check_exceptions = match.has_exception;
    adblock::Engine::Match additional = additional_engine_->Check(engine_request);
    match.matched |= additional.matched;
    match.has_exception |= additional.has_exception;
    match.important |= additional.important;
    if (match.redirect.empty()) {
      match.redirect = std::move(additional.redirect);
    }
    if (match.rewritten_url.empty()) {
      match.rewritten_url = std::move(additional.rewritten_url);
    }
  }

  decision.block = match.important || (match.matched && !match.has_exception);
  if (decision.block) {
    decision.redirect = std::move(match.redirect);
  } else if (!match.rewritten_url.empty()) {
    decision.rewritten_url = GURL(match.rewritten_url);
  }
  return decision;
}

std::string EngineHost::CspDirectives(const Request& request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string_view host = request.url.host();
  const adblock::Engine::Request engine_request{
      .url = request.url.spec(),
      .hostname = host,
      .source_hostname = request.page_host,
      .type = request.type,
      .third_party = !IsFirstParty(request.url, request.page_host),
  };
  std::vector<std::string> directives;
  for (const adblock::Engine* engine :
       {default_engine_.get(), additional_engine_.get()}) {
    if (engine) {
      std::string csp = WithoutReports(engine->CspDirectives(engine_request));
      if (!csp.empty()) {
        directives.push_back(std::move(csp));
      }
    }
  }
  return base::JoinString(directives, ",");
}

void EngineHost::GetCosmeticResources(
    const GURL& url,
    base::OnceCallback<void(CosmeticResources)> done) {
  task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&EngineHost::CosmeticResourcesOnSequence,
                     base::Unretained(this), url),
      std::move(done));
}

EngineHost::CosmeticResources EngineHost::CosmeticResourcesOnSequence(
    const GURL& url) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::set<std::string> hide_selectors;
  std::set<std::string> procedural_actions;
  std::set<std::string> exceptions;
  CosmeticResources resources;
  for (const adblock::Engine* engine :
       {default_engine_.get(), additional_engine_.get()}) {
    if (!engine) {
      continue;
    }
    std::optional<base::Value> parsed = base::JSONReader::Read(
        engine->CosmeticResources(url.spec()), base::JSON_PARSE_RFC);
    if (!parsed || !parsed->is_dict()) {
      continue;
    }
    const base::DictValue& dict = parsed->GetDict();
    AddAll(dict.FindList("hide_selectors"), hide_selectors);
    AddAll(dict.FindList("procedural_actions"), procedural_actions);
    AddAll(dict.FindList("exceptions"), exceptions);
    if (const std::string* script = dict.FindString("injected_script")) {
      resources.injected_script += *script;
    }
    resources.generichide |= dict.FindBool("generichide").value_or(false);
  }
  resources.hide_selectors.assign(hide_selectors.begin(), hide_selectors.end());
  resources.procedural_actions.assign(procedural_actions.begin(),
                                      procedural_actions.end());
  resources.exceptions.assign(exceptions.begin(), exceptions.end());
  return resources;
}

void EngineHost::GetHiddenClassIdSelectors(
    std::vector<std::string> classes,
    std::vector<std::string> ids,
    std::vector<std::string> exceptions,
    base::OnceCallback<void(std::vector<std::string>)> done) {
  task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&EngineHost::HiddenClassIdSelectorsOnSequence,
                     base::Unretained(this), std::move(classes), std::move(ids),
                     std::move(exceptions)),
      std::move(done));
}

std::vector<std::string> EngineHost::HiddenClassIdSelectorsOnSequence(
    const std::vector<std::string>& classes,
    const std::vector<std::string>& ids,
    const std::vector<std::string>& exceptions) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<std::string> selectors;
  for (const adblock::Engine* engine :
       {default_engine_.get(), additional_engine_.get()}) {
    if (engine) {
      for (std::string& selector :
           engine->HiddenClassIdSelectors(classes, ids, exceptions)) {
        selectors.push_back(std::move(selector));
      }
    }
  }
  return selectors;
}

}  // namespace oblada::barrier
