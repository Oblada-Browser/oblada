#include "oblada/browser/barrier/request_filter.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/byte_size.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/strcat.h"
#include "base/scoped_observation.h"
#include "base/supports_user_data.h"
#include "base/task/single_thread_task_runner.h"
#include "base/types/pass_key.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/net/proxy_service_factory.h"
#include "chrome/browser/net/secure_dns_config.h"
#include "chrome/browser/net/stub_resolver_config_reader.h"
#include "chrome/browser/net/system_network_context_manager.h"
#include "chrome/browser/policy/profile_policy_connector.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_observer.h"
#include "components/proxy_config/pref_proxy_config_tracker.h"
#include "components/language/core/browser/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/api/web_request/web_request_api.h"
#include "extensions/browser/browser_context_keyed_api_factory.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "mojo/public/cpp/system/data_pipe_producer.h"
#include "mojo/public/cpp/system/string_data_source.h"
#include "net/base/address_list.h"
#include "net/base/data_url.h"
#include "net/base/host_port_pair.h"
#include "net/base/network_anonymization_key.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/base/url_util.h"
#include "net/base/isolation_info.h"
#include "net/base/net_errors.h"
#include "net/cookies/site_for_cookies.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/proxy_resolution/proxy_config_service.h"
#include "net/proxy_resolution/proxy_config_with_annotation.h"
#include "net/url_request/redirect_info.h"
#include "net/url_request/redirect_util.h"
#include "oblada/browser/barrier/barrier.h"
#include "oblada/browser/barrier/domain_block.h"
#include "oblada/browser/barrier/engine_host.h"
#include "oblada/browser/barrier/google_sign_in.h"
#include "oblada/browser/barrier/sensitive_sites.h"
#include "oblada/browser/barrier/storage.h"
#include "oblada/browser/barrier/tab_helper.h"
#include "oblada/browser/barrier/url_cleaning.h"
#include "services/network/public/cpp/content_security_policy/content_security_policy.h"
#include "services/network/public/cpp/http_request_headers_update_params.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/self_deleting_url_loader_factory.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/cpp/url_loader_factory_builder.h"
#include "services/network/public/mojom/early_hints.mojom.h"
#include "services/network/public/mojom/host_resolver.mojom.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "services/network/public/mojom/parsed_headers.mojom.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "services/network/public/mojom/websocket.mojom.h"
#include "third_party/blink/public/mojom/loader/resource_load_info.mojom-shared.h"
#include "url/origin.h"

namespace oblada::barrier {

namespace {

using URLLoaderFactoryType = content::ContentBrowserClient::URLLoaderFactoryType;

// What the factory's requests are for, as it was made.
struct FilterContext {
  // The page's host, which first and third parties are told apart by; empty
  // for a page's own navigation, whose host it is.
  std::string page_host;
  bool ads = false;
  bool aggressive = false;
  // Tracking parameters are stripped (url_cleaning.h).
  bool strip = false;
  // The Accept-Language requests get, when languages are reduced; empty
  // otherwise.
  std::string accept_language;
  // Navigations' factory: documents, whose responses may get a CSP ($csp).
  bool navigation = false;
  // Whose tab counts what's blocked; none for workers'.
  content::GlobalRenderFrameHostId frame;
  // For pages' own navigations, whose settings are their address's, on the
  // UI thread.
  base::WeakPtr<Profile> profile;
  // The container's resolver, which hosts' canonical names come from (CNAME
  // uncloaking); unbound when they aren't checked.
  mojo::PendingRemote<network::mojom::HostResolver> resolver;
  net::NetworkAnonymizationKey network_anonymization_key;
  // Secure DNS only: the names come from DNS over HTTPS, not the system's.
  bool secure_dns_only = false;
};

// The Accept-Language for `profile`'s pages when the Barrier reduces
// languages on `settings`' site: the first language and its base ("it-IT,it"),
// as Brave; empty when it doesn't.
std::string ReducedAcceptLanguage(Profile* profile, const Settings& settings) {
  if (!settings.FingerprintingOn(kLanguages) ||
      !IsPrivacyOn(profile, kReduceLanguagePref)) {
    return std::string();
  }
  const std::string languages =
      profile->GetPrefs()->GetString(language::prefs::kAcceptLanguages);
  const std::string first = languages.substr(0, languages.find(','));
  if (first.empty()) {
    return std::string();
  }
  const std::string base = first.substr(0, first.find('-'));
  return base == first ? first : base::StrCat({first, ",", base, ";q=0.9"});
}

// What a request gets.
struct Decision {
  bool ads = false;
  bool aggressive = false;
  std::string accept_language;
  // A page of the Barrier's instead (a question about a sensitive site).
  std::string page;
  // Nothing: the page opens elsewhere (in a temporary container).
  bool stop = false;
  // Where it goes instead, without tracking parameters (or for where a
  // tracking redirect leads): through a redirect.
  GURL rewritten;
};

// A page's own navigation, as its site says (on the UI thread): skipping
// tracking redirects (and AMP caches), then stripping tracking parameters.
Decision DecidePage(base::WeakPtr<Profile> profile,
                    content::GlobalRenderFrameHostId frame,
                    const GURL& url,
                    const std::optional<url::Origin>& initiator) {
  if (!profile || !IsOn()) {
    return {};
  }
  Decision decision;
  // Sensitive sites, in a temporary container (or asked about first).
  content::RenderFrameHost* host = content::RenderFrameHost::FromID(frame);
  const SensitiveSiteDecision sensitive = CheckSensitiveSite(
      host ? content::WebContents::FromRenderFrameHost(host) : nullptr, url);
  switch (sensitive.action) {
    case SensitiveSiteDecision::Action::kAsk:
      decision.page = SensitiveSitePage(url);
      return decision;
    case SensitiveSiteDecision::Action::kStop:
      decision.stop = true;
      return decision;
    case SensitiveSiteDecision::Action::kGo:
      decision.rewritten = sensitive.go_to;
      return decision;
    case SensitiveSiteDecision::Action::kNone:
      break;
  }
  // Google sign-in on another site (or asked about first).
  GoogleSignInDecision google = CheckGoogleSignIn(
      host ? content::WebContents::FromRenderFrameHost(host) : nullptr, url,
      initiator);
  switch (google.action) {
    case GoogleSignInDecision::Action::kPage:
      decision.page = std::move(google.page);
      return decision;
    case GoogleSignInDecision::Action::kGo:
      decision.rewritten = google.go_to;
      return decision;
    case GoogleSignInDecision::Action::kNone:
      break;
  }
  GURL target = url;
  if (IsPrivacyOn(profile.get(), kDebouncePref)) {
    if (std::optional<GURL> debounced = DebounceTarget(
            url, IsPrivacyOn(profile.get(), kDeAmpPref))) {
      target = *debounced;
    }
  }
  const Settings settings = GetSettings(profile.get(), target);
  if (!settings.enabled) {
    return decision;
  }
  if (IsPrivacyOn(profile.get(), kStripParamsPref)) {
    if (std::optional<GURL> stripped =
            StripTrackingParameters(target, target == url ? initiator
                                                          : std::nullopt)) {
      target = *stripped;
    }
  }
  decision.ads = settings.AdsOn();
  decision.aggressive = settings.ads == AdBlocking::kAggressive;
  decision.accept_language = ReducedAcceptLanguage(profile.get(), settings);
  if (target != url) {
    decision.rewritten = target;
  }
  return decision;
}

void ReportBlocked(content::GlobalRenderFrameHostId frame, const GURL& url) {
  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE, base::BindOnce(&BarrierTabHelper::OnBlocked, frame, url));
}

// A page the lists block opened anyway (standard blocking): forgotten once
// closed, if it's new (storage.h).
void ReportBounce(content::GlobalRenderFrameHostId frame, const GURL& url) {
  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE, base::BindOnce(
                     [](content::GlobalRenderFrameHostId frame, GURL url) {
                       if (content::RenderFrameHost* host =
                               content::RenderFrameHost::FromID(frame)) {
                         ForgetUnlessKnown(
                             content::WebContents::FromRenderFrameHost(host),
                             url);
                       }
                     },
                     frame, url));
}

struct WriteData {
  mojo::Remote<network::mojom::URLLoaderClient> client;
  std::string data;
  std::unique_ptr<mojo::DataPipeProducer> producer;
};

void OnWritten(std::unique_ptr<WriteData> write, MojoResult result) {
  if (result != MOJO_RESULT_OK) {
    write->client->OnComplete(
        network::URLLoaderCompletionStatus(net::ERR_FAILED));
    return;
  }
  network::URLLoaderCompletionStatus status(net::OK);
  status.encoded_data_length = base::ByteSize(write->data.size());
  status.encoded_body_length = base::ByteSize(write->data.size());
  status.decoded_body_length = base::ByteSize(write->data.size());
  write->client->OnComplete(status);
}

// Answers the request with a data: URL's content: a $redirect rule's
// stand-in.
void Serve(const network::ResourceRequest& request,
           const std::string& data_url,
           mojo::PendingRemote<network::mojom::URLLoaderClient> client) {
  mojo::Remote<network::mojom::URLLoaderClient> remote(std::move(client));
  auto head = network::mojom::URLResponseHead::New();
  std::string data;
  const net::Error result = net::DataURL::BuildResponse(
      GURL(data_url), request.method, &head->mime_type, &head->charset, &data,
      &head->headers);
  mojo::ScopedDataPipeProducerHandle producer;
  mojo::ScopedDataPipeConsumerHandle consumer;
  if (result != net::OK ||
      mojo::CreateDataPipe(nullptr, producer, consumer) != MOJO_RESULT_OK) {
    remote->OnComplete(
        network::URLLoaderCompletionStatus(net::ERR_BLOCKED_BY_CLIENT));
    return;
  }
  // Fetched across origins too.
  head->headers->SetHeader("Access-Control-Allow-Origin", "*");
  remote->OnReceiveResponse(std::move(head), std::move(consumer), std::nullopt);

  auto write = std::make_unique<WriteData>();
  write->client = std::move(remote);
  write->data = std::move(data);
  write->producer = std::make_unique<mojo::DataPipeProducer>(std::move(producer));
  mojo::DataPipeProducer* producer_ptr = write->producer.get();
  const std::string_view view(write->data);
  producer_ptr->Write(
      std::make_unique<mojo::StringDataSource>(
          view, mojo::StringDataSource::AsyncWritingMode::
                    STRING_STAYS_VALID_UNTIL_COMPLETION),
      base::BindOnce(&OnWritten, std::move(write)));
}

// Answers the request with an HTML page of the Barrier's.
void ServePage(const std::string& html,
               mojo::PendingRemote<network::mojom::URLLoaderClient> client) {
  mojo::Remote<network::mojom::URLLoaderClient> remote(std::move(client));
  auto head = network::mojom::URLResponseHead::New();
  head->headers = net::HttpResponseHeaders::TryToCreate(
      "HTTP/1.1 200 OK\nContent-Type: text/html; charset=utf-8\n"
      "Cache-Control: no-store\n\n");
  head->mime_type = "text/html";
  head->charset = "utf-8";
  mojo::ScopedDataPipeProducerHandle producer;
  mojo::ScopedDataPipeConsumerHandle consumer;
  if (mojo::CreateDataPipe(nullptr, producer, consumer) != MOJO_RESULT_OK) {
    remote->OnComplete(
        network::URLLoaderCompletionStatus(net::ERR_BLOCKED_BY_CLIENT));
    return;
  }
  remote->OnReceiveResponse(std::move(head), std::move(consumer), std::nullopt);
  auto write = std::make_unique<WriteData>();
  write->client = std::move(remote);
  write->data = html;
  write->producer = std::make_unique<mojo::DataPipeProducer>(std::move(producer));
  mojo::DataPipeProducer* producer_ptr = write->producer.get();
  const std::string_view view(write->data);
  producer_ptr->Write(
      std::make_unique<mojo::StringDataSource>(
          view, mojo::StringDataSource::AsyncWritingMode::
                    STRING_STAYS_VALID_UNTIL_COMPLETION),
      base::BindOnce(&OnWritten, std::move(write)));
}

// Writes `data` to the client, then completes the request.
// `request` as it goes on after `redirect`, with the client's changes to its
// headers (`params`), as the network service makes it: a redirected page's
// network partition is its new site's.
void ApplyRedirect(network::ResourceRequest& request,
                   const net::RedirectInfo& redirect,
                   network::HttpRequestHeadersUpdateParams params) {
  bool clear_body = false;
  net::RedirectUtil::UpdateHttpRequest(
      request.url, request.method, redirect, params.removed_headers,
      params.modified_headers, &request.headers, &clear_body);
  for (const std::string& name : params.removed_headers) {
    request.cors_exempt_headers.RemoveHeader(name);
  }
  request.cors_exempt_headers.MergeFrom(params.modified_cors_exempt_headers);
  if (clear_body) {
    request.request_body = nullptr;
  }
  request.url = redirect.new_url;
  request.method = redirect.new_method;
  request.site_for_cookies = redirect.new_site_for_cookies;
  request.referrer = GURL(redirect.new_referrer);
  request.referrer_policy = redirect.new_referrer_policy;
  if (request.trusted_params) {
    request.trusted_params->isolation_info =
        request.trusted_params->isolation_info.CreateForRedirect(
            url::Origin::Create(redirect.new_url));
  }
}

// Answers the request with a redirect to `new_url`, the same request without
// the parameters $removeparam rules strip; once followed, the request goes on
// to the network for it.
class RewriteLoader : public network::mojom::URLLoader {
 public:
  static void Start(
      mojo::PendingReceiver<network::mojom::URLLoader> receiver,
      int32_t request_id,
      uint32_t options,
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const net::MutableNetworkTrafficAnnotationTag& traffic_annotation,
      mojo::PendingRemote<network::mojom::URLLoaderFactory> target,
      const GURL& new_url) {
    auto loader = std::make_unique<RewriteLoader>(
        request_id, options, request, std::move(client), traffic_annotation,
        std::move(target), new_url);
    RewriteLoader* raw = loader.get();
    mojo::MakeSelfOwnedReceiver(std::move(loader), std::move(receiver));
    raw->Redirect();
  }

  RewriteLoader(int32_t request_id,
                uint32_t options,
                const network::ResourceRequest& request,
                mojo::PendingRemote<network::mojom::URLLoaderClient> client,
                const net::MutableNetworkTrafficAnnotationTag& traffic_annotation,
                mojo::PendingRemote<network::mojom::URLLoaderFactory> target,
                const GURL& new_url)
      : request_id_(request_id),
        options_(options),
        request_(request),
        client_(std::move(client)),
        traffic_annotation_(traffic_annotation),
        target_(std::move(target)) {
    redirect_info_ = net::RedirectInfo::ComputeRedirectInfo(
        request_.method, request_.url, request_.site_for_cookies,
        request_.update_first_party_url_on_redirect
            ? net::RedirectInfo::FirstPartyURLPolicy::UPDATE_URL_ON_REDIRECT
            : net::RedirectInfo::FirstPartyURLPolicy::NEVER_CHANGE_URL,
        request_.referrer_policy, request_.referrer.spec(),
        request_.request_initiator, 307, new_url, std::nullopt,
        /*insecure_scheme_was_upgraded=*/false, /*copy_fragment=*/false);
  }

  // network::mojom::URLLoader:
  void FollowRedirect(network::HttpRequestHeadersUpdateParams params,
                      const std::optional<GURL>& new_url) override {
    if (target_loader_) {
      target_loader_->FollowRedirect(std::move(params), new_url);
      return;
    }
    ApplyRedirect(request_, redirect_info_, std::move(params));
    target_->CreateLoaderAndStart(
        target_loader_.BindNewPipeAndPassReceiver(), request_id_, options_,
        request_, client_.Unbind(), traffic_annotation_);
    if (priority_) {
      target_loader_->SetPriority(*priority_, intra_priority_);
    }
  }

  void SetPriority(net::RequestPriority priority,
                   int32_t intra_priority_value) override {
    if (target_loader_) {
      target_loader_->SetPriority(priority, intra_priority_value);
    } else {
      priority_ = priority;
      intra_priority_ = intra_priority_value;
    }
  }

 private:
  void Redirect() {
    auto head = network::mojom::URLResponseHead::New();
    head->headers = net::HttpResponseHeaders::TryToCreate(base::StrCat(
        {"HTTP/1.1 307 Internal Redirect\nLocation: ",
         redirect_info_.new_url.spec(),
         "\nNon-Authoritative-Reason: Oblada Barrier\n\n"}));
    head->encoded_data_length = 0;
    client_->OnReceiveRedirect(redirect_info_, std::move(head));
  }

  const int32_t request_id_;
  const uint32_t options_;
  network::ResourceRequest request_;
  mojo::Remote<network::mojom::URLLoaderClient> client_;
  const net::MutableNetworkTrafficAnnotationTag traffic_annotation_;
  mojo::Remote<network::mojom::URLLoaderFactory> target_;
  mojo::Remote<network::mojom::URLLoader> target_loader_;
  net::RedirectInfo redirect_info_;
  std::optional<net::RequestPriority> priority_;
  int32_t intra_priority_ = 0;
};

// Between a document's loader and its client: adds the Content-Security-Policy
// $csp rules call for to its response.
class CspClient : public network::mojom::URLLoaderClient {
 public:
  CspClient(const GURL& url,
            std::string page_host,
            bool aggressive,
            mojo::PendingRemote<network::mojom::URLLoaderClient> client)
      : url_(url),
        page_host_(std::move(page_host)),
        aggressive_(aggressive),
        client_(std::move(client)) {}

  // network::mojom::URLLoaderClient:
  void OnReceiveEarlyHints(network::mojom::EarlyHintsPtr early_hints) override {
    client_->OnReceiveEarlyHints(std::move(early_hints));
  }

  void OnReceiveResponse(
      network::mojom::URLResponseHeadPtr head,
      mojo::ScopedDataPipeConsumerHandle body,
      std::optional<mojo_base::BigBuffer> cached_metadata) override {
    EngineHost::Request request;
    request.url = url_;
    request.type = page_host_.empty() ? "main_frame" : "sub_frame";
    request.page_host = page_host_.empty() ? url_.host() : page_host_;
    request.aggressive = aggressive_;
    const std::string csp = EngineHost::Get().CspDirectives(request);
    if (!csp.empty() && head->headers) {
      head->headers->AddHeader("Content-Security-Policy", csp);
      if (head->parsed_headers) {
        for (auto& policy : network::ParseContentSecurityPolicies(
                 csp, network::mojom::ContentSecurityPolicyType::kEnforce,
                 network::mojom::ContentSecurityPolicySource::kHTTP, url_)) {
          head->parsed_headers->content_security_policy.push_back(
              std::move(policy));
        }
      }
    }
    client_->OnReceiveResponse(std::move(head), std::move(body),
                               std::move(cached_metadata));
  }

  void OnReceiveRedirect(const net::RedirectInfo& redirect_info,
                         network::mojom::URLResponseHeadPtr head) override {
    url_ = redirect_info.new_url;
    client_->OnReceiveRedirect(redirect_info, std::move(head));
  }

  void OnUploadProgress(int64_t current_position,
                        int64_t total_size,
                        OnUploadProgressCallback callback) override {
    client_->OnUploadProgress(current_position, total_size,
                              std::move(callback));
  }

  void OnTransferSizeUpdated(int32_t transfer_size_diff) override {
    client_->OnTransferSizeUpdated(transfer_size_diff);
  }

  void OnComplete(const network::URLLoaderCompletionStatus& status) override {
    client_->OnComplete(status);
  }

 private:
  GURL url_;
  const std::string page_host_;
  const bool aggressive_;
  mojo::Remote<network::mojom::URLLoaderClient> client_;
};

// Between a request's client and what the filter made of it, for the
// redirects the client gets: a redirect can't take a request where the lists
// block it, nor a page past the Barrier's questions (Brave checks redirects
// too). A navigation's redirect is a new request for the filter (navigations
// aren't subject to CORS); another request's is checked against the lists,
// then followed by the network as usual, so that CORS's rules on redirects
// hold.
class RedirectChecker : public network::mojom::URLLoader,
                        public network::mojom::URLLoaderClient {
 public:
  // net::URLRequest's.
  static constexpr int kMaxRedirects = 20;

  // Starts `request` in the filter (again, for a navigation redirected).
  using Begin = base::RepeatingCallback<void(
      mojo::PendingReceiver<network::mojom::URLLoader>,
      const network::ResourceRequest&,
      mojo::PendingRemote<network::mojom::URLLoaderClient>)>;
  // Whether the lists block `request` going on to `url`.
  using IsBlocked = base::RepeatingCallback<bool(const network::ResourceRequest&,
                                                 const GURL&)>;

  // `is_blocked` is null for navigations, whose redirects `begin` starts
  // again.
  static void Start(mojo::PendingReceiver<network::mojom::URLLoader> loader,
                    const network::ResourceRequest& request,
                    mojo::PendingRemote<network::mojom::URLLoaderClient> client,
                    Begin begin,
                    IsBlocked is_blocked) {
    auto checker = std::make_unique<RedirectChecker>(
        request, std::move(client), std::move(begin), std::move(is_blocked));
    RedirectChecker* raw = checker.get();
    raw->self_ = mojo::MakeSelfOwnedReceiver(std::move(checker), std::move(loader));
    raw->BeginInner();
  }

  RedirectChecker(const network::ResourceRequest& request,
                  mojo::PendingRemote<network::mojom::URLLoaderClient> client,
                  Begin begin,
                  IsBlocked is_blocked)
      : request_(request),
        client_(std::move(client)),
        begin_(std::move(begin)),
        is_blocked_(std::move(is_blocked)) {}

  // network::mojom::URLLoader, the client's:
  void FollowRedirect(network::HttpRequestHeadersUpdateParams params,
                      const std::optional<GURL>& new_url) override {
    if (!redirect_) {
      return;
    }
    net::RedirectInfo redirect = *std::exchange(redirect_, std::nullopt);
    if (!is_blocked_) {
      if (new_url) {
        redirect.new_url = *new_url;
      }
      ApplyRedirect(request_, redirect, std::move(params));
      inner_.reset();
      inner_client_.reset();
      BeginInner();
      return;
    }
    request_.url = new_url.value_or(redirect.new_url);
    inner_->FollowRedirect(std::move(params), new_url);
  }

  void SetPriority(net::RequestPriority priority,
                   int32_t intra_priority_value) override {
    priority_ = priority;
    intra_priority_ = intra_priority_value;
    if (inner_) {
      inner_->SetPriority(priority, intra_priority_value);
    }
  }

  // network::mojom::URLLoaderClient, the filter's loader's:
  void OnReceiveEarlyHints(network::mojom::EarlyHintsPtr early_hints) override {
    client_->OnReceiveEarlyHints(std::move(early_hints));
  }

  void OnReceiveResponse(
      network::mojom::URLResponseHeadPtr head,
      mojo::ScopedDataPipeConsumerHandle body,
      std::optional<mojo_base::BigBuffer> cached_metadata) override {
    client_->OnReceiveResponse(std::move(head), std::move(body),
                               std::move(cached_metadata));
  }

  void OnReceiveRedirect(const net::RedirectInfo& redirect_info,
                         network::mojom::URLResponseHeadPtr head) override {
    // As many as the network follows: a navigation's start again each time.
    if (++redirects_ > kMaxRedirects) {
      Fail(net::ERR_TOO_MANY_REDIRECTS);
      return;
    }
    if (is_blocked_ && is_blocked_.Run(request_, redirect_info.new_url)) {
      Fail(net::ERR_BLOCKED_BY_CLIENT);
      return;
    }
    redirect_ = redirect_info;
    client_->OnReceiveRedirect(redirect_info, std::move(head));
  }

  void OnUploadProgress(int64_t current_position,
                        int64_t total_size,
                        OnUploadProgressCallback callback) override {
    client_->OnUploadProgress(current_position, total_size,
                              std::move(callback));
  }

  void OnTransferSizeUpdated(int32_t transfer_size_diff) override {
    client_->OnTransferSizeUpdated(transfer_size_diff);
  }

  void OnComplete(const network::URLLoaderCompletionStatus& status) override {
    completed_ = true;
    client_->OnComplete(status);
  }

 private:
  void BeginInner() {
    mojo::PendingRemote<network::mojom::URLLoaderClient> inner_client =
        inner_client_.BindNewPipeAndPassRemote();
    // The filter gone, or the request dropped without an answer.
    inner_client_.set_disconnect_handler(base::BindOnce(
        &RedirectChecker::Fail, base::Unretained(this), net::ERR_ABORTED));
    begin_.Run(inner_.BindNewPipeAndPassReceiver(), request_,
               std::move(inner_client));
    if (priority_) {
      inner_->SetPriority(*priority_, intra_priority_);
    }
  }

  // Answers the client with `error`, unless it got its answer; then goes.
  void Fail(int error) {
    if (!completed_) {
      completed_ = true;
      client_->OnComplete(network::URLLoaderCompletionStatus(error));
    }
    inner_.reset();
    inner_client_.reset();
    self_->Close();
  }

  network::ResourceRequest request_;
  mojo::Remote<network::mojom::URLLoaderClient> client_;
  const Begin begin_;
  const IsBlocked is_blocked_;
  mojo::SelfOwnedReceiverRef<network::mojom::URLLoader> self_;
  mojo::Remote<network::mojom::URLLoader> inner_;
  mojo::Receiver<network::mojom::URLLoaderClient> inner_client_{this};
  // The redirect the client was told about, until it follows it.
  std::optional<net::RedirectInfo> redirect_;
  int redirects_ = 0;
  bool completed_ = false;
  std::optional<net::RequestPriority> priority_;
  int32_t intra_priority_ = 0;
};

// A request waiting for its host's canonical name.
struct PendingRequest {
  mojo::PendingReceiver<network::mojom::URLLoader> loader;
  int32_t request_id = 0;
  uint32_t options = 0;
  network::ResourceRequest request;
  mojo::PendingRemote<network::mojom::URLLoaderClient> client;
  net::MutableNetworkTrafficAnnotationTag traffic_annotation;
};

// Asks a resolver for the names a host is an alias of (its CNAME chain, or
// the end of it), and answers with them: none when it has none, or the
// resolver can't say.
class CanonicalNameClient : public network::mojom::ResolveHostClient {
 public:
  using Callback = base::OnceCallback<void(std::vector<std::string>)>;

  static void Resolve(network::mojom::HostResolver* resolver,
                      const GURL& url,
                      const net::NetworkAnonymizationKey& key,
                      bool secure_dns_only,
                      Callback callback) {
    // Deletes itself once answered.
    auto* client = new CanonicalNameClient(std::move(callback));
    auto parameters = network::mojom::ResolveHostParameters::New();
    parameters->dns_query_type = net::DnsQueryType::A;
    if (secure_dns_only) {
      // Chromium's own DNS client, which gives the whole chain: the system's
      // resolver, the only one that gives canonical names, doesn't use DNS
      // over HTTPS.
      parameters->source = net::HostResolverSource::DNS;
    } else {
      parameters->include_canonical_name = true;
    }
    resolver->ResolveHost(network::mojom::HostResolverHost::NewHostPortPair(
                              net::HostPortPair::FromURL(url)),
                          key, std::move(parameters),
                          client->receiver_.BindNewPipeAndPassRemote());
    client->receiver_.set_disconnect_handler(
        base::BindOnce(&CanonicalNameClient::Answer, base::Unretained(client),
                       std::vector<std::string>()));
  }

  // network::mojom::ResolveHostClient:
  void OnComplete(
      int32_t result,
      const net::ResolveErrorInfo& resolve_error_info,
      const net::AddressList& resolved_addresses,
      const net::HostResolverEndpointResults& alternative_endpoints) override {
    Answer(result == net::OK ? resolved_addresses.dns_aliases()
                             : std::vector<std::string>());
  }
  void OnTextResults(const std::vector<std::string>& text_results) override {}
  void OnHostnameResults(const std::vector<net::HostPortPair>& hosts) override {}

 private:
  explicit CanonicalNameClient(Callback callback)
      : callback_(std::move(callback)) {}

  void Answer(std::vector<std::string> names) {
    std::move(callback_).Run(std::move(names));
    delete this;
  }

  Callback callback_;
  mojo::Receiver<network::mojom::ResolveHostClient> receiver_{this};
};

// The factory in front of the network, on the engines' sequence.
class RequestFilter : public network::SelfDeletingURLLoaderFactory {
 public:
  RequestFilter(FilterContext context,
                mojo::PendingReceiver<network::mojom::URLLoaderFactory> receiver,
                mojo::PendingRemote<network::mojom::URLLoaderFactory> target,
                base::SelfDeletingPassKey key)
      : network::SelfDeletingURLLoaderFactory(std::move(receiver), key),
        resolver_(std::move(context.resolver)),
        context_(std::move(context)),
        target_(std::move(target)) {
    target_.set_disconnect_handler(base::BindOnce(
        &RequestFilter::DisconnectReceiversAndDestroy, base::Unretained(this)));
  }

  // network::mojom::URLLoaderFactory:
  void CreateLoaderAndStart(
      mojo::PendingReceiver<network::mojom::URLLoader> loader,
      int32_t request_id,
      uint32_t options,
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const net::MutableNetworkTrafficAnnotationTag& traffic_annotation)
      override {
    RedirectChecker::Begin begin = base::BindRepeating(
        &RequestFilter::Begin, weak_factory_.GetWeakPtr(), request_id, options,
        traffic_annotation);
    if (context_.navigation) {
      RedirectChecker::Start(std::move(loader), request, std::move(client),
                             std::move(begin), RedirectChecker::IsBlocked());
      return;
    }
    if (context_.ads) {
      RedirectChecker::Start(
          std::move(loader), request, std::move(client), std::move(begin),
          base::BindRepeating(&IsRedirectBlocked, context_.page_host,
                              context_.aggressive, context_.frame));
      return;
    }
    begin.Run(std::move(loader), request, std::move(client));
  }

 private:
  // Whether the lists block another request than a navigation, redirected to
  // `url`.
  static bool IsRedirectBlocked(const std::string& page_host,
                                bool aggressive,
                                content::GlobalRenderFrameHostId frame,
                                const network::ResourceRequest& request,
                                const GURL& url) {
    EngineHost::Request engine_request;
    engine_request.url = url;
    engine_request.type = AdblockRequestType(request.resource_type);
    engine_request.page_host = page_host;
    engine_request.aggressive = aggressive;
    engine_request.method = request.method;
    if (!EngineHost::Get().Check(engine_request).block) {
      return false;
    }
    if (frame) {
      ReportBlocked(frame, url);
    }
    return true;
  }

  // A request as the filter has it, new or (a navigation's) redirected.
  void Begin(int32_t request_id,
             uint32_t options,
             const net::MutableNetworkTrafficAnnotationTag& traffic_annotation,
             mojo::PendingReceiver<network::mojom::URLLoader> loader,
             const network::ResourceRequest& request,
             mojo::PendingRemote<network::mojom::URLLoaderClient> client) {
    if (context_.page_host.empty()) {
      // A page's own navigation: as its site says, which the UI thread knows.
      content::GetUIThreadTaskRunner({})->PostTaskAndReplyWithResult(
          FROM_HERE,
          base::BindOnce(&DecidePage, context_.profile, context_.frame,
                         request.url, request.request_initiator),
          base::BindOnce(&RequestFilter::Start, weak_factory_.GetWeakPtr(),
                         std::move(loader), request_id, options, request,
                         std::move(client), traffic_annotation));
      return;
    }
    Decision decision{.ads = context_.ads,
                      .aggressive = context_.aggressive,
                      .accept_language = context_.accept_language};
    if (context_.strip) {
      if (std::optional<GURL> stripped =
              StripTrackingParameters(request.url, request.request_initiator)) {
        decision.rewritten = *stripped;
      }
    }
    Start(std::move(loader), request_id, options, request, std::move(client),
          traffic_annotation, std::move(decision));
  }

  void Start(mojo::PendingReceiver<network::mojom::URLLoader> loader,
             int32_t request_id,
             uint32_t options,
             const network::ResourceRequest& request,
             mojo::PendingRemote<network::mojom::URLLoaderClient> client,
             const net::MutableNetworkTrafficAnnotationTag& traffic_annotation,
             Decision page) {
    if (page.stop) {
      mojo::Remote<network::mojom::URLLoaderClient>(std::move(client))
          ->OnComplete(network::URLLoaderCompletionStatus(net::ERR_ABORTED));
      return;
    }
    if (!page.page.empty()) {
      ServePage(page.page, std::move(client));
      return;
    }
    if (!page.accept_language.empty()) {
      network::ResourceRequest reduced = request;
      reduced.headers.SetHeader(net::HttpRequestHeaders::kAcceptLanguage,
                                page.accept_language);
      page.accept_language.clear();
      Start(std::move(loader), request_id, options, reduced, std::move(client),
            traffic_annotation, std::move(page));
      return;
    }
    if (page.rewritten.is_valid()) {
      Rewrite(std::move(loader), request_id, options, request,
              std::move(client), traffic_annotation, page.rewritten);
      return;
    }
    if (!page.ads) {
      target_->CreateLoaderAndStart(std::move(loader), request_id, options,
                                    request, std::move(client),
                                    traffic_annotation);
      return;
    }
    const bool main_frame =
        request.resource_type ==
        static_cast<int>(blink::mojom::ResourceType::kMainFrame);
    // The Barrier's warning's way on to a page it blocks.
    GURL proceeding;
    if (main_frame && IsProceeding(request.url, &proceeding)) {
      Rewrite(std::move(loader), request_id, options, request,
              std::move(client), traffic_annotation, proceeding);
      return;
    }
    EngineHost::Request engine_request;
    engine_request.url = request.url;
    engine_request.type = AdblockRequestType(request.resource_type);
    engine_request.page_host =
        context_.page_host.empty() ? request.url.host() : context_.page_host;
    engine_request.aggressive = page.aggressive;
    engine_request.method = request.method;
    const EngineHost::Decision decision = EngineHost::Get().Check(engine_request);

    // Pages themselves: warned about with aggressive blocking, forgotten once
    // closed otherwise (domain_block.h).
    if (decision.block && main_frame && !HasProceeded(request.url)) {
      if (page.aggressive) {
        if (context_.frame) {
          ReportBlocked(context_.frame, request.url);
        }
        ServePage(DomainBlockPage(request.url), std::move(client));
        return;
      }
      if (context_.frame) {
        ReportBounce(context_.frame, request.url);
      }
    }
    PendingRequest pending{.loader = std::move(loader),
                           .request_id = request_id,
                           .options = options,
                           .request = request,
                           .client = std::move(client),
                           .traffic_annotation = traffic_annotation};
    if (!decision.block && !main_frame &&
        ShouldUncloak(request.url, engine_request.page_host,
                      page.aggressive)) {
      CanonicalNameClient::Resolve(
          resolver_.get(), request.url, context_.network_anonymization_key,
          context_.secure_dns_only,
          base::BindOnce(&RequestFilter::OnCanonicalName,
                         weak_factory_.GetWeakPtr(), std::move(pending),
                         std::move(engine_request), decision));
      return;
    }
    Finish(std::move(pending), decision, page.aggressive);
  }

  // Whether a request the lists let through is checked again under its
  // host's canonical name: not when it's an address, nor, unless blocking is
  // aggressive, when it's the page's own site's (which standard blocking
  // leaves alone anyway), as Brave.
  bool ShouldUncloak(const GURL& url,
                     const std::string& page_host,
                     bool aggressive) const {
    if (!resolver_.is_bound() || url.HostIsIPAddress() ||
        net::IsLocalhost(url) || !url.has_host()) {
      return false;
    }
    if (aggressive) {
      return true;
    }
    const auto site = [](std::string_view host) {
      std::string domain = net::registry_controlled_domains::GetDomainAndRegistry(
          host, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
      return domain.empty() ? std::string(host) : domain;
    };
    return site(url.host()) != site(page_host);
  }

  void OnCanonicalName(PendingRequest pending,
                       EngineHost::Request engine_request,
                       EngineHost::Decision decision,
                       std::vector<std::string> names) {
    // A few links of the chain at most.
    constexpr size_t kMaxNames = 4;
    for (size_t i = 0; i < names.size() && i < kMaxNames; ++i) {
      std::string& name = names[i];
      if (!name.empty() && name.back() == '.') {
        name.pop_back();
      }
      if (name.empty() || name == pending.request.url.host()) {
        continue;
      }
      GURL::Replacements replacements;
      replacements.SetHostStr(name);
      engine_request.url =
          pending.request.url.ReplaceComponents(replacements);
      if (engine_request.url.is_valid() &&
          EngineHost::Get().Check(engine_request).block) {
        // Blocked as the tracker it is; no stand-in, which would be the
        // tracker's.
        decision = {};
        decision.block = true;
        break;
      }
    }
    Finish(std::move(pending), decision, engine_request.aggressive);
  }

  // What's left once the lists have spoken: blocked, or a stand-in instead,
  // or rewritten, or on to the network.
  void Finish(PendingRequest pending,
              const EngineHost::Decision& decision,
              bool aggressive) {
    const network::ResourceRequest& request = pending.request;
    const bool main_frame =
        request.resource_type ==
        static_cast<int>(blink::mojom::ResourceType::kMainFrame);
    if (decision.block && !main_frame) {
      if (context_.frame) {
        ReportBlocked(context_.frame, request.url);
      }
      if (!decision.redirect.empty()) {
        Serve(request, decision.redirect, std::move(pending.client));
      } else {
        mojo::Remote<network::mojom::URLLoaderClient>(std::move(pending.client))
            ->OnComplete(
                network::URLLoaderCompletionStatus(net::ERR_BLOCKED_BY_CLIENT));
      }
      return;
    }

    if (decision.rewritten_url.is_valid() &&
        decision.rewritten_url != request.url) {
      Rewrite(std::move(pending.loader), pending.request_id, pending.options,
              request, std::move(pending.client), pending.traffic_annotation,
              decision.rewritten_url);
      return;
    }

    mojo::PendingRemote<network::mojom::URLLoaderClient> client =
        std::move(pending.client);
    if (context_.navigation) {
      mojo::PendingRemote<network::mojom::URLLoaderClient> csp_client;
      mojo::MakeSelfOwnedReceiver(
          std::make_unique<CspClient>(request.url, context_.page_host,
                                      aggressive, std::move(client)),
          csp_client.InitWithNewPipeAndPassReceiver());
      client = std::move(csp_client);
    }
    target_->CreateLoaderAndStart(std::move(pending.loader),
                                  pending.request_id, pending.options, request,
                                  std::move(client),
                                  pending.traffic_annotation);
  }

  void Rewrite(mojo::PendingReceiver<network::mojom::URLLoader> loader,
               int32_t request_id,
               uint32_t options,
               const network::ResourceRequest& request,
               mojo::PendingRemote<network::mojom::URLLoaderClient> client,
               const net::MutableNetworkTrafficAnnotationTag& traffic_annotation,
               const GURL& url) {
    mojo::PendingRemote<network::mojom::URLLoaderFactory> target;
    target_->Clone(target.InitWithNewPipeAndPassReceiver());
    RewriteLoader::Start(std::move(loader), request_id, options, request,
                         std::move(client), traffic_annotation,
                         std::move(target), url);
  }

  ~RequestFilter() override = default;

  // Unbound when hosts' canonical names aren't checked.
  mojo::Remote<network::mojom::HostResolver> resolver_;
  const FilterContext context_;
  mojo::Remote<network::mojom::URLLoaderFactory> target_;
  base::WeakPtrFactory<RequestFilter> weak_factory_{this};
};

// The proxies a profile's requests go through, as its settings and the
// system's have them.
class ProxySettings : public base::SupportsUserData::Data,
                      public ProfileObserver {
 public:
  static ProxySettings& Get(Profile* profile) {
    auto* settings = static_cast<ProxySettings*>(profile->GetUserData(&kKey));
    if (!settings) {
      auto owned = std::make_unique<ProxySettings>(profile);
      settings = owned.get();
      profile->SetUserData(&kKey, std::move(owned));
    }
    return *settings;
  }

  explicit ProxySettings(Profile* profile)
      : tracker_(ProxyServiceFactory::CreatePrefProxyConfigTrackerOfProfile(
            profile->GetPrefs(), g_browser_process->local_state(),
            profile->GetProfilePolicyConnector()->policy_service(),
            /*enterprise_proxy_service=*/nullptr)),
        service_(ProxyServiceFactory::CreateProxyConfigService(tracker_.get(),
                                                               profile)) {
    observation_.Observe(profile);
  }

  ~ProxySettings() override { Stop(); }

  // ProfileObserver: stops watching the profile's settings while they're
  // still there.
  void OnProfileWillBeDestroyed(Profile* profile) override { Stop(); }

  // Whether hosts may be looked up apart from requests: DNS queries don't go
  // through proxies, so not when every request does (a single proxy, a
  // fallback, a proxy for HTTPS, or a PAC script that may say so), as Brave.
  bool AllowLookups() {
    if (!service_) {
      return false;
    }
    net::ProxyConfigWithAnnotation config;
    if (service_->GetLatestProxyConfig(&config) !=
        net::ProxyConfigService::CONFIG_VALID) {
      return true;
    }
    const net::ProxyConfig::ProxyRules& rules = config.value().proxy_rules();
    using Type = net::ProxyConfig::ProxyRules::Type;
    return !config.value().has_pac_url() && rules.type != Type::PROXY_LIST &&
           !(rules.type == Type::PROXY_LIST_PER_SCHEME &&
             (!rules.fallback_proxies.IsEmpty() ||
              !rules.proxies_for_https.IsEmpty()));
  }

 private:
  void Stop() {
    observation_.Reset();
    service_.reset();
    if (tracker_) {
      tracker_->DetachFromPrefService();
      tracker_.reset();
    }
  }

  static constexpr int kKey = 0;
  std::unique_ptr<PrefProxyConfigTracker> tracker_;
  std::unique_ptr<net::ProxyConfigService> service_;
  base::ScopedObservation<Profile, ProfileObserver> observation_{this};
};

// The page `frame` is part of, if it's a site's.
GURL PageOf(content::RenderFrameHost* frame) {
  const GURL page = frame->GetOutermostMainFrame()->GetLastCommittedURL();
  return page.SchemeIsHTTPOrHTTPS() ? page : GURL();
}

void OnWebSocketChecked(
    content::GlobalRenderFrameHostId frame_id,
    content::ContentBrowserClient::WebSocketFactory factory,
    const GURL& url,
    const net::SiteForCookies& site_for_cookies,
    const std::optional<std::string>& user_agent,
    mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>
        handshake_client,
    content::ContentBrowserClient::WebSocketOptions options,
    bool block) {
  // Dropping the handshake's client fails the connection.
  content::RenderFrameHost* frame = content::RenderFrameHost::FromID(frame_id);
  if (!frame) {
    return;
  }
  if (block) {
    BarrierTabHelper::OnBlocked(frame_id, url);
    return;
  }
  auto* web_request =
      extensions::BrowserContextKeyedAPIFactory<extensions::WebRequestAPI>::Get(
          frame->GetBrowserContext());
  if (web_request && web_request->MayHaveProxiesForFrame(frame)) {
    web_request->ProxyWebSocket(frame, std::move(factory), url,
                                site_for_cookies, user_agent,
                                std::move(handshake_client),
                                std::move(options.header_client));
    return;
  }
  std::vector<network::mojom::HttpHeaderPtr> headers;
  if (user_agent) {
    headers.push_back(network::mojom::HttpHeader::New(
        net::HttpRequestHeaders::kUserAgent, *user_agent));
  }
  std::move(factory).Run(url, std::move(headers), std::move(handshake_client),
                         mojo::NullRemote(), std::move(options.header_client));
}

void CreateRequestFilter(
    FilterContext context,
    mojo::PendingReceiver<network::mojom::URLLoaderFactory> receiver,
    mojo::PendingRemote<network::mojom::URLLoaderFactory> target) {
  base::MakeSelfDeleting<RequestFilter>(std::move(context), std::move(receiver),
                                        std::move(target));
}

}  // namespace

void MaybeProxyURLLoaderFactory(content::BrowserContext* browser_context,
                                content::RenderFrameHost* frame,
                                int render_process_id,
                                URLLoaderFactoryType type,
                                const url::Origin& request_initiator,
                                const net::IsolationInfo& isolation_info,
                                network::URLLoaderFactoryBuilder& factory_builder) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!IsOn()) {
    return;
  }
  FilterContext context;
  GURL page;
  switch (type) {
    case URLLoaderFactoryType::kNavigation:
      context.navigation = true;
      // A frame's: its page's. A page's own: decided by its address.
      if (frame && frame->GetParentOrOuterDocument()) {
        page = frame->GetOutermostMainFrame()->GetLastCommittedURL();
        context.page_host = page.host();
      }
      break;
    case URLLoaderFactoryType::kDocumentSubResource:
    case URLLoaderFactoryType::kWorkerSubResource:
    case URLLoaderFactoryType::kWorkerMainResource:
    case URLLoaderFactoryType::kServiceWorkerSubResource:
    case URLLoaderFactoryType::kServiceWorkerScript: {
      const url::Origin top = isolation_info.top_frame_origin().value_or(
          request_initiator);
      if (top.opaque() || (top.scheme() != url::kHttpScheme &&
                           top.scheme() != url::kHttpsScheme)) {
        return;
      }
      page = top.GetURL();
      context.page_host = top.host();
      break;
    }
    default:
      return;
  }

  Profile* profile = Profile::FromBrowserContext(browser_context);
  context.profile = profile->GetWeakPtr();
  if (!page.is_empty()) {
    const Settings settings = GetSettings(profile, page);
    context.ads = settings.AdsOn();
    context.aggressive = settings.ads == AdBlocking::kAggressive;
    context.strip =
        settings.enabled && IsPrivacyOn(profile, kStripParamsPref);
    context.accept_language = ReducedAcceptLanguage(profile, settings);
    if (!context.ads && !context.strip && context.accept_language.empty()) {
      return;
    }
  }
  if (frame) {
    context.frame = frame->GetGlobalId();
  }
  // Hosts' canonical names, from the container's resolver, unless the
  // requests go through a proxy.
  if (context.ads && ProxySettings::Get(profile).AllowLookups()) {
    content::RenderProcessHost* process =
        frame ? frame->GetProcess()
              : content::RenderProcessHost::FromID(render_process_id);
    content::StoragePartition* partition =
        process ? process->GetStoragePartition()
                : browser_context->GetDefaultStoragePartition();
    partition->GetNetworkContext()->CreateHostResolver(
        std::nullopt, context.resolver.InitWithNewPipeAndPassReceiver());
    context.network_anonymization_key =
        isolation_info.network_anonymization_key();
    context.secure_dns_only =
        SystemNetworkContextManager::GetStubResolverConfigReader()
            ->GetSecureDnsConfiguration(
                /*force_check_parental_controls_for_automatic_mode=*/false)
            .mode() == net::SecureDnsMode::kSecure;
  }

  auto [receiver, target] = factory_builder.Append();
  EngineHost::Get().task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&CreateRequestFilter, std::move(context),
                                std::move(receiver), std::move(target)));
}

bool WillFilterWebSocket(content::RenderFrameHost* frame) {
  if (!frame || !IsOn()) {
    return false;
  }
  const GURL page = PageOf(frame);
  return page.is_valid() &&
         GetSettings(Profile::FromBrowserContext(frame->GetBrowserContext()),
                     page)
             .AdsOn();
}

void FilterWebSocket(
    content::RenderFrameHost* frame,
    content::ContentBrowserClient::WebSocketFactory factory,
    const GURL& url,
    const net::SiteForCookies& site_for_cookies,
    const std::optional<std::string>& user_agent,
    mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>
        handshake_client,
    content::ContentBrowserClient::WebSocketOptions options) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  const GURL page = PageOf(frame);
  EngineHost::Request request;
  request.url = url;
  request.type = "websocket";
  request.page_host = page.host();
  request.aggressive =
      GetSettings(Profile::FromBrowserContext(frame->GetBrowserContext()), page)
          .ads == AdBlocking::kAggressive;
  EngineHost::Get().task_runner()->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](const EngineHost::Request& request) {
            return EngineHost::Get().Check(request).block;
          },
          std::move(request)),
      base::BindOnce(&OnWebSocketChecked, frame->GetGlobalId(),
                     std::move(factory), url, site_for_cookies, user_agent,
                     std::move(handshake_client), std::move(options)));
}

}  // namespace oblada::barrier
