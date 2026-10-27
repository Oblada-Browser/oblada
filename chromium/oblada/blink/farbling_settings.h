#ifndef OBLADA_BLINK_FARBLING_SETTINGS_H_
#define OBLADA_BLINK_FARBLING_SETTINGS_H_

#include <cstdint>

namespace blink {

// An execution context's fingerprinting protection (farbling.h), as the
// browser sends it to documents (oblada/common/barrier/barrier.mojom) and
// they pass it on to their workers.
struct ObladaFarblingSettings {
  // The seed: the page's site's, for the session and container. Zero: no
  // protection at all.
  uint64_t token = 0;
  // The APIs left alone (barrier.h's FingerprintingApi bits).
  uint32_t exceptions = 0;
  // Only the first language, and fewer fonts.
  bool reduce_language = false;
};

}  // namespace blink

#endif  // OBLADA_BLINK_FARBLING_SETTINGS_H_
