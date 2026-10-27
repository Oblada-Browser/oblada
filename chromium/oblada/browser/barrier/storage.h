#ifndef OBLADA_BROWSER_BARRIER_STORAGE_H_
#define OBLADA_BROWSER_BARRIER_STORAGE_H_

class GURL;

namespace content {
class WebContents;
}  // namespace content

// What the Barrier erases as sites' tabs close, as Brave does: a while after
// a site's last tab (in a container: its last tab in that container) closes or
// goes elsewhere,
// - what third parties stored under it (their partitioned cookies and
//   storage), when it blocks third-party cookies: third parties keep nothing
//   from one visit to the next ("ephemeral storage");
// - everything the site itself stored, when it's set to be forgotten
//   ("Forget me when I close this site"), or when it was opened as a tracker
//   the lists block, with nothing stored before ("unlinkable bouncing").
namespace oblada::barrier {

// tab_helper.cc: `contents` leaves the page at `page_url` (it goes elsewhere,
// or the tab closes).
void OnPageLeft(content::WebContents* contents, const GURL& page_url);

// Forgets `page_url`'s site once it's closed in `contents`' container, this
// session, as if it were set to be, unless it has cookies there already (a
// tracker opened as a page: domain_block.h).
void ForgetUnlessKnown(content::WebContents* contents, const GURL& page_url);

}  // namespace oblada::barrier

#endif  // OBLADA_BROWSER_BARRIER_STORAGE_H_
