#ifndef OBLADA_BROWSER_SETTINGS_BARRIER_HANDLER_H_
#define OBLADA_BROWSER_SETTINGS_BARRIER_HANDLER_H_

#include "base/callback_list.h"
#include "base/values.h"
#include "components/prefs/pref_change_registrar.h"
#include "content/public/browser/web_ui_message_handler.h"

class Profile;

namespace oblada::settings {

// Settings > Oblada's Barrier (barrier/barrier.h) beyond what its preferences
// show: the switch that turns it all off, blocking scripts by default
// (Chromium's JavaScript setting), the filter lists, and the sites with
// settings of their own.
class BarrierHandler : public content::WebUIMessageHandler {
 public:
  explicit BarrierHandler(Profile* profile);
  ~BarrierHandler() override;

  BarrierHandler(const BarrierHandler&) = delete;
  BarrierHandler& operator=(const BarrierHandler&) = delete;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

 private:
  void HandleGetState(const base::ListValue& args);
  void HandleSetEnabled(const base::ListValue& args);
  void HandleRelaunch(const base::ListValue& args);
  void HandleSetBlockScripts(const base::ListValue& args);
  void HandleGetLists(const base::ListValue& args);
  void HandleSetList(const base::ListValue& args);
  void HandleSubscribe(const base::ListValue& args);
  void HandleUnsubscribe(const base::ListValue& args);
  void HandleSetSubscription(const base::ListValue& args);
  void HandleSetCustomFilters(const base::ListValue& args);
  void HandleUpdateLists(const base::ListValue& args);
  void HandleGetSites(const base::ListValue& args);
  void HandleResetSite(const base::ListValue& args);

  base::DictValue State();
  base::DictValue Lists();
  void OnListsChanged();
  // A switch decided at startup changed (Local State).
  void OnStateChanged();

  raw_ptr<Profile> profile_;
  base::CallbackListSubscription lists_subscription_;
  PrefChangeRegistrar local_state_registrar_;
};

}  // namespace oblada::settings

#endif  // OBLADA_BROWSER_SETTINGS_BARRIER_HANDLER_H_
