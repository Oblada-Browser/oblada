#include "oblada/renderer/barrier/barrier_agent.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "content/public/renderer/render_frame.h"
#include "gin/arguments.h"
#include "gin/converter.h"
#include "gin/function_template.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "oblada/barrier_scripts.h"
#include "services/service_manager/public/cpp/interface_provider.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_registry.h"
#include "third_party/blink/public/platform/browser_interface_broker_proxy.h"
#include "third_party/blink/public/platform/scheduler/web_agent_group_scheduler.h"
#include "third_party/blink/public/platform/web_isolated_world_info.h"
#include "third_party/blink/public/platform/web_security_origin.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_script_source.h"
#include "url/gurl.h"
#include "v8/include/v8-context.h"
#include "v8/include/v8-function.h"
#include "v8/include/v8-isolate.h"
#include "v8/include/v8-object.h"
#include "v8/include/v8-primitive.h"

namespace oblada::barrier {

namespace {

// The cosmetic filters' world in each frame.
constexpr int32_t kWorldId = ISOLATED_WORLD_ID_OBLADA_BARRIER;

// Selectors that would close the rule they're in, and add rules of their own.
bool IsSafeSelector(std::string_view selector) {
  return !selector.empty() && selector.size() < 4096 &&
         selector.find_first_of("{}") == std::string_view::npos &&
         selector.find("/*") == std::string_view::npos;
}

std::string HidingStyleSheet(const std::vector<std::string>& selectors) {
  // A rule each: one that isn't valid CSS drops only itself.
  std::string css;
  for (const std::string& selector : selectors) {
    if (IsSafeSelector(selector)) {
      base::StrAppend(&css, {selector, "{display:none!important}\n"});
    }
  }
  return css;
}

}  // namespace

// static
void BarrierAgent::Create(content::RenderFrame* render_frame) {
  new BarrierAgent(render_frame);
}

// static
const oblada::mojom::BarrierPageSettings* BarrierAgent::SettingsFor(
    content::RenderFrame* render_frame) {
  BarrierAgent* agent = render_frame ? Get(render_frame) : nullptr;
  if (!agent) {
    return nullptr;
  }
  if (agent->settings_) {
    return agent->settings_.get();
  }
  // A document without settings of its own has its parent's.
  blink::WebLocalFrame* frame = render_frame->GetWebFrame();
  return frame->Parent() && frame->Parent()->IsWebLocalFrame()
             ? SettingsFor(content::RenderFrame::FromWebFrame(
                   frame->Parent()->ToWebLocalFrame()))
             : nullptr;
}

BarrierAgent::BarrierAgent(content::RenderFrame* render_frame)
    : content::RenderFrameObserver(render_frame),
      content::RenderFrameObserverTracker<BarrierAgent>(render_frame) {
  render_frame->GetAssociatedInterfaceRegistry()
      ->AddInterface<oblada::mojom::BarrierAgent>(base::BindRepeating(
          &BarrierAgent::Bind, weak_factory_.GetWeakPtr()));
  // The frame's initial empty document, which its parent's scripts can use.
  ApplyFarbling();
}

BarrierAgent::~BarrierAgent() = default;

void BarrierAgent::Bind(
    mojo::PendingAssociatedReceiver<oblada::mojom::BarrierAgent> receiver) {
  receiver_.reset();
  receiver_.Bind(std::move(receiver));
}

void BarrierAgent::OnDestruct() {
  delete this;
}

void BarrierAgent::SetPageSettings(
    oblada::mojom::BarrierPageSettingsPtr settings) {
  pending_ = std::move(settings);
}

void BarrierAgent::DidCreateNewDocument() {
  // The document the browser sent settings for (or not: about:blank's, an
  // iframe's srcdoc, which get their parent's protection).
  ++document_;
  settings_ = std::move(pending_);
  started_ = false;
  ApplyFarbling();
}

void BarrierAgent::ApplyFarbling() {
  const oblada::mojom::BarrierPageSettings* settings = settings_.get();
  blink::WebLocalFrame* frame = render_frame()->GetWebFrame();
  if (!settings && frame->Parent() && frame->Parent()->IsWebLocalFrame()) {
    settings = SettingsFor(content::RenderFrame::FromWebFrame(
        frame->Parent()->ToWebLocalFrame()));
  }
  frame->SetObladaFarbling(settings ? settings->farbling_token : 0,
                           settings ? settings->farbling_exceptions : 0,
                           settings && settings->reduce_language);
}

void BarrierAgent::DidCreateScriptContext(v8::Local<v8::Context> context,
                                          int32_t world_id) {
  blink::WebLocalFrame* frame = render_frame()->GetWebFrame();
  if (world_id != kWorldId) {
    return;
  }
  // The cosmetic filters' world (and the element picker's): `obladaBarrier`.
  v8::Isolate* isolate = frame->GetAgentGroupScheduler()->Isolate();
  v8::Context::Scope context_scope(context);
  v8::Local<v8::Object> barrier = v8::Object::New(isolate);
  auto set = [&](std::string_view name, v8::Local<v8::Value> value) {
    barrier->Set(context, gin::StringToSymbol(isolate, name), value).Check();
  };
  auto function = [&](void (BarrierAgent::*method)(gin::Arguments*)) {
    return gin::CreateFunctionTemplate(
               isolate,
               base::BindRepeating(method, weak_factory_.GetWeakPtr()))
        ->GetFunction(context)
        .ToLocalChecked();
  };
  set("hiddenSelectors", function(&BarrierAgent::HiddenSelectors));
  set("hide", function(&BarrierAgent::HideFromScript));
  set("sameSite", function(&BarrierAgent::SameSite));
  set("addFilter", function(&BarrierAgent::AddFilter));
  set("pickerStrings", function(&BarrierAgent::PickerStrings));
  const bool cosmetic = settings_ && settings_->cosmetic_filtering;
  v8::Local<v8::Object> config = v8::Object::New(isolate);
  config
      ->Set(context, gin::StringToSymbol(isolate, "aggressive"),
            v8::Boolean::New(isolate, cosmetic && settings_->aggressive))
      .Check();
  config
      ->Set(context, gin::StringToSymbol(isolate, "genericHide"),
            v8::Boolean::New(isolate, !cosmetic || settings_->generic_hide))
      .Check();
  config
      ->Set(context, gin::StringToSymbol(isolate, "procedural"),
            gin::ConvertToV8(isolate, cosmetic ? settings_->procedural_actions
                                               : std::vector<std::string>()))
      .Check();
  set("config", config);
  context->Global()
      ->Set(context, gin::StringToSymbol(isolate, "obladaBarrier"), barrier)
      .Check();
}

// static
void BarrierAgent::RunScriptsAtDocumentStart(
    content::RenderFrame* render_frame) {
  if (BarrierAgent* agent = Get(render_frame)) {
    agent->RunAtDocumentStart();
  }
}

void BarrierAgent::RunAtDocumentStart() {
  // Once a document (this may be called again for the same one).
  if (started_ || !settings_ || !settings_->cosmetic_filtering) {
    return;
  }
  started_ = true;
  // The scriptlets, before any of the page's own scripts, as extensions'
  // scripts at document_start: in a namespace of their own, with the globals
  // uBlock Origin's scriptlets share, as uBlock Origin injects them.
  if (!settings_->scriptlets.empty()) {
    Run(blink::kMainDOMWorldId,
        base::StrCat({"(function() {\nconst scriptletGlobals = {};\n",
                      settings_->scriptlets, "\n})();"}));
  }
  Hide(settings_->hide_selectors);
  RunInWorld(kCosmeticFiltersScript);
}

void BarrierAgent::StartElementPicker(
    const base::flat_map<std::string, std::string>& strings) {
  picker_strings_ = strings;
  RunInWorld(kElementPickerScript);
}

void BarrierAgent::RunInWorld(const std::string& script) {
  blink::WebIsolatedWorldInfo info;
  info.security_origin = render_frame()->GetWebFrame()->GetSecurityOrigin();
  info.human_readable_name = blink::WebString::FromUtf8("Oblada Barrier");
  blink::SetIsolatedWorldInfo(kWorldId, info);
  Run(kWorldId, script);
}

void BarrierAgent::Run(int32_t world_id, const std::string& script) {
  const blink::WebScriptSource source(blink::WebString::FromUtf8(script));
  blink::WebLocalFrame* frame = render_frame()->GetWebFrame();
  if (world_id == blink::kMainDOMWorldId) {
    frame->ExecuteScript(source);
  } else {
    frame->ExecuteScriptInIsolatedWorld(world_id, source,
                                        blink::BackForwardCacheAware::kAllow);
  }
}

void BarrierAgent::Hide(const std::vector<std::string>& selectors) {
  const std::string css = HidingStyleSheet(selectors);
  if (!css.empty()) {
    render_frame()->GetWebFrame()->GetDocument().InsertStyleSheet(
        blink::WebString::FromUtf8(css), nullptr, blink::WebCssOrigin::kUser);
  }
}

oblada::mojom::BarrierHost* BarrierAgent::host() {
  if (!host_.is_bound()) {
    render_frame()->GetBrowserInterfaceBroker().GetInterface(
        host_.BindNewPipeAndPassReceiver());
  }
  return host_.get();
}

void BarrierAgent::HiddenSelectors(gin::Arguments* args) {
  std::vector<std::string> classes;
  std::vector<std::string> ids;
  v8::Local<v8::Function> callback;
  if (!settings_ || !args->GetNext(&classes) || !args->GetNext(&ids) ||
      !args->GetNext(&callback)) {
    return;
  }
  host()->HiddenClassIdSelectors(
      classes, ids, settings_->exceptions,
      base::BindOnce(&BarrierAgent::OnHiddenSelectors,
                     weak_factory_.GetWeakPtr(), document_,
                     v8::Global<v8::Function>(args->isolate(), callback)));
}

void BarrierAgent::OnHiddenSelectors(int document,
                                     v8::Global<v8::Function> callback,
                                     const std::vector<std::string>& selectors) {
  if (document != document_ || selectors.empty()) {
    return;
  }
  blink::WebLocalFrame* frame = render_frame()->GetWebFrame();
  v8::Isolate* isolate = frame->GetAgentGroupScheduler()->Isolate();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Function> function = callback.Get(isolate);
  v8::Local<v8::Context> context = function->GetCreationContextChecked();
  v8::Context::Scope context_scope(context);
  v8::Local<v8::Value> argv[] = {gin::ConvertToV8(isolate, selectors)};
  std::ignore = frame->CallFunctionEvenIfScriptDisabled(
      function, context->Global(), std::size(argv), argv);
}

void BarrierAgent::PickerStrings(gin::Arguments* args) {
  v8::Isolate* isolate = args->isolate();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Object> strings = v8::Object::New(isolate);
  for (const auto& [key, value] : picker_strings_) {
    strings
        ->Set(context, gin::StringToSymbol(isolate, key),
              gin::StringToV8(isolate, value))
        .Check();
  }
  args->Return(strings);
}

void BarrierAgent::HideFromScript(gin::Arguments* args) {
  std::vector<std::string> selectors;
  if (args->GetNext(&selectors)) {
    Hide(selectors);
  }
}

void BarrierAgent::SameSite(gin::Arguments* args) {
  std::string url;
  if (!args->GetNext(&url)) {
    return;
  }
  args->Return(net::registry_controlled_domains::SameDomainOrHost(
      GURL(url), GURL(render_frame()->GetWebFrame()->GetDocument().Url()),
      net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES));
}

void BarrierAgent::AddFilter(gin::Arguments* args) {
  std::string filter;
  if (args->GetNext(&filter)) {
    host()->AddCustomFilter(filter);
  }
}

}  // namespace oblada::barrier
