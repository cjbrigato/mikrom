// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_OMNIBOX_OMNIBOX_API_H_
#define CHROME_BROWSER_EXTENSIONS_API_OMNIBOX_OMNIBOX_API_H_

#include <set>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/extensions/api/omnibox/suggestion_parser.h"
#include "chrome/common/extensions/api/omnibox.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/extension_suggestion.h"
#include "components/search_engines/template_url_service.h"
#include "extensions/browser/browser_context_keyed_api_factory.h"
#include "extensions/browser/extension_function.h"
#include "extensions/browser/extension_icon_manager.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_registry_observer.h"
#include "extensions/browser/permissions_manager.h"
#include "extensions/common/extension_id.h"
#include "ui/base/window_open_disposition.h"

class Profile;
class TemplateURL;
class TemplateURLService;

namespace content {
class BrowserContext;
class WebContents;
}

namespace gfx {
class Image;
}

namespace extensions {

// Event router class for events related to the omnibox API.
class ExtensionOmniboxEventRouter {
 public:
  static constexpr size_t kMaxSuggestionActions = 7;
  static constexpr char kMaxSuggestionActionsExceededError[] =
      "Found suggest result with %d action, which exceeds the limit of %d "
      "actions per suggestion.";
  static constexpr char kActionsRequireDirectInputPermissionError[] =
      "Actions in suggest results require omnibox.directInput permission.";
  static constexpr char kActionIconError[] =
      "Action icon failed to parse for suggestion description: %s and action "
      "name: %s.";

  ExtensionOmniboxEventRouter(const ExtensionOmniboxEventRouter&) = delete;
  ExtensionOmniboxEventRouter& operator=(const ExtensionOmniboxEventRouter&) =
      delete;

  // The user has just typed the omnibox keyword. This is sent exactly once in
  // a given input session, before any OnInputChanged events.
  static void OnInputStarted(Profile* profile, const ExtensionId& extension_id);

  // The user has changed what is typed into the omnibox while in an extension
  // keyword session. Returns true if someone is listening to this event, and
  // thus we have some degree of confidence we'll get a response.
  static bool OnInputChanged(Profile* profile,
                             const ExtensionId& extension_id,
                             const std::string& input,
                             int suggest_id);

  // The user has accepted the omnibox input.
  static void OnInputEntered(content::WebContents* web_contents,
                             const ExtensionId& extension_id,
                             const std::string& input,
                             WindowOpenDisposition disposition);

  // The user has cleared the keyword, or closed the omnibox popup. This is
  // sent at most once in a give input session, after any OnInputChanged events.
  static void OnInputCancelled(Profile* profile,
                               const ExtensionId& extension_id);

  // The user has deleted an extension omnibox suggestion result.
  static void OnDeleteSuggestion(Profile* profile,
                                 const ExtensionId& extension_id,
                                 const std::string& suggestion_text);

  // The user has clicked an action of an extension omnibox suggestion result.
  static void OnActionExecuted(Profile* profile,
                               const ExtensionId& extension_id,
                               const std::string& action_name,
                               const std::string& content);
};

class OmniboxSendSuggestionsFunction : public ExtensionFunction {
 public:
  OmniboxSendSuggestionsFunction();

  DECLARE_EXTENSION_FUNCTION("omnibox.sendSuggestions", OMNIBOX_SENDSUGGESTIONS)

 protected:
  ~OmniboxSendSuggestionsFunction() override;

  // ExtensionFunction:
  ResponseAction Run() override;

 private:
  // Called with the result of parsing the omnibox suggestions.
  void OnParsedDescriptionsAndStyles(DescriptionAndStylesResult result);

  // Notifies the omnibox that the suggestions have been prepared.
  void NotifySuggestionsReady();

  // The parsed `params_.suggest_results`.
  std::vector<ExtensionSuggestion> extension_suggestions_;

  int request_id_;
};

class OmniboxAPI : public BrowserContextKeyedAPI,
                   public ExtensionRegistryObserver,
                   public PermissionsManager::Observer {
 public:
  explicit OmniboxAPI(content::BrowserContext* context);

  OmniboxAPI(const OmniboxAPI&) = delete;
  OmniboxAPI& operator=(const OmniboxAPI&) = delete;

  ~OmniboxAPI() override;

  // BrowserContextKeyedAPI implementation.
  static BrowserContextKeyedAPIFactory<OmniboxAPI>* GetFactoryInstance();

  // Convenience method to get the OmniboxAPI for a profile.
  static OmniboxAPI* Get(content::BrowserContext* context);

  // KeyedService implementation.
  void Shutdown() override;

  // Returns the icon to display in the location bar or omnibox popup for the
  // given extension.
  gfx::Image GetOmniboxIcon(const ExtensionId& extension_id);

 private:
  friend class BrowserContextKeyedAPIFactory<OmniboxAPI>;

  typedef std::set<raw_ptr<const Extension, SetExperimental>> PendingExtensions;

  void OnTemplateURLsLoaded();

  // ExtensionRegistryObserver implementation.
  void OnExtensionLoaded(content::BrowserContext* browser_context,
                         const Extension* extension) override;
  void OnExtensionUnloaded(content::BrowserContext* browser_context,
                           const Extension* extension,
                           UnloadedExtensionReason reason) override;

  // PermissionsManager::Observer:
  void OnExtensionPermissionsUpdated(
      const Extension& extension,
      const PermissionSet& permissions,
      PermissionsManager::UpdateReason reason) override;

  // BrowserContextKeyedAPI implementation.
  static const char* service_name() {
    return "OmniboxAPI";
  }
  static const bool kServiceRedirectedInIncognito = true;

  raw_ptr<Profile> profile_;

  raw_ptr<TemplateURLService> url_service_;

  // List of extensions waiting for the TemplateURLService to Load to
  // have keywords registered.
  PendingExtensions pending_extensions_;

  // Listen to extension load, unloaded notifications.
  base::ScopedObservation<ExtensionRegistry, ExtensionRegistryObserver>
      extension_registry_observation_{this};

  // Permissions observer to listen to `omnibox.directInput` permission changes.
  base::ScopedObservation<extensions::PermissionsManager,
                          extensions::PermissionsManager::Observer>
      permissions_manager_observation_{this};

  // Keeps track of favicon-sized omnibox icons for extensions.
  ExtensionIconManager omnibox_icon_manager_;

  base::CallbackListSubscription template_url_subscription_;
};

template <>
void BrowserContextKeyedAPIFactory<OmniboxAPI>::DeclareFactoryDependencies();

class OmniboxSetDefaultSuggestionFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("omnibox.setDefaultSuggestion",
                             OMNIBOX_SETDEFAULTSUGGESTION)

 protected:
  ~OmniboxSetDefaultSuggestionFunction() override = default;

  // Called asynchronously with the parsed description and styles for the
  // default suggestion.
  void OnParsedDescriptionAndStyles(DescriptionAndStylesResult result);

  // Sets the default suggestion in the extension preferences.
  void SetDefaultSuggestion(
      const api::omnibox::DefaultSuggestResult& suggestion);

  // ExtensionFunction:
  ResponseAction Run() override;
};

// If the extension has set a custom default suggestion via
// omnibox.setDefaultSuggestion, apply that to `match`. Otherwise, do nothing.
void ApplyDefaultSuggestionForExtensionKeyword(
    Profile* profile,
    const TemplateURL* keyword,
    const std::u16string& remaining_input,
    AutocompleteMatch* match);

// This function converts style information populated by the JSON schema
// // compiler into an ACMatchClassifications object.
ACMatchClassifications StyleTypesToACMatchClassifications(
    const std::vector<api::omnibox::MatchClassification>* description_styles,
    const std::string& suggestion_description);

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_OMNIBOX_OMNIBOX_API_H_
