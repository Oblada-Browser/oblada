#ifndef OBLADA_BROWSER_EXTENSIONS_API_CONTEXTUAL_IDENTITIES_CONTEXTUAL_IDENTITIES_API_H_
#define OBLADA_BROWSER_EXTENSIONS_API_CONTEXTUAL_IDENTITIES_CONTEXTUAL_IDENTITIES_API_H_

// Firefox's contextualIdentities API over Oblada's containers; see
// //oblada/common/extensions/api/contextual_identities.json.

#include "extensions/browser/extension_function.h"

namespace oblada::firefox_addons {
// Sends the API's events (onCreated...) as containers change.
void StartContextualIdentitiesEvents();
}  // namespace oblada::firefox_addons

namespace extensions {

class ContextualIdentitiesGetFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("contextualIdentities.get", UNKNOWN)

 protected:
  ~ContextualIdentitiesGetFunction() override = default;
  ResponseAction Run() override;
};

class ContextualIdentitiesQueryFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("contextualIdentities.query", UNKNOWN)

 protected:
  ~ContextualIdentitiesQueryFunction() override = default;
  ResponseAction Run() override;
};

class ContextualIdentitiesCreateFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("contextualIdentities.create", UNKNOWN)

 protected:
  ~ContextualIdentitiesCreateFunction() override = default;
  ResponseAction Run() override;
};

class ContextualIdentitiesUpdateFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("contextualIdentities.update", UNKNOWN)

 protected:
  ~ContextualIdentitiesUpdateFunction() override = default;
  ResponseAction Run() override;
};

class ContextualIdentitiesMoveFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("contextualIdentities.move", UNKNOWN)

 protected:
  ~ContextualIdentitiesMoveFunction() override = default;
  ResponseAction Run() override;
};

class ContextualIdentitiesRemoveFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("contextualIdentities.remove", UNKNOWN)

 protected:
  ~ContextualIdentitiesRemoveFunction() override = default;
  ResponseAction Run() override;
};

}  // namespace extensions

#endif  // OBLADA_BROWSER_EXTENSIONS_API_CONTEXTUAL_IDENTITIES_CONTEXTUAL_IDENTITIES_API_H_
