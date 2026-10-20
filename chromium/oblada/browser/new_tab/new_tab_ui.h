#ifndef OBLADA_BROWSER_NEW_TAB_NEW_TAB_UI_H_
#define OBLADA_BROWSER_NEW_TAB_NEW_TAB_UI_H_

#include <memory>

#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"

namespace content {
class WebContents;
}  // namespace content

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

// The New Tab page, as in Zen and Arc: a blank page, which the window shows
// through (its glass), for the address bar to float over (see
// oblada/browser/ui/floating_location_bar.h) rather than the search engine's page
// with a search box of its own. chrome://newtab shows it (search.cc), unless the
// profile turns it off.
namespace oblada::new_tab {

inline constexpr char kNewTabHost[] = "oblada-new-tab";
inline constexpr char kNewTabURL[] = "chrome://oblada-new-tab/";

// Whether chrome://newtab shows this page (the default) rather than the search
// engine's.
inline constexpr char kBlankNewTabPref[] = "oblada.blank_new_tab";

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

// Whether `web_contents` shows this page (or is about to). It gives the address
// bar the focus once it has loaded, rather than as its tab opens
// (browser_web_contents_delegate.cc): the bar's animation then has the window to
// itself.
bool IsBlankNewTab(content::WebContents* web_contents);

// Whether `web_contents` shows one of Oblada's pages drawn on the window's own
// background (its glass): this one and oblada://welcome. Their tab's background
// is see-through then (contents_web_view.cc).
bool IsSeeThrough(content::WebContents* web_contents);

class NewTabUI : public content::WebUIController {
 public:
  explicit NewTabUI(content::WebUI* web_ui);
  NewTabUI(const NewTabUI&) = delete;
  NewTabUI& operator=(const NewTabUI&) = delete;
  ~NewTabUI() override;

 private:
  class FocusWhenLoaded;
  std::unique_ptr<FocusWhenLoaded> focus_when_loaded_;
};

class NewTabUIConfig : public content::DefaultWebUIConfig<NewTabUI> {
 public:
  NewTabUIConfig();
};

}  // namespace oblada::new_tab

#endif  // OBLADA_BROWSER_NEW_TAB_NEW_TAB_UI_H_
