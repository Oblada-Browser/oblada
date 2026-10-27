#ifndef OBLADA_BLINK_CONNECTION_POOLS_H_
#define OBLADA_BLINK_CONNECTION_POOLS_H_

#include <memory>
#include <string>

#include "third_party/blink/renderer/core/core_export.h"

namespace blink {

class ExecutionContext;

// Limits to the WebSockets and EventSource connections a site may have open at
// once, part of the Barrier's fingerprinting protection (farbling.h's
// kConnectionPools), as Brave has them: a site that took every connection the
// browser allows could tell when another site opens one, and the two could
// then talk across sites. The site is the page's (its top-level frame's), or
// the worker's own, counted in each renderer process.
namespace oblada_farbling {

enum class ConnectionPool {
  kWebSocket,
  kEventSource,
};

// A place in a site's pool: the connection counts against it as long as this
// lives.
class CORE_EXPORT PooledConnection {
 public:
  PooledConnection(ConnectionPool pool, std::string site);
  PooledConnection(const PooledConnection&) = delete;
  PooledConnection& operator=(const PooledConnection&) = delete;
  ~PooledConnection();

 private:
  const ConnectionPool pool_;
  const std::string site_;
};

// Takes a place in `pool` for a connection of `context`, into `place`: false
// when the pool is full, and the connection must fail. Nothing is counted
// when `context` doesn't limit its connections, or `place` already holds one
// (a connection that reconnects).
CORE_EXPORT bool TakeConnection(ExecutionContext* context,
                                ConnectionPool pool,
                                std::unique_ptr<PooledConnection>* place);

}  // namespace oblada_farbling

}  // namespace blink

#endif  // OBLADA_BLINK_CONNECTION_POOLS_H_
