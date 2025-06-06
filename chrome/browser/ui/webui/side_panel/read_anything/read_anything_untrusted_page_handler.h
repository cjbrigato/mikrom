// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_SIDE_PANEL_READ_ANYTHING_READ_ANYTHING_UNTRUSTED_PAGE_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_SIDE_PANEL_READ_ANYTHING_READ_ANYTHING_UNTRUSTED_PAGE_HANDLER_H_

#include <memory>
#include <string>

#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/safe_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/views/side_panel/read_anything/read_anything_side_panel_controller.h"
#include "chrome/browser/ui/webui/side_panel/read_anything/read_anything_screenshotter.h"
#include "chrome/common/read_anything/read_anything.mojom.h"
#include "components/translate/core/browser/translate_client.h"
#include "components/translate/core/browser/translate_driver.h"
#include "content/public/browser/tts_controller.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_updates_and_events.h"

#if BUILDFLAG(IS_CHROMEOS)
#include "ash/public/cpp/session/session_observer.h"
#else
#include "extensions/browser/extension_registry_observer.h"
#endif

namespace content {
class ScopedAccessibilityMode;
}

class ReadAnythingUntrustedPageHandler;

///////////////////////////////////////////////////////////////////////////////
// ReadAnythingWebContentsObserver
//
//  This class allows the ReadAnythingUntrustedPageHandler to observe multiple
//  web contents at once.
//
class ReadAnythingWebContentsObserver : public content::WebContentsObserver {
 public:
  ReadAnythingWebContentsObserver(
      base::SafeRef<ReadAnythingUntrustedPageHandler> page_handler,
      content::WebContents* web_contents,
      ui::AXMode accessibility_mode);
  ReadAnythingWebContentsObserver(const ReadAnythingWebContentsObserver&) =
      delete;
  ReadAnythingWebContentsObserver& operator=(
      const ReadAnythingWebContentsObserver&) = delete;
  ~ReadAnythingWebContentsObserver() override;

  // content::WebContentsObserver:
  void AccessibilityEventReceived(
      const ui::AXUpdatesAndEvents& details) override;
  void AccessibilityLocationChangesReceived(
      const ui::AXTreeID& tree_id,
      ui::AXLocationAndScrollUpdates& details) override;
  void PrimaryPageChanged(content::Page& page) override;
  void WebContentsDestroyed() override;
  void DidStopLoading() override;
  void DidUpdateAudioMutingState(bool muted) override;

  // base::SafeRef used since the lifetime of ReadAnythingWebContentsObserver is
  // completely contained by page_handler_. See
  // ReadAnythingUntrustedPageHandler's destructor.
  base::SafeRef<ReadAnythingUntrustedPageHandler> page_handler_;

 private:
  // Enables the kReadAnythingAXMode accessibility mode flags for the
  // WebContents.
  std::unique_ptr<content::ScopedAccessibilityMode> scoped_accessibility_mode_;
};

///////////////////////////////////////////////////////////////////////////////
// ReadAnythingUntrustedPageHandler
//
//  A handler of the Read Anything app
//  (chrome/browser/resources/side_panel/read_anything/app.ts).
//  This class is created and owned by ReadAnythingUntrustedUI and has the same
//  lifetime as the Side Panel view.
//
class ReadAnythingUntrustedPageHandler :
#if BUILDFLAG(IS_CHROMEOS)
    public ash::SessionObserver,
#else
    public content::UpdateLanguageStatusDelegate,
    public extensions::ExtensionRegistryObserver,
#endif
    public ui::AXActionHandlerObserver,
    public read_anything::mojom::UntrustedPageHandler,
    public ReadAnythingSidePanelController::Observer,
    public translate::TranslateDriver::LanguageDetectionObserver {
 public:
  ReadAnythingUntrustedPageHandler(
      mojo::PendingRemote<read_anything::mojom::UntrustedPage> page,
      mojo::PendingReceiver<read_anything::mojom::UntrustedPageHandler>
          receiver,
      content::WebUI* web_ui,
      bool use_screen_ai_service);
  ReadAnythingUntrustedPageHandler(const ReadAnythingUntrustedPageHandler&) =
      delete;
  ReadAnythingUntrustedPageHandler& operator=(
      const ReadAnythingUntrustedPageHandler&) = delete;
  ~ReadAnythingUntrustedPageHandler() override;

  void AccessibilityEventReceived(const ui::AXUpdatesAndEvents& details);
  void AccessibilityLocationChangesReceived(
      const ui::AXTreeID& tree_id,
      ui::AXLocationAndScrollUpdates& details);
  void PrimaryPageChanged();
  void DidStopLoading();
  void DidUpdateAudioMutingState(bool muted);
  void WebContentsDestroyed();
  void OnActiveAXTreeIDChanged();

  // read_anything::mojom::UntrustedPageHandler:
  void OnVoiceChange(const std::string& voice,
                     const std::string& lang) override;
  void OnLanguagePrefChange(const std::string& lang, bool enabled) override;
  void OnReadAloudAudioStateChange(bool playing) override;
  void OnSpeechRateChange(double rate) override;
  void OnImageDataRequested(const ui::AXTreeID& target_tree_id,
                            ui::AXNodeID target_node_id) override;
  void OnLineSpaceChange(
      read_anything::mojom::LineSpacing line_spacing) override;
  void OnLetterSpaceChange(
      read_anything::mojom::LetterSpacing letter_spacing) override;
  void OnFontChange(const std::string& font) override;
  void OnFontSizeChange(double font_size) override;
  void OnLinksEnabledChanged(bool enabled) override;
  void OnImagesEnabledChanged(bool enabled) override;
  void OnColorChange(read_anything::mojom::Colors color) override;
  void OnHighlightGranularityChanged(
      read_anything::mojom::HighlightGranularity granularity) override;
  void GetVoicePackInfo(const std::string& language) override;
  void InstallVoicePack(const std::string& language) override;
  void UninstallVoice(const std::string& language) override;

  // TranslateDriver::LanguageDetectionObserver:
  void OnLanguageDetermined(
      const translate::LanguageDetectionDetails& details) override;
  void OnTranslateDriverDestroyed(translate::TranslateDriver* driver) override;

  // ReadAnythingSidePanelController::Observer:
  void OnTabWillDetach() override;

  // ash::SessionObserver
#if BUILDFLAG(IS_CHROMEOS)
  void OnLockStateChanged(bool locked) override;
#endif

 protected:
  void OnImageDataDownloaded(const ui::AXTreeID& target_tree_id,
                             ui::AXNodeID,
                             int id,
                             int http_status_code,
                             const GURL& image_url,
                             const std::vector<SkBitmap>& bitmaps,
                             const std::vector<gfx::Size>& sizes);

 private:
#if !BUILDFLAG(IS_CHROMEOS)
  // content::UpdateLanguageStatusDelegate:
  void OnUpdateLanguageStatus(const std::string& lang,
                              content::LanguageInstallStatus install_status,
                              const std::string& error) override;
  // extensions::ExtensionRegistryObserver implementation.

  // OnExtensionReady is called even if the TTS engine was previously installed,
  // which read anything needs to know about to access the new voices.
  void OnExtensionReady(content::BrowserContext* browser_context,
                        const extensions::Extension* extension) override;
#endif

  // ui::AXActionHandlerObserver:
  void TreeRemoved(ui::AXTreeID ax_tree_id) override;

  // read_anything::mojom::UntrustedPageHandler:
  void GetDependencyParserModel(
      GetDependencyParserModelCallback callback) override;
  void OnCopy() override;

  void OnLinkClicked(const ui::AXTreeID& target_tree_id,
                     ui::AXNodeID target_node_id) override;
  void ScrollToTargetNode(const ui::AXTreeID& target_tree_id,
                          ui::AXNodeID target_node_id) override;
  void OnSelectionChange(const ui::AXTreeID& target_tree_id,
                         ui::AXNodeID anchor_node_id,
                         int anchor_offset,
                         ui::AXNodeID focus_node_id,
                         int focus_offset) override;
  void OnCollapseSelection() override;
  void OnScreenshotRequested() override;

  // ReadAnythingSidePanelController::Observer:
  void Activate(bool active) override;
  void OnSidePanelControllerDestroyed() override;

  void SetDefaultLanguageCode(const std::string& code);

  // Sends the language code of the new page, or the default if a language can't
  // be determined.
  void SetLanguageCode(const std::string& code);

  void SetUpPdfObserver();

  void OnGetVoicePackInfo(read_anything::mojom::VoicePackInfoPtr info);

  // Logs the current visual settings values.
  void LogTextStyle();

  // Adds this as an observer of the ReadAnythingSidePanelController tied to a
  // tab.
  void ObserveWebContentsSidePanelController(tabs::TabInterface* tab);

  void PerformActionInTargetTree(const ui::AXActionData& data);

  bool AreInnerContentsPdfContent(
      std::vector<content::WebContents*> inner_contents);

  void OnScreenAIServiceInitialized(bool successful);

  // Called to notify this instance that the dependency parser loader
  // is available for model requests or is invalidating existing requests
  // specified by "is_available". The "callback" will be either forwarded to a
  // request to get the actual model file or will be run with an empty file if
  // the dependency parser loader is rejecting requests because the pending
  // model request queue is already full (100 requests maximum).
  void OnDependencyParserModelFileAvailabilityChanged(
      GetDependencyParserModelCallback callback,
      bool is_available);

  raw_ptr<ReadAnythingSidePanelController> side_panel_controller_;
  const raw_ptr<Profile> profile_;
  const raw_ptr<content::WebUI> web_ui_;

  std::unique_ptr<ReadAnythingWebContentsObserver> main_observer_;

  // This observer is used when the current page is a pdf. It observes a child
  // (iframe) of the main web contents since that is where the pdf contents is
  // contained.
  std::unique_ptr<ReadAnythingWebContentsObserver> pdf_observer_;

  // `web_screenshotter_` is used to capture a screenshot of the main web
  // contents requested.
  std::unique_ptr<ReadAnythingScreenshotter> web_screenshotter_;

  const mojo::Receiver<read_anything::mojom::UntrustedPageHandler> receiver_;
  const mojo::Remote<read_anything::mojom::UntrustedPage> page_;

  // Whether the Read Anything feature is currently active. The feature is
  // active when it is currently shown in the Side Panel.
  bool active_ = true;
  // Whether the tab is going to detach soon.
  bool tab_will_detach_ = false;

  // The current language being used in the app.
  std::string current_language_code_ = "en-US";

  // Observes the AXActionHandlerRegistry for AXTree removals.
  base::ScopedObservation<ui::AXActionHandlerRegistry,
                          ui::AXActionHandlerObserver>
      ax_action_handler_observer_{this};

  const bool use_screen_ai_service_;

  bool extension_installed_ = false;

  // Whether the currently distilled page is recognized as a pdf. This allows
  // the page handler to trigger distillation if the page would now be
  // recognized as a pdf after it finishes loading.
  bool is_pdf_ = false;

  base::ScopedClosureRunner audible_closure_;

  // Observes LanguageDetectionObserver, which notifies us when the language of
  // the contents of the current page has been determined.
  base::ScopedObservation<translate::TranslateDriver,
                          translate::TranslateDriver::LanguageDetectionObserver>
      translate_observation_{this};

  base::WeakPtrFactory<ReadAnythingUntrustedPageHandler> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_WEBUI_SIDE_PANEL_READ_ANYTHING_READ_ANYTHING_UNTRUSTED_PAGE_HANDLER_H_
