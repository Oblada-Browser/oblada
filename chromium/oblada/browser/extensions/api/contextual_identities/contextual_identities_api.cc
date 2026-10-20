#include "oblada/browser/extensions/api/contextual_identities/contextual_identities_api.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/stringprintf.h"
#include "chrome/browser/profiles/profile.h"
#include "extensions/browser/event_router.h"
#include "extensions/browser/extension_event_histogram_value.h"
#include "oblada/browser/containers/containers.h"
#include "oblada/browser/firefox_addons/cookie_store_ids.h"
#include "oblada/browser/ui/icons.h"
#include "oblada/common/extensions/api/contextual_identities.h"

namespace oblada::firefox_addons {

namespace {

namespace api = extensions::api::contextual_identities;
using containers::Container;

// Firefox's container icons.
constexpr std::string_view kIcons[] = {
    "fingerprint", "briefcase", "dollar", "cart",  "circle", "gift", "vacation",
    "food",        "fruit",     "pet",    "tree",  "chill",  "fence"};

bool IsFirefoxIcon(std::string_view icon) {
  return std::ranges::find(kIcons, icon) != std::end(kIcons);
}

bool IsColor(std::string_view color) {
  return std::ranges::find(containers::ColorNames(), color) !=
         containers::ColorNames().end();
}

api::ContextualIdentity ToIdentity(const Container& container) {
  const SkColor color = containers::ColorFor(container.color);
  const std::string color_code = base::StringPrintf(
      "#%02x%02x%02x", SkColorGetR(color), SkColorGetG(color), SkColorGetB(color));
  api::ContextualIdentity identity;
  identity.name = container.name;
  identity.icon = std::string(oblada::icons::FirefoxName(container.icon));
  // Firefox's icons are its own resources; add-ons use the URL as a CSS mask:
  // Oblada's icon, else a dot.
  identity.icon_url = oblada::icons::MaskUrl(container.icon, 16);
  if (identity.icon_url.empty()) {
    identity.icon_url = base::StrCat(
        {"data:image/svg+xml,",
         base::EscapeAllExceptUnreserved(
             "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<circle cx='8' cy='8' r='7' fill='black'/></svg>")});
  }
  identity.color = container.color;
  identity.color_code = color_code;
  identity.cookie_store_id = CookieStoreIdOf(container);
  return identity;
}

Profile* ProfileOf(content::BrowserContext* context) {
  return Profile::FromBrowserContext(context)->GetOriginalProfile();
}

std::optional<Container> FindByCookieStoreId(Profile* profile,
                                             const std::string& cookie_store_id) {
  base::expected<std::string, std::string> id =
      ContainerOfCookieStoreId(profile, cookie_store_id);
  if (!id.has_value() || id->empty()) {
    return std::nullopt;
  }
  return containers::FindContainer(profile, *id);
}

std::string InvalidIdentity(const std::string& cookie_store_id) {
  return "Invalid contextual identity: " + cookie_store_id;
}

void DispatchEvent(Profile* profile,
                   containers::Change change,
                   const Container& container) {
  extensions::EventRouter* router = extensions::EventRouter::Get(profile);
  if (!router) {
    return;
  }
  api::ChangeInfo info;
  info.contextual_identity = ToIdentity(container);
  std::string name;
  base::ListValue args;
  switch (change) {
    case containers::Change::kCreated:
      name = api::OnCreated::kEventName;
      args = api::OnCreated::Create(info);
      break;
    case containers::Change::kUpdated:
      name = api::OnUpdated::kEventName;
      args = api::OnUpdated::Create(info);
      break;
    case containers::Change::kRemoved:
      name = api::OnRemoved::kEventName;
      args = api::OnRemoved::Create(info);
      break;
  }
  router->BroadcastEvent(std::make_unique<extensions::Event>(
      extensions::events::FOR_TEST, name, std::move(args)));
}

}  // namespace

void StartContextualIdentitiesEvents() {
  static base::NoDestructor<base::CallbackListSubscription> subscription(
      containers::ObserveContainerChanges(base::BindRepeating(&DispatchEvent)));
}

}  // namespace oblada::firefox_addons

namespace extensions {

namespace api_ci = api::contextual_identities;
using oblada::containers::Container;
using oblada::firefox_addons::FindByCookieStoreId;
using oblada::firefox_addons::InvalidIdentity;
using oblada::firefox_addons::IsColor;
using oblada::firefox_addons::IsFirefoxIcon;
using oblada::firefox_addons::ProfileOf;
using oblada::firefox_addons::ToIdentity;

ExtensionFunction::ResponseAction ContextualIdentitiesGetFunction::Run() {
  std::optional<api_ci::Get::Params> params = api_ci::Get::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);
  std::optional<Container> container =
      FindByCookieStoreId(ProfileOf(browser_context()), params->cookie_store_id);
  if (!container) {
    return RespondNow(Error(InvalidIdentity(params->cookie_store_id)));
  }
  return RespondNow(ArgumentList(api_ci::Get::Results::Create(ToIdentity(*container))));
}

ExtensionFunction::ResponseAction ContextualIdentitiesQueryFunction::Run() {
  std::optional<api_ci::Query::Params> params = api_ci::Query::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);
  std::vector<api_ci::ContextualIdentity> identities;
  for (const Container& container :
       oblada::containers::GetContainers(ProfileOf(browser_context()))) {
    if (!params->details.name || *params->details.name == container.name) {
      identities.push_back(ToIdentity(container));
    }
  }
  return RespondNow(ArgumentList(api_ci::Query::Results::Create(identities)));
}

ExtensionFunction::ResponseAction ContextualIdentitiesCreateFunction::Run() {
  std::optional<api_ci::Create::Params> params = api_ci::Create::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);
  const api_ci::Create::Params::Details& details = params->details;
  if (!IsColor(details.color)) {
    return RespondNow(Error("Invalid color name: " + details.color));
  }
  if (!IsFirefoxIcon(details.icon)) {
    return RespondNow(Error("Invalid icon name: " + details.icon));
  }
  const Container container = oblada::containers::AddContainer(
      ProfileOf(browser_context()), details.name, details.color, details.icon);
  return RespondNow(ArgumentList(api_ci::Create::Results::Create(ToIdentity(container))));
}

ExtensionFunction::ResponseAction ContextualIdentitiesUpdateFunction::Run() {
  std::optional<api_ci::Update::Params> params = api_ci::Update::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);
  Profile* profile = ProfileOf(browser_context());
  std::optional<Container> container =
      FindByCookieStoreId(profile, params->cookie_store_id);
  if (!container) {
    return RespondNow(Error(InvalidIdentity(params->cookie_store_id)));
  }
  const api_ci::Update::Params::Details& details = params->details;
  if (details.color && !IsColor(*details.color)) {
    return RespondNow(Error("Invalid color name: " + *details.color));
  }
  if (details.icon && !IsFirefoxIcon(*details.icon)) {
    return RespondNow(Error("Invalid icon name: " + *details.icon));
  }
  if (details.name) {
    container->name = *details.name;
  }
  if (details.color) {
    container->color = *details.color;
  }
  if (details.icon) {
    container->icon = *details.icon;
  }
  oblada::containers::UpdateContainer(profile, *container);
  return RespondNow(ArgumentList(api_ci::Update::Results::Create(ToIdentity(*container))));
}

ExtensionFunction::ResponseAction ContextualIdentitiesMoveFunction::Run() {
  std::optional<api_ci::Move::Params> params = api_ci::Move::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);
  Profile* profile = ProfileOf(browser_context());
  std::vector<std::string> cookie_store_ids;
  if (params->cookie_store_ids.as_string) {
    cookie_store_ids.push_back(*params->cookie_store_ids.as_string);
  } else if (params->cookie_store_ids.as_strings) {
    cookie_store_ids = *params->cookie_store_ids.as_strings;
  }
  std::vector<std::string> ids;
  for (const std::string& cookie_store_id : cookie_store_ids) {
    std::optional<Container> container = FindByCookieStoreId(profile, cookie_store_id);
    if (!container || container->temporary) {
      return RespondNow(Error(InvalidIdentity(cookie_store_id)));
    }
    ids.push_back(container->id);
  }
  // -1 is the end, as in Firefox.
  size_t position = params->position < 0 ? SIZE_MAX : params->position;
  for (const std::string& id : ids) {
    oblada::containers::MoveContainer(profile, id, position);
    if (position != SIZE_MAX) {
      ++position;
    }
  }
  return RespondNow(NoArguments());
}

ExtensionFunction::ResponseAction ContextualIdentitiesRemoveFunction::Run() {
  std::optional<api_ci::Remove::Params> params = api_ci::Remove::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);
  Profile* profile = ProfileOf(browser_context());
  std::optional<Container> container =
      FindByCookieStoreId(profile, params->cookie_store_id);
  if (!container) {
    return RespondNow(Error(InvalidIdentity(params->cookie_store_id)));
  }
  oblada::containers::RemoveContainer(profile, container->id);
  return RespondNow(ArgumentList(api_ci::Remove::Results::Create(ToIdentity(*container))));
}

}  // namespace extensions
