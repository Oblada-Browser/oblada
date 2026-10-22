#ifndef OBLADA_BROWSER_CLEAN_LINKS_CLEAN_LINKS_H_
#define OBLADA_BROWSER_CLEAN_LINKS_CLEAN_LINKS_H_

class GURL;
class Profile;

// Links copied without the parameters sites add to them to track who follows
// them (utm_source, fbclid, a shop's referral codes…), as Firefox's "Copy Clean
// Link" copies them, from Firefox's lists (lists/README.md): its global ones,
// for every site, and some sites' own, and Mozilla's list, kept up to date from
// Mozilla's server (mozilla_list.h).
// - Links on pages have "Copy Clean Link" next to "Copy Link Address"
//   (render_view_context_menu.cc), and the address bar has it next to "Copy"
//   (omnibox_context_menu_mixin.cc).
// - Selected text has "Copy Clean Link to Highlight" next to "Copy Link to
//   Highlight" (link_to_text_menu_observer.cc).
// - "Copy link" (IDC_COPY_URL, the site panel's) copies the page's link clean
//   (browser_commands.cc).
namespace oblada::clean_links {

// The link pages' and the address bar's menus offer to copy clean.
inline constexpr int kCopyCleanLinkMenuId = 55680;
inline constexpr int kCopyCleanLinkToHighlightMenuId = 55681;

// Whether `url` is a link Oblada cleans (a web page's), with or without
// tracking parameters.
bool IsCleanable(const GURL& url);

// Whether `url` has tracking parameters, its own or a link's it carries (as
// a redirect's ?url=…).
bool HasTracking(const GURL& url);

// `url` without its tracking parameters, nor those of a link it carries, and
// otherwise as it was (its other parameters in their order, its fragment);
// `url` itself if it has none.
GURL Clean(const GURL& url);

// Copies `url` clean, as text, for pasting (off the record for an incognito
// `profile`).
void CopyClean(const GURL& url, Profile* profile);

}  // namespace oblada::clean_links

#endif  // OBLADA_BROWSER_CLEAN_LINKS_CLEAN_LINKS_H_
