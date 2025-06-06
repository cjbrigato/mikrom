// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_ACCESSIBILITY_READ_ANYTHING_READ_ANYTHING_APP_CONTROLLER_H_
#define CHROME_RENDERER_ACCESSIBILITY_READ_ANYTHING_READ_ANYTHING_APP_CONTROLLER_H_

#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/safe_ref.h"
#include "base/scoped_observation.h"
#include "chrome/common/read_anything/read_anything.mojom.h"
#include "chrome/renderer/accessibility/read_anything/read_aloud_app_model.h"
#include "chrome/renderer/accessibility/read_anything/read_anything_app_model.h"
#include "content/public/renderer/render_frame_observer.h"
#include "gin/wrappable.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/metrics/public/cpp/ukm_source_id.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_location_and_scroll_updates.h"
#include "ui/accessibility/ax_node_id_forward.h"
#include "ui/accessibility/ax_node_position.h"
#include "ui/accessibility/ax_position.h"
#include "ui/accessibility/ax_tree_observer.h"
#include "ui/accessibility/ax_tree_update_forward.h"

namespace content {
class RenderFrame;
}  // namespace content

namespace ui {
class AXNode;
class AXTree;
}  // namespace ui

namespace ukm {
class MojoUkmRecorder;
}  // namespace ukm

class AXTreeDistiller;
class DependencyParserModel;
class ReadAnythingAppControllerTest;
class ReadAnythingAppControllerScreen2xDataCollectionModeTest;

///////////////////////////////////////////////////////////////////////////////
// ReadAnythingAppController
//
//  A class that controls the Read Anything WebUI app. It serves two purposes:
//  1. Communicate with ReadAnythingUntrustedPageHandler (written in c++) via
//  mojom.
//  2. Communicate with ReadAnythingApp (written in ts) via gin bindings.
//  The ReadAnythingAppController unserializes the AXTreeUpdate and exposes
//  methods on it to the ts resource for accessing information about the AXTree.
//  This class is owned by the ChromeRenderFrameObserver and has the same
//  lifetime as the render frame.
//
//  This class is responsible for identifying the nodes to be displayed by the
//  webapp and providing attributes about them when queried. Nodes are selected
//  from the provided AXTreeUpdate and content nodes. There are two rendering
//  algorithms:
//  1. If the AXTreeUpdate has a selection, display a subtree containing all of
//     the nodes between the selection start and end.
//  2. If the AXTreeUpdate has no selection, display a subtree containing all of
//     the content nodes, their descendants, and their ancestors.
//
class ReadAnythingAppController
    : public content::RenderFrameObserver,
      public gin::Wrappable<ReadAnythingAppController>,
      public ReadAnythingAppModel::ModelObserver,
      public read_anything::mojom::UntrustedPage,
      public ui::AXTreeObserver {
 public:
  static gin::WrapperInfo kWrapperInfo;

  ReadAnythingAppController(const ReadAnythingAppController&) = delete;
  ReadAnythingAppController& operator=(const ReadAnythingAppController&) =
      delete;

  // Installs v8 context for Read Anything and adds chrome.readingMode binding
  // to page.
  static ReadAnythingAppController* Install(content::RenderFrame* render_frame);

  // content::RenderFrameObserver:
  void OnDestruct() override;

  // gin::WrappableBase:
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) override;

  // ReadAnythingAppModel::ModelObserver:
  void OnTreeAdded(ui::AXTree* tree) override;
  void OnTreeRemoved(ui::AXTree* tree) override;

  // read_anything::mojom::UntrustedPage:
  void AccessibilityEventReceived(
      const ui::AXTreeID& tree_id,
      const std::vector<ui::AXTreeUpdate>& updates,
      const std::vector<ui::AXEvent>& events) override;
  void AccessibilityLocationChangesReceived(
      const ui::AXTreeID& tree_id,
      ui::AXLocationAndScrollUpdates& details) override;
  void AccessibilityLocationChangesReceived(
      const ui::AXTreeID& tree_id,
      const ui::AXLocationAndScrollUpdates& details) override;
  void OnActiveAXTreeIDChanged(const ui::AXTreeID& tree_id,
                               ukm::SourceId ukm_source_id,
                               bool is_pdf) override;
  void OnAXTreeDestroyed(const ui::AXTreeID& tree_id) override;
  void OnImageDataDownloaded(const ui::AXTreeID& tree_id,
                             ui::AXNodeID node_id,
                             const SkBitmap& image) override;
  void OnSettingsRestoredFromPrefs(
      read_anything::mojom::LineSpacing line_spacing,
      read_anything::mojom::LetterSpacing letter_spacing,
      const std::string& font,
      double font_size,
      bool links_enabled,
      bool images_enabled,
      read_anything::mojom::Colors color,
      double speech_rate,
      base::Value::Dict voices,
      base::Value::List languages_enabled_in_pref,
      read_anything::mojom::HighlightGranularity granularity) override;
  void SetLanguageCode(const std::string& code) override;
  void SetDefaultLanguageCode(const std::string& code) override;
  void ScreenAIServiceReady() override;
  void OnGetVoicePackInfo(
      read_anything::mojom::VoicePackInfoPtr voice_pack_info) override;
  void OnReadingModeHidden() override;
  void OnTabWillDetach() override;
  void OnTabMuteStateChange(bool muted) override;
#if BUILDFLAG(IS_CHROMEOS)
  void OnDeviceLocked() override;
#else
  void OnTtsEngineInstalled() override;
#endif

  // ui::AXTreeObserver:
  void OnNodeDataChanged(ui::AXTree* tree,
                         const ui::AXNodeData& old_node_data,
                         const ui::AXNodeData& new_node_data) override;

  void OnNodeWillBeDeleted(ui::AXTree* tree, ui::AXNode* node) override;

  void OnNodeDeleted(ui::AXTree* tree, ui::AXNodeID node) override;

  // gin templates:
  ui::AXNodeID RootId() const;
  ui::AXNodeID StartNodeId() const;
  int StartOffset() const;
  ui::AXNodeID EndNodeId() const;
  int EndOffset() const;
  std::string FontName() const;
  float FontSize() const;
  bool LinksEnabled() const;
  bool ImagesEnabled() const;
  bool ImagesFeatureEnabled() const;
  double SpeechRate() const;
  void OnFontSizeChanged(bool increase);
  void OnFontSizeReset();
  void OnLinksEnabledToggled();
  void OnImagesEnabledToggled();
  int LetterSpacing() const;
  int LineSpacing() const;
  int ColorTheme() const;
  int HighlightGranularity() const;
  bool IsHighlightOn();
  int StandardLineSpacing() const;
  int LooseLineSpacing() const;
  int VeryLooseLineSpacing() const;
  int StandardLetterSpacing() const;
  int WideLetterSpacing() const;
  int VeryWideLetterSpacing() const;
  int DefaultTheme() const;
  int LightTheme() const;
  int DarkTheme() const;
  int YellowTheme() const;
  int BlueTheme() const;
  int AutoHighlighting() const;
  int WordHighlighting() const;
  int PhraseHighlighting() const;
  int SentenceHighlighting() const;
  int NoHighlighting() const;
  int PauseButtonStopSource() const;
  int KeyboardShortcutStopSource() const;
  int EngineInterruptStopSource() const;
  int EngineErrorStopSource() const;
  int ContentFinishedStopSource() const;
  int UnexpectedUpdateContentStopSource() const;
  std::string GetStoredVoice() const;
  std::vector<std::string> GetLanguagesEnabledInPref() const;
  std::vector<ui::AXNodeID> GetChildren(ui::AXNodeID ax_node_id) const;
  std::string GetDataFontCss(ui::AXNodeID ax_node_id) const;
  std::string GetHtmlTag(ui::AXNodeID ax_node_id) const;
  std::string GetLanguage(ui::AXNodeID ax_node_id) const;
  std::u16string GetTextContent(ui::AXNodeID ax_node_id) const;
  std::string GetTextDirection(ui::AXNodeID ax_node_id) const;
  std::string GetUrl(ui::AXNodeID ax_node_id) const;
  std::string GetAltText(ui::AXNodeID ax_node_id) const;
  // The results of these are sent back via UntrustedPage::OnGetVoicePackInfo.
  void SendGetVoicePackInfoRequest(const std::string& language) const;
  void SendInstallVoicePackRequest(const std::string& language) const;
  void SendUninstallVoiceRequest(const std::string& language) const;

  bool IsSpeechTreeInitialized();
  bool ShouldBold(ui::AXNodeID ax_node_id) const;
  bool IsOverline(ui::AXNodeID ax_node_id) const;
  bool IsLeafNode(ui::AXNodeID ax_node_id) const;
  void OnConnected();
  void OnCopy() const;
  void OnScroll(bool on_selection) const;
  void OnLinkClicked(ui::AXNodeID ax_node_id) const;
  void OnSelectionChange(ui::AXNodeID anchor_node_id,
                         int anchor_offset,
                         ui::AXNodeID focus_node_id,
                         int focus_offset);
  void OnCollapseSelection() const;
  bool IsGoogleDocs() const;
  bool IsReadAloudEnabled() const;
  bool IsChromeOsAsh() const;
  bool IsPhraseHighlightingEnabled() const;
  void OnLetterSpacingChange(int value);
  void OnLineSpacingChange(int value);
  void OnThemeChange(int value);
  void OnFontChange(const std::string& font);
  void OnSpeechRateChange(double rate);
  void OnVoiceChange(const std::string& voice, const std::string& lang);
  void OnLanguagePrefChange(const std::string& lang, bool enabled);
  bool RequiresDistillation();
  void OnHighlightGranularityChanged(int granularity);
  double GetLineSpacingValue(int line_spacing) const;
  double GetLetterSpacingValue(int letter_spacing) const;
  std::vector<std::string> GetSupportedFonts();
  void RequestImageDataUrl(ui::AXNodeID node_id) const;
  std::string GetImageDataUrl(ui::AXNodeID node_id) const;
  v8::Local<v8::Value> GetImageBitmap(ui::AXNodeID node_id);
  void OnIsSpeechActiveChanged(bool is_speech_active);
  void OnIsAudioCurrentlyPlayingChanged(bool is_audio_currently_playing);
  std::string GetValidatedFontName(const std::string& font) const;
  std::vector<std::string> GetAllFonts() const;
  void OnScrolledToBottom();
  bool IsDocsLoadMoreButtonVisible() const;
  void OnNoTextContent(bool previouslyHadContent);

  // The language code that should be used to determine which voices are
  // supported for speech.
  const std::string& GetLanguageCodeForSpeech() const;

  const std::string GetDisplayNameForLocale(
      const std::string& locale,
      const std::string& display_locale) const;

  // Returns a list of AXNodeIds representing the next nodes that should be
  // spoken and highlighted with Read Aloud.
  // This defaults to returning the first granularity until
  // MovePositionTo<Next,Previous>Granularity() moves the position.
  // If the the current processed_granularity_index_ has not been calculated
  // yet, GetNextNodes() is called which updates the AXPosition.
  // GetCurrentTextStartIndex and GetCurrentTextEndIndex called with an AXNodeID
  // return by GetCurrentText will return the starting text and ending text
  // indices for specific text that should be referenced within the node.
  std::vector<ui::AXNodeID> GetCurrentText();

  // Returns the Read Aloud starting text index for a node. For example,
  // if the entire text of the node should be read by Read Aloud at a particular
  // moment, this will return 0. Returns -1 if the node isn't in the current
  // segment.
  int GetCurrentTextStartIndex(ui::AXNodeID node_id);

  // Returns the Read Aloud ending text index for a node. For example,
  // if the entire text of the node should be read by Read Aloud at a particular
  // moment, this will return the length of the node's text. Returns -1 if the
  // node isn't in the current segment.
  int GetCurrentTextEndIndex(ui::AXNodeID node_id);

  int GetAccessibleBoundary(const std::u16string& text, int max_text_length);

  // Called when a new dependency parser model file has been loaded and is
  // available.
  void UpdateDependencyParserModel(base::File model_file);

  DependencyParserModel& GetDependencyParserModelForTesting();

  // Called when distillation has completed.
  void OnAXTreeDistilled(const ui::AXTreeID& tree_id,
                         const std::vector<ui::AXNodeID>& content_node_ids);

  // Inits the AXPosition with a starting node.
  // TODO(crbug.com/40927698): We should be able to use AXPosition in a way
  // where this isn't needed.
  void InitAXPositionWithNode(const ui::AXNodeID& starting_node_id);

  // Increments the processed_granularity_index_, updating ReadAloud's state of
  // the current granularity to refer to the next granularity. The current
  // behavior allows the client to increment past the end of the page's content.
  void MovePositionToNextGranularity();

  // Decrements the processed_granularity_index_,updating ReadAloud's state of
  // the current granularity to refer to the previous granularity
  void MovePositionToPreviousGranularity();

  void ResetGranularityIndex();

  // Preprocess the text on the current page for speech to be used by
  // Read Aloud.
  void PreprocessTextForSpeech();

  void Draw(bool recompute_display_nodes);

  // Snapshot_lite is a data structure which resembles an
  // AXTreeUpdate. E.g.:
  //   const axTree = {
  //     root_id: 1,
  //     nodes: [
  //       {
  //         id: 1,
  //         role: 'rootWebArea',
  //         child_ids: [2],
  //       },
  //       {
  //         id: 2,
  //         role: 'staticText',
  //         name: 'Some text.',
  //       },
  //     ],
  //   };
  void SetContentForTesting(v8::Local<v8::Value> v8_snapshot_lite,
                            std::vector<ui::AXNodeID> content_node_ids);
  void SetLanguageForTesting(const std::string& language_code);

 private:
  friend ReadAnythingAppControllerTest;
  friend ReadAnythingAppControllerScreen2xDataCollectionModeTest;

  explicit ReadAnythingAppController(content::RenderFrame* render_frame);
  ~ReadAnythingAppController() override;

  // The fallback language code if GetLanguageCodeForSpeech has an error.
  // However, this may be the same value as GetLanguageCodeForSpeech.
  const std::string& GetDefaultLanguageCodeForSpeech() const;

  void Distill(bool for_training_data = false);
  void DrawSelection();
  void DrawEmptyState();

  void ExecuteJavaScript(const std::string& script);

  // Returns true if a draw occured.
  bool PostProcessSelection();

  // Signals that the side panel has finished loading and it's safe to show
  // the UI to avoid loading artifacts.
  void ShouldShowUI();

  // Helper for forwarding various updates to the webui based on the latest
  // processed accessibility events.
  void SendEventUpdates();

  // Records the number of selections that occurred for the active page. Called
  // when the active tree changes.
  void RecordNumSelections();

  // Given a boundary position within the current granularity, identifies the
  // nodes that needs to be highlighted (e.g. until the word boundary), and
  // returns a list containing nodes and the ranges within those nodes. The
  // ranges are represented as a start offset and a length. Multiple nodes are
  // returned if the highlight spans over more than one node. This allows us to
  // correctly position the highlight within the current text segment. The
  // return value is thus a list containing node id, start, and length.
  //
  //  If the `phrases` argument is `true`, the text ranges for the containing
  //  phrase are returned, otherwise the text ranges for the word are returned.
  //
  // Note that this is only needed for custom granularity highlighting. Sentence
  // highlighting is able to be handled directly in WebUI because the entire
  // speech segment is highlighted at once.
  v8::Local<v8::Value> GetHighlightForCurrentSegmentIndex(int index,
                                                          bool phrases);

  // Helpers for logging UmaHistograms based on times recorded in WebUI.
  void IncrementMetricCount(const std::string& metric);

  void LogSpeechStop(int source);

  void OnUrlInformationSet();

  // Stores a screenshot of the page and triggers distillation to record protos.
  // This function is not used in production and is behind the disabled
  // `DataCollectionModeForScreen2x` flag.
  // This function is expected to be called just once. There would be a mismatch
  // between the training protos and the screenshot if it runs more than once.
  void DistillAndScreenshot();

  std::unique_ptr<AXTreeDistiller> distiller_;
  mojo::Remote<read_anything::mojom::UntrustedPageHandlerFactory>
      page_handler_factory_;
  mojo::Remote<read_anything::mojom::UntrustedPageHandler> page_handler_;
  mojo::Receiver<read_anything::mojom::UntrustedPage> receiver_{this};

  std::map<ui::AXNodeID, SkBitmap> downloaded_images_;

  // Model that holds Read Aloud state for this controller.
  ReadAloudAppModel read_aloud_model_;

  // Set of nodes that will be deleted that are also displayed. A draw will
  // occur when the set becomes empty.
  std::set<ui::AXNodeID> displayed_nodes_pending_deletion_;

  // Model that holds Reading mode state for this controller.
  ReadAnythingAppModel model_;

  // Observer of `model_`, which gets notified when trees are added / removed.
  base::ScopedObservation<ReadAnythingAppModel,
                          ReadAnythingAppModel::ModelObserver>
      model_observer_{this};

  // Observers of AXTrees, which are added / removed  as the `model_` changes
  // state.
  std::deque<
      std::unique_ptr<base::ScopedObservation<ui::AXTree, ui::AXTreeObserver>>>
      tree_observers_;

  // For metrics logging
  std::unique_ptr<ukm::MojoUkmRecorder> ukm_recorder_;

  // The time when the renderer constructor is first triggered.
  base::TimeTicks renderer_load_triggered_time_ms_;

  // The time when the WebUI connects i.e. when onConnected is called.
  base::TimeTicks web_ui_connected_time_ms_;

  // A timer that causes a distillation after a user stops typing for a set
  // number of seconds.
  std::unique_ptr<base::RetainingOneShotTimer> post_user_entry_draw_timer_;

  base::WeakPtrFactory<ReadAnythingAppController> weak_ptr_factory_{this};
};

#endif  // CHROME_RENDERER_ACCESSIBILITY_READ_ANYTHING_READ_ANYTHING_APP_CONTROLLER_H_
