#ifndef OBLADA_BROWSER_WELCOME_WELCOME_UI_H_
#define OBLADA_BROWSER_WELCOME_WELCOME_UI_H_

#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"

// oblada://welcome (chrome://welcome internally): Oblada's welcome, drawn on the
// window's own background in its colors, as part of the browser: the first things
// to do (import, make it the default browser, set up containers) and what's worth
// knowing about it, with thanks to Zen, whose design it follows.
namespace oblada::welcome {

inline constexpr char kWelcomeHost[] = "welcome";
inline constexpr char kWelcomeURL[] = "chrome://welcome/";

class WelcomeUI : public content::WebUIController {
 public:
  explicit WelcomeUI(content::WebUI* web_ui);
  WelcomeUI(const WelcomeUI&) = delete;
  WelcomeUI& operator=(const WelcomeUI&) = delete;
  ~WelcomeUI() override;
};

class WelcomeUIConfig : public content::DefaultWebUIConfig<WelcomeUI> {
 public:
  WelcomeUIConfig();
};

}  // namespace oblada::welcome

#endif  // OBLADA_BROWSER_WELCOME_WELCOME_UI_H_
