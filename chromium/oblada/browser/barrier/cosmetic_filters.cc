#include "oblada/browser/barrier/cosmetic_filters.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/document_service.h"
#include "content/public/browser/document_user_data.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_handle_user_data.h"
#include "content/public/browser/navigation_throttle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "oblada/browser/barrier/barrier.h"
#include "oblada/browser/barrier/engine_host.h"
#include "oblada/browser/barrier/farbling.h"
#include "oblada/browser/barrier/filter_lists.h"
#include "oblada/browser/l10n.h"
#include "oblada/common/barrier/barrier.mojom.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "url/gurl.h"

namespace oblada::barrier {

namespace {

// The address of the page (top-level frame) a navigation's document is part
// of.
GURL PageUrl(content::NavigationHandle* navigation) {
  content::RenderFrameHost* parent =
      navigation->GetParentFrameOrOuterDocument();
  return parent ? parent->GetOutermostMainFrame()->GetLastCommittedURL()
                : navigation->GetURL();
}

Profile* ProfileOf(content::NavigationHandle* navigation) {
  return Profile::FromBrowserContext(
      navigation->GetWebContents()->GetBrowserContext());
}

// A navigation's cosmetic filters, once the engines gave them.
class CosmeticResourcesData
    : public content::NavigationHandleUserData<CosmeticResourcesData> {
 public:
  ~CosmeticResourcesData() override = default;

  EngineHost::CosmeticResources resources;

 private:
  friend class content::NavigationHandleUserData<CosmeticResourcesData>;
  explicit CosmeticResourcesData(content::NavigationHandle& navigation) {}

  NAVIGATION_HANDLE_USER_DATA_KEY_DECL();
};

NAVIGATION_HANDLE_USER_DATA_KEY_IMPL(CosmeticResourcesData);

// Holds a document's response until its cosmetic filters are there: the
// engines take a moment, and the scriptlets must run before the page's own
// scripts.
class CosmeticFiltersThrottle : public content::NavigationThrottle {
 public:
  using content::NavigationThrottle::NavigationThrottle;

  ThrottleCheckResult WillProcessResponse() override {
    content::NavigationHandle* navigation = navigation_handle();
    if (!navigation->GetURL().SchemeIsHTTPOrHTTPS() ||
        !GetSettings(ProfileOf(navigation), PageUrl(navigation)).AdsOn()) {
      return PROCEED;
    }
    EngineHost::Get().GetCosmeticResources(
        navigation->GetURL(),
        base::BindOnce(&CosmeticFiltersThrottle::OnResources,
                       weak_factory_.GetWeakPtr()));
    return DEFER;
  }

  const char* GetNameForLogging() override {
    return "ObladaCosmeticFiltersThrottle";
  }

 private:
  void OnResources(EngineHost::CosmeticResources resources) {
    CosmeticResourcesData::GetOrCreateForNavigationHandle(*navigation_handle())
        ->resources = std::move(resources);
    Resume();
  }

  base::WeakPtrFactory<CosmeticFiltersThrottle> weak_factory_{this};
};

// The page the browser started the element picker on, which may then add the
// one filter the picker makes, once.
class ElementPickerSession
    : public content::DocumentUserData<ElementPickerSession> {
 public:
  ~ElementPickerSession() override = default;

 private:
  explicit ElementPickerSession(content::RenderFrameHost* frame)
      : content::DocumentUserData<ElementPickerSession>(frame) {}
  friend content::DocumentUserData<ElementPickerSession>;
  DOCUMENT_USER_DATA_KEY_DECL();
};

DOCUMENT_USER_DATA_KEY_IMPL(ElementPickerSession);

// A document's questions to the browser.
class BarrierHostImpl : public content::DocumentService<oblada::mojom::BarrierHost> {
 public:
  BarrierHostImpl(content::RenderFrameHost& frame,
                  mojo::PendingReceiver<oblada::mojom::BarrierHost> receiver)
      : content::DocumentService<oblada::mojom::BarrierHost>(frame,
                                                             std::move(receiver)) {}

  // oblada::mojom::BarrierHost:
  void HiddenClassIdSelectors(
      const std::vector<std::string>& classes,
      const std::vector<std::string>& ids,
      const std::vector<std::string>& exceptions,
      HiddenClassIdSelectorsCallback callback) override {
    // Only for pages whose site blocks ads, with cosmetic filters then.
    if (!GetSettings(Profile::FromBrowserContext(
                         render_frame_host().GetBrowserContext()),
                     render_frame_host().GetOutermostMainFrame()
                         ->GetLastCommittedURL())
             .AdsOn()) {
      std::move(callback).Run({});
      return;
    }
    EngineHost::Get().GetHiddenClassIdSelectors(
        classes, ids, exceptions,
        base::BindOnce(
            [](HiddenClassIdSelectorsCallback callback,
               std::vector<std::string> selectors) {
              std::move(callback).Run(selectors);
            },
            std::move(callback)));
  }

  void AddCustomFilter(const std::string& filter) override {
    // Only from the page the browser started the picker on (a web page's main
    // frame), once: any other document could otherwise add rules, for every
    // site when it has no host (about:blank, data:, blob:).
    content::RenderFrameHost& frame = render_frame_host();
    const GURL& url = frame.GetLastCommittedURL();
    if (!frame.IsInPrimaryMainFrame() || !url.SchemeIsHTTPOrHTTPS() ||
        url.host().empty() ||
        !ElementPickerSession::GetForCurrentDocument(&frame)) {
      return;
    }
    ElementPickerSession::DeleteForCurrentDocument(&frame);
    // One cosmetic rule for the page's own host, as the element picker makes
    // them.
    const std::string host(url.host());
    if (filter.find_first_of("\r\n") != std::string::npos ||
        !filter.starts_with(host + "##") || filter.size() > 2000) {
      return;
    }
    filter_lists::AddCustomFilter(filter);
  }
};

}  // namespace

void MaybeAddCosmeticFiltersThrottle(
    content::NavigationThrottleRegistry& registry) {
  if (IsOn()) {
    registry.AddThrottle(std::make_unique<CosmeticFiltersThrottle>(registry));
  }
}

void SendPageSettings(content::NavigationHandle* navigation) {
  content::RenderFrameHost* frame = navigation->GetRenderFrameHost();
  if (!frame || navigation->IsSameDocument()) {
    return;
  }
  auto settings = oblada::mojom::BarrierPageSettings::New();
  const GURL page = PageUrl(navigation);
  const Settings barrier = GetSettings(ProfileOf(navigation), page);
  if (barrier.AdsOn()) {
    if (auto* data = CosmeticResourcesData::GetForNavigationHandle(*navigation)) {
      EngineHost::CosmeticResources& resources = data->resources;
      settings->cosmetic_filtering = true;
      settings->aggressive = barrier.ads == AdBlocking::kAggressive;
      settings->hide_selectors = std::move(resources.hide_selectors);
      settings->procedural_actions = std::move(resources.procedural_actions);
      settings->exceptions = std::move(resources.exceptions);
      settings->generic_hide = resources.generichide;
      settings->scriptlets = std::move(resources.injected_script);
    }
  }
  const FarblingSettings farbling =
      FarblingFor(ProfileOf(navigation), frame->GetStoragePartition(), page);
  settings->farbling_token = farbling.token;
  settings->farbling_exceptions = farbling.exceptions;
  settings->reduce_language = farbling.reduce_language;

  mojo::AssociatedRemote<oblada::mojom::BarrierAgent> agent;
  frame->GetRemoteAssociatedInterfaces()->GetInterface(&agent);
  agent->SetPageSettings(std::move(settings));
}

void BindBarrierHost(content::RenderFrameHost* frame,
                     mojo::PendingReceiver<oblada::mojom::BarrierHost> receiver) {
  new BarrierHostImpl(*frame, std::move(receiver));
}

bool CanPickElement(content::WebContents* contents) {
  const GURL& url = contents->GetLastCommittedURL();
  return IsOn() && url.SchemeIsHTTPOrHTTPS() && !SiteOf(url).empty() &&
         GetSettings(Profile::FromBrowserContext(contents->GetBrowserContext()),
                     url)
             .enabled;
}

void StartElementPicker(content::WebContents* contents) {
  // Again: the page may have changed since the menu was shown.
  if (!CanPickElement(contents)) {
    return;
  }
  ElementPickerSession::CreateForCurrentDocument(contents->GetPrimaryMainFrame());
  base::flat_map<std::string, std::string> strings;
  for (const char* key :
       {"hint", "block", "cancel", "wider", "narrower", "count"}) {
    strings[key] = L10nUTF8(base::StrCat({"barrier.picker.", key}));
  }
  mojo::AssociatedRemote<oblada::mojom::BarrierAgent> agent;
  contents->GetPrimaryMainFrame()->GetRemoteAssociatedInterfaces()->GetInterface(
      &agent);
  agent->StartElementPicker(std::move(strings));
}

}  // namespace oblada::barrier
