#ifndef OBLADA_COMPONENTS_BARRIER_ADBLOCK_ADBLOCK_ENGINE_H_
#define OBLADA_COMPONENTS_BARRIER_ADBLOCK_ADBLOCK_ENGINE_H_

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "third_party/rust/cxx/v1/cxx.h"

namespace oblada::barrier::adblock {

namespace ffi {
struct EngineHandle;
}  // namespace ffi

// One of Brave's ad-blocking engines (adblock-rust, lib.rs), compiled from
// filter lists in EasyList's and uBlock Origin's syntax. Not thread-safe: it's
// used on one sequence (barrier/engine_host.h).
class Engine {
 public:
  struct List {
    std::string text;
    // Which privileged scriptlets the list's rules may inject (kTrusted).
    uint8_t permissions = 0;
  };
  // uBlock Origin's trusted scriptlets (cookie and storage setters, and the
  // like), for the lists Brave trusts with them.
  static constexpr uint8_t kTrusted = 1;

  // A request, as adblock-rust matches it.
  struct Request {
    std::string_view url;
    std::string_view hostname;
    // The host of the frame making the request.
    std::string_view source_hostname;
    // adblock-rust's names of resource types: "script", "image", "sub_frame"…
    std::string_view type;
    // Whether the request's site (eTLD+1) isn't the source's.
    bool third_party = true;
    std::string_view method = "GET";
    // What another engine found already (Brave keeps several, see
    // engine_host.cc): a blocking rule matched, so only exceptions are
    // looked for; or exceptions are looked for even without one.
    bool previously_matched_rule = false;
    bool force_check_exceptions = false;
  };

  struct Match {
    // A blocking rule matched, and no exception overruled it.
    bool should_block = false;
    // A blocking rule matched, exceptions or not.
    bool matched = false;
    bool has_exception = false;
    // The rule was $important: other engines' exceptions don't apply.
    bool important = false;
    // What to answer with instead, a data: URL, for a $redirect rule.
    std::string redirect;
    // The URL without the parameters $removeparam rules remove, if any.
    std::string rewritten_url;
  };

  static std::unique_ptr<Engine> FromLists(const std::vector<List>& lists);
  // From Serialize()'s data; null if it isn't (from another version of the
  // engine, or damaged).
  static std::unique_ptr<Engine> Deserialize(base::span<const uint8_t> data);
  // An engine without rules.
  static std::unique_ptr<Engine> Empty();

  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  std::vector<uint8_t> Serialize() const;

  // The resources $redirect rules and scriptlets use, a JSON list in
  // adblock-rust's format. False if `json` isn't one.
  bool UseResources(std::string_view json);
  void EnableTag(std::string_view tag, bool enabled);

  Match Check(const Request& request) const;
  // The Content-Security-Policy $csp rules add to a document or frame's
  // response; empty for none.
  std::string CspDirectives(const Request& request) const;

  // The cosmetic filters for the page at `url`, as JSON: `hide_selectors`,
  // `procedural_actions`, `exceptions`, `injected_script` (its scriptlets) and
  // `generichide`.
  std::string CosmeticResources(std::string_view url) const;
  // The generic hiding rules that the classes and ids found on a page call
  // for, but those `exceptions` (from CosmeticResources()) rule out.
  std::vector<std::string> HiddenClassIdSelectors(
      const std::vector<std::string>& classes,
      const std::vector<std::string>& ids,
      const std::vector<std::string>& exceptions) const;

 private:
  explicit Engine(rust::Box<ffi::EngineHandle> handle);

  rust::Box<ffi::EngineHandle> handle_;
};

// A filter list's header.
struct ListMetadata {
  std::string title;
  std::string homepage;
  // How often it should be downloaded again (`! Expires:`), in hours; 0 if
  // it doesn't say.
  uint32_t expires_hours = 0;
};
ListMetadata ReadListMetadata(std::string_view text);

}  // namespace oblada::barrier::adblock

#endif  // OBLADA_COMPONENTS_BARRIER_ADBLOCK_ADBLOCK_ENGINE_H_
