// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTERNAL_INSTALL_ERROR_DESKTOP_H_
#define CHROME_BROWSER_EXTENSIONS_EXTERNAL_INSTALL_ERROR_DESKTOP_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "chrome/browser/extensions/cws_item_service.pb.h"
#include "chrome/browser/extensions/extension_install_prompt.h"
#include "chrome/browser/extensions/external_install_error.h"
#include "chrome/browser/extensions/webstore_data_fetcher_delegate.h"
#include "extensions/common/extension_id.h"

class Browser;
class ExtensionInstallPromptShowParams;
class GlobalError;
class GlobalErrorService;

namespace content {
class BrowserContext;
}

namespace extensions {
class Extension;
class ExternalInstallManager;
class WebstoreDataFetcher;

// An error to show the user an extension has been externally installed. The
// error will automatically fetch data about the extension from the webstore (if
// possible) and will handle adding itself to the GlobalErrorService when
// initialized and removing itself from the GlobalErrorService upon
// destruction.
class ExternalInstallErrorDesktop : public ExternalInstallError,
                                    public WebstoreDataFetcherDelegate {
 public:
  ExternalInstallErrorDesktop(content::BrowserContext* browser_context,
                              const std::string& extension_id,
                              AlertType error_type,
                              ExternalInstallManager* manager);

  ExternalInstallErrorDesktop(const ExternalInstallErrorDesktop&) = delete;
  ExternalInstallErrorDesktop& operator=(const ExternalInstallErrorDesktop&) =
      delete;

  ~ExternalInstallErrorDesktop() override;

  // ExternalInstallError:
  void OnInstallPromptDone(
      ExtensionInstallPrompt::DoneCallbackPayload payload) override;
  void DidOpenBubbleView() override;
  void DidCloseBubbleView() override;
  const Extension* GetExtension() const override;
  const ExtensionId& extension_id() const override;
  ExternalInstallError::AlertType alert_type() const override;
  ExtensionInstallPrompt::Prompt* GetPromptForTesting() const override;

  // Show the associated dialog. This should only be called once the dialog is
  // ready.
  void ShowDialog(Browser* browser);

 private:
  // WebstoreDataFetcherDelegate implementation.
  void OnWebstoreRequestFailure(const std::string& extension_id) override;
  void OnFetchItemSnippetParseSuccess(
      const std::string& extension_id,
      FetchItemSnippetResponse item_snippet) override;
  void OnWebstoreResponseParseFailure(const std::string& extension_id,
                                      const std::string& error) override;

  // Called when data fetching has completed (either successfully or not).
  void OnFetchComplete();

  // Called when the dialog has been successfully populated, and is ready to be
  // shown.
  void OnDialogReady(
      std::unique_ptr<ExtensionInstallPromptShowParams> show_params,
      ExtensionInstallPrompt::DoneCallback done_callback,
      std::unique_ptr<ExtensionInstallPrompt::Prompt> prompt);

  // Removes the error.
  void RemoveError();

  // The associated BrowserContext.
  raw_ptr<content::BrowserContext> browser_context_;

  // The id of the external extension.
  ExtensionId extension_id_;

  // The type of alert to show the user.
  AlertType alert_type_;

  // The owning ExternalInstallManager.
  raw_ptr<ExternalInstallManager> manager_;

  // The associated GlobalErrorService.
  raw_ptr<GlobalErrorService> error_service_;

  // The UI for showing the error.
  std::unique_ptr<ExtensionInstallPrompt> install_ui_;
  std::unique_ptr<ExtensionInstallPrompt::Prompt> prompt_;

  // The UI for the given error, which will take the form of either a menu
  // alert or a bubble alert (depending on the `alert_type_`.
  std::unique_ptr<GlobalError> global_error_;

  // The WebstoreDataFetcher to use in order to populate the error with webstore
  // information of the extension.
  std::unique_ptr<WebstoreDataFetcher> webstore_data_fetcher_;

  base::WeakPtrFactory<ExternalInstallErrorDesktop> weak_factory_{this};
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_EXTERNAL_INSTALL_ERROR_DESKTOP_H_
