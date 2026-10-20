#include "oblada/browser/welcome/welcome_ui.h"

#include <memory>
#include <string>
#include <string_view>

#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/tabs/public/tab_interface.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/chrome_unscaled_resources.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "content/public/common/url_constants.h"
#include "oblada/browser/containers/containers_ui.h"
#include "oblada/browser/import/import_handler.h"
#include "oblada/browser/l10n.h"
#include "oblada/browser/welcome/welcome_page.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace oblada::welcome {

namespace {

// The page's text: l10n.cc's "welcome.<name>" for each $i18n{<name>}.
constexpr std::string_view kStrings[] = {
    "lang",          "title",            "lead",
    "start",         "skip",             "back",
    "next",          "finish",           "import_title",
    "import_text",   "import_from",      "import_running",
    "import_done",   "import_bookmarks", "import_history",
    "import_passwords", "import_autofill", "import_cards",
    "import_safari_how", "import_safari_choose", "import_locked",
    "import_failed", "import_unreadable", "import_none",
    "import_file",   "containers_title", "containers_setup_text",
    "containers_name", "containers_sites", "containers_add_site",
    "containers_remove", "containers_done", "containers_new",
    "containers_add", "containers_import_from", "containers_imported",
    "default_title",
    "default_text",  "default",          "default_done",
    "workspaces",    "workspaces_text",  "workspaces_hint",
    "containers",    "containers_text",  "containers_hint",
    "work",          "personal",         "address_bar",
    "address_bar_text", "address_bar_hint", "address_query",
    "peek",          "peek_text",        "peek_hint",
    "peek_link",     "split",            "split_text",
    "split_hint",    "compact",          "compact_text",
    "compact_hint",  "extensions",       "extensions_text",
    "done_title",    "zen",              "zen_link",
};

// What the page asks of the browser: whether Oblada is the default browser,
// making it so, and leaving for a new tab once done.
class WelcomeHandler : public content::WebUIMessageHandler {
 public:
  WelcomeHandler() = default;
  WelcomeHandler(const WelcomeHandler&) = delete;
  WelcomeHandler& operator=(const WelcomeHandler&) = delete;
  ~WelcomeHandler() override = default;

  // content::WebUIMessageHandler:
  void RegisterMessages() override {
    web_ui()->RegisterMessageCallback(
        "getDefaultBrowser",
        base::BindRepeating(&WelcomeHandler::GetDefaultBrowser,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "setAsDefaultBrowser",
        base::BindRepeating(&WelcomeHandler::SetAsDefaultBrowser,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "finish",
        base::BindRepeating(&WelcomeHandler::Finish, base::Unretained(this)));
  }
  void OnJavascriptDisallowed() override { weak_factory_.InvalidateWeakPtrs(); }

 private:
  void GetDefaultBrowser(const base::ListValue& args) {
    AllowJavascript();
    base::MakeRefCounted<shell_integration::DefaultBrowserWorker>()
        ->StartCheckIsDefault(base::BindOnce(&WelcomeHandler::Resolve,
                                             weak_factory_.GetWeakPtr(),
                                             args[0].Clone()));
  }

  // On macOS, the system asks the user to confirm, after this resolves: the
  // page checks again meanwhile.
  void SetAsDefaultBrowser(const base::ListValue& args) {
    AllowJavascript();
    base::MakeRefCounted<shell_integration::DefaultBrowserWorker>()
        ->StartSetAsDefault(base::BindOnce(&WelcomeHandler::Resolve,
                                           weak_factory_.GetWeakPtr(),
                                           args[0].Clone()));
  }

  void Resolve(base::Value callback_id,
               shell_integration::DefaultWebClientState state) {
    ResolveJavascriptCallback(callback_id,
                              base::Value(state == shell_integration::IS_DEFAULT));
  }

  // Opens a new tab as ⌘T does, then closes this one: the New Tab page then
  // comes up as it always does, rather than in place of this page.
  void Finish(const base::ListValue& args) {
    content::WebContents* contents = web_ui()->GetWebContents();
    tabs::TabInterface* tab = tabs::TabInterface::MaybeGetFromContents(contents);
    if (!tab || !tab->GetBrowserWindowInterface()) {
      contents->GetController().LoadURL(GURL(chrome::kChromeUINewTabURL),
                                        content::Referrer(),
                                        ui::PAGE_TRANSITION_AUTO_TOPLEVEL,
                                        std::string());
      return;
    }
    chrome::ExecuteCommand(tab->GetBrowserWindowInterface(), IDC_NEW_TAB);
    // Not from inside its own page's message.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::WeakPtr<tabs::TabInterface> tab) {
                         if (tab) {
                           tab->Close();
                         }
                       },
                       tab->GetWeakPtr()));
  }

  base::WeakPtrFactory<WelcomeHandler> weak_factory_{this};
};

}  // namespace

bool IsWelcomeURL(const GURL& url) {
  return url.SchemeIs(content::kChromeUIScheme) && url.host() == kWelcomeHost;
}

WelcomeUI::WelcomeUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  web_ui->AddMessageHandler(std::make_unique<WelcomeHandler>());
  web_ui->AddMessageHandler(std::make_unique<import::ImportHandler>());
  web_ui->AddMessageHandler(containers::CreateContainersHandler());
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), kWelcomeHost);
  std::string html = kWelcomeHtml;
  for (std::string_view name : kStrings) {
    base::ReplaceSubstringsAfterOffset(
        &html, 0, base::StrCat({"$i18n{", name, "}"}),
        base::EscapeForHTML(
            base::UTF16ToUTF8(L10n(base::StrCat({"welcome.", name})))));
  }
  // For what differs between platforms (shortcuts, window controls).
#if BUILDFLAG(IS_MAC)
  constexpr char kPlatform[] = "mac";
#elif BUILDFLAG(IS_WIN)
  constexpr char kPlatform[] = "win";
#else
  constexpr char kPlatform[] = "linux";
#endif
  base::ReplaceSubstringsAfterOffset(&html, 0, "$platform", kPlatform);
  // The page itself whatever follows it (e.g. oblada://welcome/#import, which
  // opens at the import), its style and its script.
  source->SetRequestFilter(
      base::BindRepeating([](const std::string& path) {
        const std::string_view file =
            std::string_view(path).substr(0, path.find_first_of("?#"));
        return file.empty() || file == "welcome.css" || file == "welcome.js";
      }),
      base::BindRepeating(
          [](const std::string& html, const std::string& path,
             content::WebUIDataSource::GotDataCallback done) {
            const std::string_view file =
                std::string_view(path).substr(0, path.find_first_of("?#"));
            std::string data = file == "welcome.css"  ? kWelcomeCss
                               : file == "welcome.js" ? kWelcomeJs
                                                      : html;
            std::move(done).Run(
                base::MakeRefCounted<base::RefCountedString>(std::move(data)));
          },
          std::move(html)));
  // Oblada's logo (branding/export.sh puts it in place of Chromium's).
  source->AddResourcePath("logo.png", IDR_PRODUCT_LOGO_256);
}

WelcomeUI::~WelcomeUI() = default;

WelcomeUIConfig::WelcomeUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme, kWelcomeHost) {}

}  // namespace oblada::welcome
