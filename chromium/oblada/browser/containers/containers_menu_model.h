#ifndef OBLADA_BROWSER_CONTAINERS_CONTAINERS_MENU_MODEL_H_
#define OBLADA_BROWSER_CONTAINERS_CONTAINERS_MENU_MODEL_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/base/window_open_disposition.h"
#include "ui/menus/simple_menu_model.h"
#include "url/gurl.h"

class BrowserWindowInterface;

namespace content {
class WebContents;
}  // namespace content

namespace oblada::containers {

// The list of containers as a submenu, opening a tab in the one picked.
class ContainersMenuModel : public ui::SimpleMenuModel,
                            public ui::SimpleMenuModel::Delegate {
 public:
  // "New container tab", in the app menu.
  static std::unique_ptr<ContainersMenuModel> ForNewTab(
      BrowserWindowInterface* browser);
  // "Open link in new container tab", in a link's context menu. `source_container`
  // is the container of the tab the link is in (empty if none).
  static std::unique_ptr<ContainersMenuModel> ForLink(
      BrowserWindowInterface* browser,
      const GURL& url,
      const std::string& source_container);
  // "Reopen in container", for the tab of `contents`: replaces it with a tab on the
  // same page in the container picked.
  static std::unique_ptr<ContainersMenuModel> ForReopen(
      BrowserWindowInterface* browser,
      content::WebContents* contents);

  ContainersMenuModel(const ContainersMenuModel&) = delete;
  ContainersMenuModel& operator=(const ContainersMenuModel&) = delete;
  ~ContainersMenuModel() override;

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  ContainersMenuModel(BrowserWindowInterface* browser,
                      const GURL& url,
                      WindowOpenDisposition disposition,
                      bool offer_no_container,
                      const std::string& current_container);

  void Open(const std::string& id);

  raw_ptr<BrowserWindowInterface> browser_;
  const GURL url_;
  const WindowOpenDisposition disposition_;
  // Container id of each item, by command id - kFirstItemId (empty: no container).
  std::vector<std::string> ids_;
  // The tab to replace, for ForReopen().
  base::WeakPtr<content::WebContents> reopen_;
};

}  // namespace oblada::containers

#endif  // OBLADA_BROWSER_CONTAINERS_CONTAINERS_MENU_MODEL_H_
