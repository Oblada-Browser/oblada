#include "oblada/browser/new_tab/new_tab_ui.h"

#include <string>

#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "oblada/browser/l10n.h"
#include "oblada/browser/welcome/welcome_ui.h"
#include "ui/views/focus/focus_manager.h"
#include "url/gurl.h"

namespace oblada::new_tab {

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(kBlankNewTabPref, true);
}

namespace {

// The host of the page `web_contents` shows (or is about to), if it's internal.
std::string InternalHost(content::WebContents* web_contents) {
  const content::NavigationEntry* entry =
      web_contents ? web_contents->GetController().GetVisibleEntry() : nullptr;
  return entry && entry->GetURL().SchemeIs(content::kChromeUIScheme)
             ? std::string(entry->GetURL().host())
             : std::string();
}

}  // namespace

bool IsBlankNewTab(content::WebContents* web_contents) {
  return InternalHost(web_contents) == kNewTabHost;
}

bool IsSeeThrough(content::WebContents* web_contents) {
  const std::string host = InternalHost(web_contents);
  return host == kNewTabHost || host == welcome::kWelcomeHost;
}

// Gives the address bar the focus once the page has loaded, if its tab is still
// the one shown and nothing else has taken the focus meanwhile.
class NewTabUI::FocusWhenLoaded : public content::WebContentsObserver {
 public:
  explicit FocusWhenLoaded(content::WebContents* web_contents)
      : content::WebContentsObserver(web_contents) {}

  // content::WebContentsObserver:
  void DidStopLoading() override {
    if (done_) {
      return;
    }
    done_ = true;
    // Once the page is on screen.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&FocusWhenLoaded::Focus,
                                  weak_factory_.GetWeakPtr()));
  }

 private:
  void Focus() {
    content::WebContents* contents = web_contents();
    tabs::TabInterface* tab =
        contents ? tabs::TabInterface::MaybeGetFromContents(contents) : nullptr;
    BrowserWindowInterface* browser =
        tab ? tab->GetBrowserWindowInterface() : nullptr;
    BrowserView* browser_view =
        browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
    if (!browser_view || !contents->GetDelegate() ||
        !contents->GetDelegate()->ShouldFocusLocationBarByDefault(contents)) {
      return;
    }
    const views::FocusManager* focus_manager = browser_view->GetFocusManager();
    const views::View* focused =
        focus_manager ? focus_manager->GetFocusedView() : nullptr;
    if (focused && !views::IsViewClass<ContentsWebView>(focused)) {
      return;
    }
    browser_view->SetFocusToLocationBar(/*is_user_initiated=*/false);
  }

  bool done_ = false;
  base::WeakPtrFactory<FocusWhenLoaded> weak_factory_{this};
};

NewTabUI::NewTabUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui),
      focus_when_loaded_(
          std::make_unique<FocusWhenLoaded>(web_ui->GetWebContents())) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), kNewTabHost);
  // Nothing but its title: no background either, for the window to show through.
  const std::string html = base::StrCat(
      {"<!doctype html><meta charset=\"utf-8\"><title>",
       base::EscapeForHTML(base::UTF16ToUTF8(L10n("new_tab.title"))),
       "</title>"});
  source->SetRequestFilter(
      base::BindRepeating([](const std::string& path) { return path.empty(); }),
      base::BindRepeating(
          [](const std::string& html, const std::string& path,
             content::WebUIDataSource::GotDataCallback done) {
            std::move(done).Run(base::MakeRefCounted<base::RefCountedString>(html));
          },
          html));
}

NewTabUI::~NewTabUI() = default;

NewTabUIConfig::NewTabUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme, kNewTabHost) {}

}  // namespace oblada::new_tab
