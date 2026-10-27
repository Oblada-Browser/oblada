#ifndef OBLADA_BROWSER_BARRIER_REQUEST_FILTER_H_
#define OBLADA_BROWSER_BARRIER_REQUEST_FILTER_H_

#include <optional>
#include <string>

#include "content/public/browser/content_browser_client.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "services/network/public/mojom/websocket.mojom-forward.h"

class GURL;

namespace content {
class BrowserContext;
class RenderFrameHost;
}  // namespace content

namespace net {
class IsolationInfo;
class SiteForCookies;
}  // namespace net

namespace network {
class URLLoaderFactoryBuilder;
}  // namespace network

namespace url {
class Origin;
}  // namespace url

// The Barrier between pages and the network: every request of a page whose
// site blocks trackers and ads (its frames', its workers') goes through it, as
// through Brave's proxying URLLoaderFactory, on the engines' sequence
// (engine_host.h), which says what to do with it:
// - blocked (net::ERR_BLOCKED_BY_CLIENT), counted for its tab (tab_helper.h);
// - answered with a harmless stand-in instead, a script that does nothing,
//   a transparent image… ($redirect);
// - sent without the parameters $removeparam rules strip, or the tracking
//   parameters Brave's list has, through a redirect; pages' own navigations
//   go past tracking redirects (url_cleaning.h);
// - for documents, with the Content-Security-Policy $csp rules add.
// Requests the lists let through are checked again under their host's
// canonical name (CNAME uncloaking, as Brave): a tracker can hide behind a
// site's own subdomain. The rest go on as they were, to the network. Frames' WebSocket connections
// are checked too, before they open.
namespace oblada::barrier {

// chrome_content_browser_client.cc (WillCreateURLLoaderFactory): puts the
// Barrier in front of the factory being built, when it has something to do.
void MaybeProxyURLLoaderFactory(
    content::BrowserContext* browser_context,
    content::RenderFrameHost* frame,
    int render_process_id,
    content::ContentBrowserClient::URLLoaderFactoryType type,
    const url::Origin& request_initiator,
    const net::IsolationInfo& isolation_info,
    network::URLLoaderFactoryBuilder& factory_builder);

// chrome_content_browser_client.cc (WillInterceptWebSocket): whether the
// Barrier checks `frame`'s WebSocket connections.
bool WillFilterWebSocket(content::RenderFrameHost* frame);

// chrome_content_browser_client.cc (CreateWebSocket): the connection is
// dropped if the filter lists block it, and counted for its tab; otherwise it
// goes on, through extensions' WebRequest when they may want it, as Chromium
// sends it.
void FilterWebSocket(
    content::RenderFrameHost* frame,
    content::ContentBrowserClient::WebSocketFactory factory,
    const GURL& url,
    const net::SiteForCookies& site_for_cookies,
    const std::optional<std::string>& user_agent,
    mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>
        handshake_client,
    content::ContentBrowserClient::WebSocketOptions options);

}  // namespace oblada::barrier

#endif  // OBLADA_BROWSER_BARRIER_REQUEST_FILTER_H_
