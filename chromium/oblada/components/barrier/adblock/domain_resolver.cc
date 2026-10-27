#include "oblada/components/barrier/adblock/domain_resolver.h"

#include <string>
#include <string_view>

#include "net/base/registry_controlled_domains/registry_controlled_domain.h"

namespace oblada::barrier::adblock::ffi {

void ResolveDomainPosition(rust::Str host, size_t& start, size_t& end) {
  const std::string_view view(host.data(), host.size());
  const std::string domain =
      net::registry_controlled_domains::GetDomainAndRegistry(
          view, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  // `domain` is the end of `host`.
  start = domain.empty() ? 0 : view.size() - domain.size();
  end = view.size();
}

}  // namespace oblada::barrier::adblock::ffi
