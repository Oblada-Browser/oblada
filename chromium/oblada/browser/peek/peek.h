#ifndef OBLADA_BROWSER_PEEK_PEEK_H_
#define OBLADA_BROWSER_PEEK_PEEK_H_

class BrowserWindowInterface;
class GURL;

namespace content {
class WebContents;
}  // namespace content

// Zen's Glance (Arc's Peek): a link Option-clicked, or force-clicked on the
// trackpad, opens in a card over its page, dimmed, rather than in place of it or
// in a tab. In the card the page can be browsed; the buttons next to it make it a
// tab of its own (or one side by side with its page), or close it, as Escape,
// Command-W, a click on the page around it or another tab do. It opens in its
// page's container.
namespace oblada::peek {

// Starts watching every window's tabs for links to peek at.
void Init();

// The link `source` shows under the mouse (as its status bubble does), or none
// (browser_web_contents_delegate.cc).
void SetHoveredLink(content::WebContents* source, const GURL& url);

// For a force click in `source`: peeks at the link under the mouse, if there's
// one, and returns whether there was (else the system looks up the word there).
// The rest of that click (moves, the release) doesn't reach the page then: it
// would follow the link, or drag it.
bool PeekAtHoveredLink(content::WebContents* source);

// Whether a peek is open over `source` (which then takes no drops).
bool IsPeekedFrom(content::WebContents* source);

// The page in an open peek whose tab id, as extensions know it, is `tab_id`, if
// there's one. A peek's page loads with a tab's helpers, as a page prerendered
// for a tab, so that extensions treat it as the tab it may become: their tabs
// APIs find it as they find a prerendered one, a tab of no window
// (extension_tab_util.cc), and can run scripts in it (tabs_api.cc).
content::WebContents* FindPeekByTabId(int tab_id);

// Makes the peek open in `browser`'s window a tab of its own, as its button does
// (for debugging, window_snapshots.cc).
void OpenPeekAsTab(BrowserWindowInterface* browser);

}  // namespace oblada::peek

#endif  // OBLADA_BROWSER_PEEK_PEEK_H_
