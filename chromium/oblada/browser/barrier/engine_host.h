#ifndef OBLADA_BROWSER_BARRIER_ENGINE_HOST_H_
#define OBLADA_BROWSER_BARRIER_ENGINE_HOST_H_

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/scoped_refptr.h"
#include "base/no_destructor.h"
#include "base/sequence_checker.h"
#include "url/gurl.h"

namespace base {
class SequencedTaskRunner;
}  // namespace base

namespace oblada::barrier {

namespace adblock {
class Engine;
}  // namespace adblock

// The Barrier's ad-blocking engines, Brave's (adblock-rust), the same for
// every profile. Two, as Brave has them: the default one, with the lists whose
// rules don't apply to a site's own requests but in aggressive blocking (the
// catalog's lists "with first-party protections"), and the additional one,
// with the others, custom filters and subscriptions. filter_lists.h builds
// them from the lists, request_filter.h asks them about requests.
//
// They live on a sequence of their own, task_runner(): the methods that use
// them must be called on it, but SetEngines().
class EngineHost {
 public:
  static EngineHost& Get();

  EngineHost(const EngineHost&) = delete;
  EngineHost& operator=(const EngineHost&) = delete;

  scoped_refptr<base::SequencedTaskRunner> task_runner() const {
    return task_runner_;
  }

  // Replaces the engines, from any sequence.
  void SetEngines(std::unique_ptr<adblock::Engine> default_engine,
                  std::unique_ptr<adblock::Engine> additional_engine);

  // A request, for Check().
  struct Request {
    GURL url;
    // adblock-rust's name of its type ("script", "image", "sub_frame"…).
    std::string type;
    // The page's (top-level) host, which first and third parties are told
    // apart by, as Brave does.
    std::string page_host;
    // Lists flagged for first-party protection block the page's own requests
    // too.
    bool aggressive = false;
    std::string method = "GET";
  };

  struct Decision {
    bool block = false;
    // What to answer with instead, a data: URL ($redirect).
    std::string redirect;
    // The request's URL without the parameters $removeparam rules remove.
    GURL rewritten_url;
  };

  // On task_runner().
  Decision Check(const Request& request);
  // The Content-Security-Policy to add to a document or frame's response
  // ($csp); empty for none. On task_runner().
  std::string CspDirectives(const Request& request);

  // A page's cosmetic filters, from both engines.
  struct CosmeticResources {
    std::vector<std::string> hide_selectors;
    // Procedural filters and filters with an action, each as JSON.
    std::vector<std::string> procedural_actions;
    // Classes and ids (".ad", "#banner") generic rules don't hide.
    std::vector<std::string> exceptions;
    // Scriptlets to run in the page, as it starts.
    std::string injected_script;
    // Generic rules don't apply to the page ($generichide).
    bool generichide = false;
  };
  // Asks for `url`'s, from any sequence: `done` gets them on the caller's.
  void GetCosmeticResources(const GURL& url,
                            base::OnceCallback<void(CosmeticResources)> done);

  // The generic hiding rules the classes and ids found on a page call for,
  // but `exceptions`. From any sequence: `done` gets them on the caller's.
  void GetHiddenClassIdSelectors(
      std::vector<std::string> classes,
      std::vector<std::string> ids,
      std::vector<std::string> exceptions,
      base::OnceCallback<void(std::vector<std::string>)> done);

  // Whether the engines have rules (filter lists may not be there yet).
  bool HasRulesOnSequence() const;

 private:
  friend class base::NoDestructor<EngineHost>;

  EngineHost();
  ~EngineHost();

  void SetEnginesOnSequence(std::unique_ptr<adblock::Engine> default_engine,
                            std::unique_ptr<adblock::Engine> additional_engine);
  CosmeticResources CosmeticResourcesOnSequence(const GURL& url);
  std::vector<std::string> HiddenClassIdSelectorsOnSequence(
      const std::vector<std::string>& classes,
      const std::vector<std::string>& ids,
      const std::vector<std::string>& exceptions);

  scoped_refptr<base::SequencedTaskRunner> task_runner_;
  std::unique_ptr<adblock::Engine> default_engine_;
  std::unique_ptr<adblock::Engine> additional_engine_;
  SEQUENCE_CHECKER(sequence_checker_);
};

// adblock-rust's name of a request's type, from its blink::mojom::ResourceType
// (network::ResourceRequest::resource_type).
std::string_view AdblockRequestType(int resource_type);

}  // namespace oblada::barrier

#endif  // OBLADA_BROWSER_BARRIER_ENGINE_HOST_H_
