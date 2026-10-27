#ifndef OBLADA_BROWSER_BARRIER_DOMAIN_BLOCK_H_
#define OBLADA_BROWSER_BARRIER_DOMAIN_BLOCK_H_

#include <string>
#include <string_view>

class GURL;

// Pages the filter lists block outright (`||tracker.example^`), opened as
// pages, as Brave handles them (request_filter.h asks here):
// - with aggressive blocking, the Barrier's warning instead of the page, which
//   can go back, or on to the page (for the session);
// - otherwise, the page opens, and its site is forgotten once closed, if it
//   had stored nothing before (storage.h, "unlinkable bouncing").
namespace oblada::barrier {

// Prepares the warning in the browser's language (on the UI thread, before
// pages load).
void InitDomainBlock();

// The warning's page for `url` (any sequence).
std::string DomainBlockPage(const GURL& url);

// Whether `url` is the warning's way on to a page (its "Open anyway"):
// `original` gets the page's address. Its site isn't warned about again this
// session (any sequence, the engines').
bool IsProceeding(const GURL& url, GURL* original);
// Whether the user went on to `url`'s site already, this session.
bool HasProceeded(const GURL& url);

// A token of the session's, which the Barrier's pages put in their answers
// (pages can't guess it).
const std::string& SessionToken();

// `url` as a JavaScript string literal, for the Barrier's pages' scripts:
// nothing in it breaks out.
std::string JavaScriptUrl(const GURL& url);

// `url` without its query parameter `name` (and without a "?" left alone).
GURL WithoutQueryParameter(const GURL& url, std::string_view name);

}  // namespace oblada::barrier

#endif  // OBLADA_BROWSER_BARRIER_DOMAIN_BLOCK_H_
