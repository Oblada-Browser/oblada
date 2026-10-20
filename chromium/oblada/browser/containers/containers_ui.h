#ifndef OBLADA_BROWSER_CONTAINERS_CONTAINERS_UI_H_
#define OBLADA_BROWSER_CONTAINERS_CONTAINERS_UI_H_

#include <memory>

#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"

namespace content {
class WebUIConfigMap;
class WebUIMessageHandler;
}  // namespace content

namespace oblada::containers {

// oblada://containers (chrome://containers internally): create, edit and delete
// containers, clear their data, open a tab in one.
inline constexpr char kContainersHost[] = "containers";
inline constexpr char kContainersURL[] = "chrome://containers/";

class ContainersUI : public content::WebUIController {
 public:
  explicit ContainersUI(content::WebUI* web_ui);
  ContainersUI(const ContainersUI&) = delete;
  ContainersUI& operator=(const ContainersUI&) = delete;
  ~ContainersUI() override;
};

class ContainersUIConfig : public content::DefaultWebUIConfig<ContainersUI> {
 public:
  ContainersUIConfig();
};

// The page's messages, for other pages that set containers up too
// (oblada://welcome): "getState", "addContainer", "updateContainer",
// "removeContainer", "assignSite"…
std::unique_ptr<content::WebUIMessageHandler> CreateContainersHandler();

}  // namespace oblada::containers

namespace oblada {

// Registers Oblada's internal pages (chrome_web_ui_configs.cc).
void RegisterWebUIConfigs(content::WebUIConfigMap& map);

}  // namespace oblada

#endif  // OBLADA_BROWSER_CONTAINERS_CONTAINERS_UI_H_
