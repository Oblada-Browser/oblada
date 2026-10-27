#include "oblada/blink/connection_pools.h"

#include <map>
#include <utility>

#include "base/no_destructor.h"
#include "base/notreached.h"
#include "base/synchronization/lock.h"
#include "oblada/blink/farbling.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/core/execution_context/security_context.h"
#include "third_party/blink/renderer/core/frame/frame.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/platform/weborigin/security_origin.h"

namespace blink::oblada_farbling {

namespace {

// Brave's limits.
int Limit(ConnectionPool pool) {
  switch (pool) {
    case ConnectionPool::kWebSocket:
      return 30;
    case ConnectionPool::kEventSource:
      return 250;
  }
  NOTREACHED();
}

// The connections open by pool and site, for windows and workers alike.
class Pools {
 public:
  static Pools& Get() {
    static base::NoDestructor<Pools> pools;
    return *pools;
  }

  bool Take(ConnectionPool pool, const std::string& site) {
    base::AutoLock lock(lock_);
    int& count = counts_[{pool, site}];
    if (count >= Limit(pool)) {
      return false;
    }
    ++count;
    return true;
  }

  void Release(ConnectionPool pool, const std::string& site) {
    base::AutoLock lock(lock_);
    auto it = counts_.find({pool, site});
    if (it != counts_.end() && --it->second <= 0) {
      counts_.erase(it);
    }
  }

 private:
  base::Lock lock_;
  std::map<std::pair<ConnectionPool, std::string>, int> counts_
      GUARDED_BY(lock_);
};

// The site whose pool `context`'s connections count against: its page's for
// a window, its own for a worker.
std::string SiteOf(ExecutionContext* context) {
  const SecurityOrigin* origin = context->GetSecurityOrigin();
  if (auto* window = DynamicTo<LocalDOMWindow>(context)) {
    if (LocalFrame* frame = window->GetFrame()) {
      if (const SecurityContext* top = frame->Top()->GetSecurityContext()) {
        origin = top->GetSecurityOrigin();
      }
    }
  }
  if (!origin) {
    return std::string();
  }
  origin = origin->GetOriginOrPrecursorOriginIfOpaque();
  String site = origin->RegistrableDomain();
  if (site.empty()) {
    site = origin->Host();
  }
  return site.Utf8();
}

}  // namespace

PooledConnection::PooledConnection(ConnectionPool pool, std::string site)
    : pool_(pool), site_(std::move(site)) {}

PooledConnection::~PooledConnection() {
  Pools::Get().Release(pool_, site_);
}

bool TakeConnection(ExecutionContext* context,
                    ConnectionPool pool,
                    std::unique_ptr<PooledConnection>* place) {
  if (*place || !IsOn(context, kConnectionPools)) {
    return true;
  }
  std::string site = SiteOf(context);
  if (!Pools::Get().Take(pool, site)) {
    return false;
  }
  *place = std::make_unique<PooledConnection>(pool, std::move(site));
  return true;
}

}  // namespace blink::oblada_farbling
