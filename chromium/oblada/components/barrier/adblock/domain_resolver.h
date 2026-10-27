#ifndef OBLADA_COMPONENTS_BARRIER_ADBLOCK_DOMAIN_RESOLVER_H_
#define OBLADA_COMPONENTS_BARRIER_ADBLOCK_DOMAIN_RESOLVER_H_

#include <cstddef>

#include "third_party/rust/cxx/v1/cxx.h"

namespace oblada::barrier::adblock::ffi {

// For adblock-rust (lib.rs), which parses URLs without a public suffix list of
// its own: where `host`'s registrable domain (eTLD+1) is in it, from
// Chromium's list. The whole host when it has none (an IP address, a host
// without a dot).
void ResolveDomainPosition(rust::Str host, size_t& start, size_t& end);

}  // namespace oblada::barrier::adblock::ffi

#endif  // OBLADA_COMPONENTS_BARRIER_ADBLOCK_DOMAIN_RESOLVER_H_
