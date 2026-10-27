#ifndef OBLADA_BROWSER_BARRIER_URL_CLEANING_H_
#define OBLADA_BROWSER_BARRIER_URL_CLEANING_H_

#include <optional>

class GURL;

namespace url {
class Origin;
}  // namespace url

// Addresses cleaned before they're requested, as Brave does (request_filter.h
// applies these):
// - tracking parameters stripped (fbclid, gclid, utm_… as Brave's list has
//   them: lists/query-filter.json), on requests from another site, or typed;
// - tracking redirects skipped: a page of one site that only redirects to
//   another, with where it leads in its address, is left for where it leads
//   ("debouncing", lists/debounce.json), and so are AMP caches' copies of
//   pages, for the original.
// Both work on any sequence (their lists are read once).
namespace oblada::barrier {

// `url` without the tracking parameters Brave's list names, when requested
// from `initiator` (none: typed, or a bookmark); nothing if it has none, or
// `initiator` is its own site.
std::optional<GURL> StripTrackingParameters(
    const GURL& url,
    const std::optional<url::Origin>& initiator);

// Where `url` leads, when it's a tracking redirect (or an AMP cache's copy,
// with `de_amp`) of another site; nothing otherwise.
std::optional<GURL> DebounceTarget(const GURL& url, bool de_amp);

}  // namespace oblada::barrier

#endif  // OBLADA_BROWSER_BARRIER_URL_CLEANING_H_
