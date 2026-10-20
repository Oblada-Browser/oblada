#ifndef OBLADA_BROWSER_OBLADA_BROWSER_MAIN_EXTRA_PARTS_H_
#define OBLADA_BROWSER_OBLADA_BROWSER_MAIN_EXTRA_PARTS_H_

#include <memory>

class ChromeBrowserMainExtraParts;

namespace oblada {

// Oblada's hooks into browser startup (added in chrome_browser_main.cc).
std::unique_ptr<ChromeBrowserMainExtraParts> CreateBrowserMainExtraParts();

}  // namespace oblada

#endif  // OBLADA_BROWSER_OBLADA_BROWSER_MAIN_EXTRA_PARTS_H_
