#ifndef OBLADA_BROWSER_BARRIER_FARBLING_H_
#define OBLADA_BROWSER_BARRIER_FARBLING_H_

#include <cstdint>

class GURL;
class Profile;

namespace blink {
class StorageKey;
}  // namespace blink

namespace content {
class BrowserContext;
class StoragePartition;
}  // namespace content

// The Barrier's fingerprinting protection ("farbling", as Brave calls it): Blink
// changes what pages could recognize the device by (canvas and audio readouts
// slightly, hardware details, fonts…, oblada/blink/farbling.h), the same way
// for a site through a session, differently from one site to another, from one
// container to another and from one session to the next. The way is a seed,
// sent to each document as it commits (cosmetic_filters.h), and to shared and
// service workers as they start (GetWorkerFarbling()); dedicated workers and
// worklets get their document's.
namespace oblada::barrier {

// What a page's documents and workers get.
struct FarblingSettings {
  // From a key of the profile's made anew at each start, the page's site and
  // the container. Zero: no protection.
  uint64_t token = 0;
  // The FingerprintingApi bits (barrier.h) left alone: the site's own, and
  // those Brave's list of exceptions leaves alone for it to keep working
  // (lists/webcompat-exceptions.json).
  uint32_t exceptions = 0;
  // Only the first language, and fewer fonts.
  bool reduce_language = false;
};

// What documents and workers of `partition` (a container's, or the profile's
// own) get on the page at `page_url` (its top-level frame's address), as the
// site's settings say.
FarblingSettings FarblingFor(Profile* profile,
                             content::StoragePartition* partition,
                             const GURL& page_url);

// content::ContentBrowserClient::GetObladaWorkerFarbling(): what a shared or
// service worker of `storage_key` gets, its page being its top-level site.
void GetWorkerFarbling(content::BrowserContext* browser_context,
                       content::StoragePartition* partition,
                       const blink::StorageKey& storage_key,
                       uint64_t* token,
                       uint32_t* exceptions,
                       bool* reduce_language);

}  // namespace oblada::barrier

#endif  // OBLADA_BROWSER_BARRIER_FARBLING_H_
