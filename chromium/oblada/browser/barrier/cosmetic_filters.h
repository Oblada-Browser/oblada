#ifndef OBLADA_BROWSER_BARRIER_COSMETIC_FILTERS_H_
#define OBLADA_BROWSER_BARRIER_COSMETIC_FILTERS_H_

#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "oblada/common/barrier/barrier.mojom-forward.h"

namespace content {
class NavigationHandle;
class NavigationThrottleRegistry;
class RenderFrameHost;
class WebContents;
}  // namespace content

// What the Barrier sends each document as it commits (oblada.mojom.BarrierAgent,
// which renderer/barrier receives): the cosmetic filters of filter lists (the
// elements to hide, and the scriptlets to run), fetched from the engines as its
// response arrives; and its fingerprinting protection (farbling.h). And what
// documents ask in return (oblada.mojom.BarrierHost): the generic rules the
// classes and ids of their elements call for, and the element picker's
// filters, which join the user's own (filter_lists.h).
namespace oblada::barrier {

// chrome_content_browser_client_navigation_throttles.cc: holds a document's
// response until its cosmetic filters are there.
void MaybeAddCosmeticFiltersThrottle(
    content::NavigationThrottleRegistry& registry);

// tab_helper.cc, as a navigation is about to commit: sends its document what
// it gets.
void SendPageSettings(content::NavigationHandle* navigation);

// chrome_browser_interface_binders.cc.
void BindBarrierHost(
    content::RenderFrameHost* frame,
    mojo::PendingReceiver<oblada::mojom::BarrierHost> receiver);

// The page's context menu's "Block an element" (render_view_context_menu.cc),
// which starts the element picker.
inline constexpr int kBlockElementMenuId = 55690;

// Whether the element picker can run on the page in `contents`: a site's, with
// the Barrier up.
bool CanPickElement(content::WebContents* contents);

// Lets the user pick an element of the page in `contents` to hide, for good:
// a filter for it joins their own (filter_lists.h).
void StartElementPicker(content::WebContents* contents);

}  // namespace oblada::barrier

#endif  // OBLADA_BROWSER_BARRIER_COSMETIC_FILTERS_H_
