#include "oblada/browser/oblada_browser_main_extra_parts.h"

#include "chrome/browser/chrome_browser_main_extra_parts.h"
#include "oblada/browser/debug/window_snapshots.h"
#include "oblada/browser/firefox_addons/firefox_addons.h"
#include "oblada/browser/peek/peek.h"
#include "oblada/browser/workspaces/workspaces.h"

namespace oblada {

namespace {

class ObladaBrowserMainExtraParts : public ChromeBrowserMainExtraParts {
 public:
  void PostBrowserStart() override {
    firefox_addons::Init();
    workspaces::Init();
    peek::Init();
    debug::MaybeStartWindowSnapshots();
  }
};

}  // namespace

std::unique_ptr<ChromeBrowserMainExtraParts> CreateBrowserMainExtraParts() {
  return std::make_unique<ObladaBrowserMainExtraParts>();
}

}  // namespace oblada
