// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/tabs/browser_tab_strip_controller.h"

#include <limits>
#include <memory>
#include <optional>
#include <utility>

#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/user_metrics.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/browser/autocomplete/autocomplete_classifier_factory.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/favicon/favicon_utils.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_external.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_source.h"
#include "chrome/browser/search/search.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_command_controller.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/public/tab_features.h"
#include "chrome/browser/ui/tabs/saved_tab_groups/saved_tab_group_utils.h"
#include "chrome/browser/ui/tabs/split_tab_util.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_group_deletion_dialog_controller.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_menu_model.h"
#include "chrome/browser/ui/tabs/tab_network_state.h"
#include "chrome/browser/ui/tabs/tab_renderer_data.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_delegate.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/tabs/tab_strip_user_gesture_details.h"
#include "chrome/browser/ui/tabs/tab_utils.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/views/tabs/dragging/tab_drag_controller.h"
#include "chrome/browser/ui/views/tabs/tab.h"
#include "chrome/browser/ui/views/tabs/tab_strip.h"
#include "chrome/browser/ui/views/tabs/tab_strip_types.h"
#include "chrome/browser/ui/web_applications/app_browser_controller.h"
#include "chrome/browser/ui/web_applications/web_app_tabbed_utils.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "components/feature_engagement/public/feature_constants.h"
#include "components/feature_engagement/public/tracker.h"
#include "components/omnibox/browser/autocomplete_classifier.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/performance_manager/public/user_tuning/prefs.h"
#include "components/saved_tab_groups/public/features.h"
#include "components/saved_tab_groups/public/saved_tab_group.h"
#include "components/saved_tab_groups/public/tab_group_sync_service.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/split_tab_id.h"
#include "components/tabs/public/split_tab_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/peak_gpu_memory_tracker_factory.h"
#include "content/public/browser/web_contents.h"
#include "ipc/ipc_message.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/list_selection_model.h"
#include "ui/base/models/menu_model.h"
#include "ui/base/mojom/menu_source_type.mojom-forward.h"
#include "ui/compositor/compositor.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/range/range.h"
#include "ui/gfx/text_elider.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/widget/widget.h"
#include "url/origin.h"

#if BUILDFLAG(IS_CHROMEOS)
#include "ash/public/cpp/window_properties.h"
#include "chromeos/ash/experiences/system_web_apps/types/system_web_app_delegate.h"
#endif  // BUILDFLAG(IS_CHROMEOS)

#if defined(USE_AURA)
#include "ui/aura/window.h"
#endif

using base::UserMetricsAction;
using content::WebContents;

namespace {

// Gets the source browser view during a tab dragging. Returns nullptr if there
// is none.
BrowserView* GetSourceBrowserViewInTabDragging() {
  auto* source_context = TabDragController::GetSourceContext();
  if (source_context) {
    gfx::NativeWindow source_window =
        source_context->GetWidget()->GetNativeWindow();
    if (source_window) {
      return BrowserView::GetBrowserViewForNativeWindow(source_window);
    }
  }
  return nullptr;
}

void DialogTimingToSource(
    base::OnceCallback<void(CloseTabSource)> callback,
    CloseTabSource source,
    tab_groups::DeletionDialogController::DeletionDialogTiming timing) {
  switch (timing) {
    case tab_groups::DeletionDialogController::DeletionDialogTiming::
        Synchronous: {
      std::move(callback).Run(source);
      return;
    }
    case tab_groups::DeletionDialogController::DeletionDialogTiming::
        Asynchronous: {
      std::move(callback).Run(CloseTabSource::kFromNonUIEvent);
      return;
    }
  }
}

}  // namespace

class BrowserTabStripController::TabContextMenuContents
    : public ui::SimpleMenuModel::Delegate {
 public:
  TabContextMenuContents(Tab* tab, BrowserTabStripController* controller)
      : tab_(tab), controller_(controller) {
    model_ = controller_->menu_model_factory_->Create(
        this,
        controller->GetBrowserWindowInterface()
            ->GetFeatures()
            .tab_menu_model_delegate(),
        controller->model_,
        controller->tabstrip_->GetModelIndexOf(tab).value());

    // Because we use "new" badging for feature promos, we cannot use system-
    // native context menus. (See crbug.com/1109256.)
    const int run_flags =
        views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU;
    menu_runner_ = std::make_unique<views::MenuRunner>(
        model_.get(), run_flags,
        base::BindRepeating(&TabContextMenuContents::OnMenuClosed,
                            base::Unretained(this)));
  }
  TabContextMenuContents(const TabContextMenuContents&) = delete;
  TabContextMenuContents& operator=(const TabContextMenuContents&) = delete;

  void Cancel() { controller_ = nullptr; }

  void CloseMenu() {
    if (menu_runner_) {
      menu_runner_->Cancel();
    }
  }

  void OnMenuClosed() { tab_ = nullptr; }

  void RunMenuAt(const gfx::Point& point,
                 ui::mojom::MenuSourceType source_type) {
    menu_runner_->RunMenuAt(tab_->GetWidget(), nullptr,
                            gfx::Rect(point, gfx::Size()),
                            views::MenuAnchorPosition::kTopLeft, source_type);
  }

  // Overridden from ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override { return false; }
  bool IsCommandIdEnabled(int command_id) const override {
    return controller_->IsCommandEnabledForTab(
        static_cast<TabStripModel::ContextMenuCommand>(command_id), tab_);
  }

  bool IsCommandIdAlerted(int command_id) const override { return false; }

  bool GetAcceleratorForCommandId(int command_id,
                                  ui::Accelerator* accelerator) const override {
#if BUILDFLAG(IS_CHROMEOS)
    auto* browser = controller_->browser_view_->browser();
    auto* system_app = browser->app_controller()
                           ? browser->app_controller()->system_app()
                           : nullptr;
    if (system_app && !system_app->ShouldShowTabContextMenuShortcut(
                          browser->profile(), command_id)) {
      return false;
    }
#endif  // BUILDFLAG(IS_CHROMEOS)

    int browser_cmd;
    return TabStripModel::ContextMenuCommandToBrowserCommand(command_id,
                                                             &browser_cmd) &&
           controller_->tabstrip_->GetWidget()->GetAccelerator(browser_cmd,
                                                               accelerator);
  }
  void ExecuteCommand(int command_id, int event_flags) override {
    // Executing the command destroys `this`, and can also end up destroying
    // `controller_`. So stop the highlights before executing the command.
    controller_->ExecuteCommandForTab(
        static_cast<TabStripModel::ContextMenuCommand>(command_id), tab_);
  }

 private:
  std::unique_ptr<ui::SimpleMenuModel> model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;

  // The tab we're showing a menu for.
  raw_ptr<Tab> tab_;

  // A pointer back to our hosting controller, for command state information.
  raw_ptr<BrowserTabStripController> controller_;
};

////////////////////////////////////////////////////////////////////////////////
// BrowserTabStripController, public:

BrowserTabStripController::BrowserTabStripController(
    TabStripModel* model,
    BrowserView* browser_view,
    std::unique_ptr<TabMenuModelFactory> menu_model_factory_override)
    : model_(model),
      tabstrip_(nullptr),
      browser_view_(browser_view),
      hover_tab_selector_(model),
      menu_model_factory_(std::move(menu_model_factory_override)) {
  if (!menu_model_factory_) {
    // Use the default one.
    menu_model_factory_ = std::make_unique<TabMenuModelFactory>();
  }
  model_->SetTabStripUI(this);

  should_show_discard_indicator_ = g_browser_process->local_state()->GetBoolean(
      performance_manager::user_tuning::prefs::kDiscardRingTreatmentEnabled);
  local_state_registrar_.Init(g_browser_process->local_state());
  local_state_registrar_.Add(
      performance_manager::user_tuning::prefs::kDiscardRingTreatmentEnabled,
      base::BindRepeating(
          &BrowserTabStripController::OnDiscardRingTreatmentEnabledChanged,
          base::Unretained(this)));
}

BrowserTabStripController::~BrowserTabStripController() {
  // When we get here the TabStrip is being deleted. We need to explicitly
  // cancel the menu, otherwise it may try to invoke something on the tabstrip
  // from its destructor.
  if (context_menu_contents_.get()) {
    context_menu_contents_->Cancel();
  }

  model_->RemoveObserver(this);
}

void BrowserTabStripController::InitFromModel(TabStrip* tabstrip) {
  tabstrip_ = tabstrip;

  // Walk the model, calling our insertion observer method for each item within
  // it.
  std::vector<std::pair<WebContents*, int>> tabs_to_add;
  for (int i = 0; i < model_->count(); ++i) {
    tabs_to_add.emplace_back(model_->GetWebContentsAt(i), i);
  }
  AddTabs(tabs_to_add);
}

bool BrowserTabStripController::IsCommandEnabledForTab(
    TabStripModel::ContextMenuCommand command_id,
    const Tab* tab) const {
  const std::optional<int> model_index = tabstrip_->GetModelIndexOf(tab);
  return model_index.has_value() ? model_->IsContextMenuCommandEnabled(
                                       model_index.value(), command_id)
                                 : false;
}

void BrowserTabStripController::ExecuteCommandForTab(
    TabStripModel::ContextMenuCommand command_id,
    const Tab* tab) {
  const std::optional<int> model_index = tabstrip_->GetModelIndexOf(tab);
  if (model_index.has_value()) {
    model_->ExecuteContextMenuCommand(model_index.value(), command_id);
  }
}

bool BrowserTabStripController::IsTabPinned(const Tab* tab) const {
  return IsTabPinned(tabstrip_->GetModelIndexOf(tab).value());
}

const ui::ListSelectionModel& BrowserTabStripController::GetSelectionModel()
    const {
  return model_->selection_model();
}

int BrowserTabStripController::GetCount() const {
  return model_->count();
}

bool BrowserTabStripController::CanShowModalUI() const {
  return model_->CanShowModalUI();
}

std::unique_ptr<ScopedTabStripModalUI>
BrowserTabStripController::ShowModalUI() {
  return model_->ShowModalUI();
}

bool BrowserTabStripController::IsValidIndex(int index) const {
  return model_->ContainsIndex(index);
}

bool BrowserTabStripController::IsActiveTab(int model_index) const {
  return GetActiveIndex() == model_index;
}

std::optional<int> BrowserTabStripController::GetActiveIndex() const {
  const int active_index = model_->active_index();
  if (IsValidIndex(active_index)) {
    return active_index;
  }
  return std::nullopt;
}

bool BrowserTabStripController::IsTabSelected(int model_index) const {
  return model_->IsTabSelected(model_index);
}

bool BrowserTabStripController::IsTabPinned(int model_index) const {
  return model_->ContainsIndex(model_index) && model_->IsTabPinned(model_index);
}

void BrowserTabStripController::SelectTab(int model_index,
                                          const ui::Event& event) {
  // When selecting a split tab, activate the most recently focused tab in the
  // split.
  std::optional<split_tabs::SplitTabId> split_id =
      tabstrip_->tab_at(model_index)->split();
  if (split_id.has_value()) {
    model_index = split_tabs::GetIndexOfLastActiveTab(
        browser()->tab_strip_model(), split_id.value());
  }

  std::unique_ptr<viz::PeakGpuMemoryTracker> tracker =
      content::PeakGpuMemoryTrackerFactory::Create(
          viz::PeakGpuMemoryTracker::Usage::CHANGE_TAB);

  TabStripUserGestureDetails gesture_detail(
      TabStripUserGestureDetails::GestureType::kOther, event.time_stamp());
  TabStripUserGestureDetails::GestureType type =
      TabStripUserGestureDetails::GestureType::kOther;
  if (event.type() == ui::EventType::kMousePressed) {
    type = TabStripUserGestureDetails::GestureType::kMouse;
  } else if (event.type() == ui::EventType::kGestureTapDown) {
    type = TabStripUserGestureDetails::GestureType::kTouch;
  }
  gesture_detail.type = type;
  model_->ActivateTabAt(model_index, gesture_detail);

  tabstrip_->GetWidget()
      ->GetCompositor()
      ->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce(
          [](std::unique_ptr<viz::PeakGpuMemoryTracker> tracker,
             const viz::FrameTimingDetails& frame_timing_details) {
            // This callback will be ran once the ui::Compositor presents the
            // next frame for the `tabstrip_`. The destruction of `tracker` will
            // get the peak GPU memory and record a histogram.
          },
          std::move(tracker)));
}

void BrowserTabStripController::RecordMetricsOnTabSelectionChange(
    std::optional<tab_groups::TabGroupId> group) {
  base::UmaHistogramEnumeration("TabStrip.Tab.Views.ActivationAction",
                                TabActivationTypes::kTab);

  if (!group) {
    return;
  }

  base::RecordAction(base::UserMetricsAction("TabGroups_SwitchGroupedTab"));

  if (!tab_groups::SavedTabGroupUtils::SupportsSharedTabGroups()) {
    return;
  }

  tab_groups::TabGroupSyncService* tab_group_service =
      tab_groups::SavedTabGroupUtils::GetServiceForProfile(
          browser_view_->GetProfile());

  if (!tab_group_service) {
    return;
  }

  std::optional<tab_groups::SavedTabGroup> saved_group =
      tab_group_service->GetGroup(group.value());
  if (saved_group && saved_group->collaboration_id()) {
    base::RecordAction(
        base::UserMetricsAction("TabGroups.Shared.SwitchGroupedTab"));
  }
}

void BrowserTabStripController::ExtendSelectionTo(int model_index) {
  model_->ExtendSelectionTo(model_index);
}

void BrowserTabStripController::ToggleSelected(int model_index) {
  if (model_->IsTabSelected(model_index)) {
    model_->DeselectTabAt(model_index);
  } else {
    model_->SelectTabAt(model_index);
  }
}

void BrowserTabStripController::AddSelectionFromAnchorTo(int model_index) {
  model_->AddSelectionFromAnchorTo(model_index);
}

void BrowserTabStripController::OnCloseTab(
    int model_index,
    CloseTabSource source,
    base::OnceCallback<void(CloseTabSource)> callback) {
  if (!web_app::IsTabClosable(model_, model_index)) {
    return;
  }

#if BUILDFLAG(IS_CHROMEOS)
  // Tabs cannot be closed when the app is locked for OnTask. Only relevant for
  // non-web browser scenarios.
  if (browser_view_->browser()->IsLockedForOnTask()) {
    return;
  }
#endif

  // Only consider pausing the close operation if this is the last remaining
  // tab (since otherwise closing it won't close the browser window).
  if (GetCount() <= 1) {
    // Closing this tab will close the current window. See if the browser wants
    // to prompt the user before the browser is allowed to close.
    const Browser::WarnBeforeClosingResult result =
        browser_view_->browser()->MaybeWarnBeforeClosing(base::BindOnce(
            [](TabStrip* tab_strip, int model_index,
               Browser::WarnBeforeClosingResult result) {
              if (result == Browser::WarnBeforeClosingResult::kOkToClose) {
                tab_strip->CloseTab(tab_strip->tab_at(model_index),
                                    CloseTabSource::kFromNonUIEvent);
              }
            },
            base::Unretained(tabstrip_), model_index));

    if (result != Browser::WarnBeforeClosingResult::kOkToClose) {
      return;
    }
  }

  // Check to make sure the tab is not the last in it's group.
  std::vector<tab_groups::TabGroupId> groups_to_delete =
      model_->GetGroupsDestroyedFromRemovingIndices({model_index});

  if (groups_to_delete.empty()) {
    std::move(callback).Run(source);
    return;
  }

  auto timing_mapped_callback =
      base::BindOnce(&DialogTimingToSource, std::move(callback), source);

  // If the user is destroying the last tab in a saved or shared group via the
  // tabstrip, a dialog is shown that will decide whether to destroy the tab or
  // not. It will first ungroup the tab, then close the tab.
  tab_groups::SavedTabGroupUtils::MaybeShowSavedTabGroupDeletionDialog(
      browser_view_->browser(), tab_groups::GroupDeletionReason::ClosedLastTab,
      groups_to_delete, std::move(timing_mapped_callback));
}

void BrowserTabStripController::CloseTab(int model_index) {
  // Cancel any pending tab transition.
  hover_tab_selector_.CancelTabTransition();

  model_->CloseWebContentsAt(model_index,
                             TabCloseTypes::CLOSE_USER_GESTURE |
                                 TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);

  // Try to show reading list IPH if needed.
  if (tabstrip_->GetTabCount() >= 7) {
    browser_view_->MaybeShowFeaturePromo(
        feature_engagement::kIPHReadingListEntryPointFeature);
  }
}

void BrowserTabStripController::ToggleTabAudioMute(int model_index) {
  content::WebContents* const contents = model_->GetWebContentsAt(model_index);
  bool mute_tab = !contents->IsAudioMuted();
  UMA_HISTOGRAM_BOOLEAN("Media.Audio.TabAudioMuted", mute_tab);
  SetTabAudioMuted(contents, mute_tab, TabMutedReason::AUDIO_INDICATOR,
                   std::string());
}

void BrowserTabStripController::AddTabToGroup(
    int model_index,
    const tab_groups::TabGroupId& group) {
  model_->AddToExistingGroup({model_index}, group);
}

void BrowserTabStripController::RemoveTabFromGroup(int model_index) {
  model_->RemoveFromGroup({model_index});
}

void BrowserTabStripController::MoveTab(int start_index, int final_index) {
  model_->MoveWebContentsAt(start_index, final_index, false);
}

void BrowserTabStripController::MoveGroup(const tab_groups::TabGroupId& group,
                                          int final_index) {
  model_->MoveGroupTo(group, final_index);
}

void BrowserTabStripController::ToggleTabGroupCollapsedState(
    const tab_groups::TabGroupId group,
    ToggleTabGroupCollapsedStateOrigin origin) {
  const bool is_currently_collapsed = IsGroupCollapsed(group);
  bool should_toggle_group = true;

  if (!is_currently_collapsed && GetActiveIndex().has_value()) {
    const int active_index = GetActiveIndex().value();
    if (model_->GetTabGroupForTab(active_index) == group) {
      // If the active tab is in the group that is toggling to collapse, the
      // active tab should switch to the next available tab. If there are no
      // available tabs for the active tab to switch to, a new tab will
      // be created.
      const std::optional<int> next_active =
          model_->GetNextExpandedActiveTab(group);
      if (next_active.has_value()) {
        model_->ActivateTabAt(
            next_active.value(),
            TabStripUserGestureDetails(
                TabStripUserGestureDetails::GestureType::kOther));
      } else {
        // Create a new tab that will automatically be activated
        should_toggle_group = false;
        CreateNewTab();
      }
    } else {
      // If the active tab is not in the group that is toggling to collapse,
      // reactive the active tab to deselect any other potentially selected
      // tabs.
      model_->ActivateTabAt(
          active_index, TabStripUserGestureDetails(
                            TabStripUserGestureDetails::GestureType::kOther));
    }
  }

  if (origin != ToggleTabGroupCollapsedStateOrigin::kMenuAction ||
      should_toggle_group) {
    tabstrip_->ToggleTabGroup(group, !is_currently_collapsed, origin);
    model_->ChangeTabGroupVisuals(
        group,
        tab_groups::TabGroupVisualData(GetGroupTitle(group),
                                       GetGroupColorId(group),
                                       !is_currently_collapsed),
        true);
  }

  const bool is_implicit_action =
      origin == ToggleTabGroupCollapsedStateOrigin::kMenuAction ||
      origin == ToggleTabGroupCollapsedStateOrigin::kTabsSelected;
  if (!is_implicit_action) {
    if (is_currently_collapsed) {
      base::RecordAction(
          base::UserMetricsAction("TabGroups_TabGroupHeader_Expanded"));
    } else {
      base::RecordAction(
          base::UserMetricsAction("TabGroups_TabGroupHeader_Collapsed"));
    }
  }
}

void BrowserTabStripController::ShowContextMenuForTab(
    Tab* tab,
    const gfx::Point& p,
    ui::mojom::MenuSourceType source_type) {
  context_menu_contents_ = std::make_unique<TabContextMenuContents>(tab, this);
  context_menu_contents_->RunMenuAt(p, source_type);
  base::UmaHistogramEnumeration("TabStrip.Tab.Views.ActivationAction",
                                TabActivationTypes::kContextMenu);
}

void BrowserTabStripController::CloseContextMenuForTesting() {
  if (context_menu_contents_) {
    context_menu_contents_->CloseMenu();
  }
}

int BrowserTabStripController::HasAvailableDragActions() const {
  return model_->delegate()->GetDragActions();
}

void BrowserTabStripController::OnDropIndexUpdate(
    const std::optional<int> index,
    const bool drop_before) {
  // Perform a delayed tab transition if hovering directly over a tab.
  // Otherwise, cancel the pending one.
  if (index.has_value() && !drop_before) {
    hover_tab_selector_.StartTabTransition(index.value());
  } else {
    hover_tab_selector_.CancelTabTransition();
  }
}

void BrowserTabStripController::CreateNewTab() {
  model_->delegate()->AddTabAt(GURL(), -1, true);
}

void BrowserTabStripController::CreateNewTabWithLocation(
    const std::u16string& location) {
  // Use autocomplete to clean up the text, going so far as to turn it into
  // a search query if necessary.
  AutocompleteMatch match;
  AutocompleteClassifierFactory::GetForProfile(GetProfile())
      ->Classify(location, false, false, metrics::OmniboxEventProto::BLANK,
                 &match, nullptr);
  if (match.destination_url.is_valid()) {
    model_->delegate()->AddTabAt(match.destination_url, -1, true);
  }
}

void BrowserTabStripController::OnStartedDragging(bool dragging_window) {
  if (!immersive_reveal_lock_.get()) {
    // The top-of-window views should be revealed while the user is dragging
    // tabs in immersive fullscreen. The top-of-window views may not be already
    // revealed if the user is attempting to attach a tab to a tabstrip
    // belonging to an immersive fullscreen window.
    immersive_reveal_lock_ =
        browser_view_->immersive_mode_controller()->GetRevealedLock(
            ImmersiveModeController::ANIMATE_REVEAL_NO);
  }

  browser_view_->frame()->SetTabDragKind(dragging_window ? TabDragKind::kAllTabs
                                                         : TabDragKind::kTab);
  // We also use fast resize for the source browser window as the source browser
  // window may also change bounds during dragging.
  BrowserView* source_browser_view = GetSourceBrowserViewInTabDragging();
  if (source_browser_view && source_browser_view != browser_view_) {
    source_browser_view->frame()->SetTabDragKind(TabDragKind::kTab);
  }

#if BUILDFLAG(IS_CHROMEOS)
  browser_view_->GetWidget()->GetNativeWindow()->SetProperty(
      ash::kIsDraggingTabsKey, true);
#endif  // BUILDFLAG(IS_CHROMEOS)
}

void BrowserTabStripController::OnStoppedDragging() {
  immersive_reveal_lock_.reset();

  BrowserView* source_browser_view = GetSourceBrowserViewInTabDragging();
  // Only reset the source window's fast resize bit after the entire drag
  // ends.
  if (browser_view_ != source_browser_view) {
    browser_view_->frame()->SetTabDragKind(TabDragKind::kNone);
  }
  if (source_browser_view && !TabDragController::IsActive()) {
    source_browser_view->frame()->SetTabDragKind(TabDragKind::kNone);
  }

#if BUILDFLAG(IS_CHROMEOS)
  browser_view_->GetWidget()->GetNativeWindow()->ClearProperty(
      ash::kIsDraggingTabsKey);
#endif  // BUILDFLAG(IS_CHROMEOS)
}

void BrowserTabStripController::OnKeyboardFocusedTabChanged(
    std::optional<int> index) {
  browser_view_->browser()->command_controller()->TabKeyboardFocusChangedTo(
      index);
}

std::u16string BrowserTabStripController::GetGroupTitle(
    const tab_groups::TabGroupId& group) const {
  return model_->group_model()->GetTabGroup(group)->visual_data()->title();
}

// TODO(crbug.com/418774949) Combine with ExistingTabGroupSubMenuModel and move
// To TabGroupFeatures.
std::u16string BrowserTabStripController::GetGroupContentString(
    const tab_groups::TabGroupId& group) const {
  CHECK(model_->SupportsTabGroups());

  const TabGroup* tab_group = model_->group_model()->GetTabGroup(group);
  CHECK(tab_group);

  constexpr size_t kContextMenuTabTitleMaxLength = 30;
  std::u16string format_string = l10n_util::GetPluralStringFUTF16(
      IDS_TAB_CXMENU_PLACEHOLDER_GROUP_TITLE, tab_group->tab_count() - 1);
  std::u16string short_title;
  gfx::ElideString(
      tab_group->GetFirstTab()->GetTabFeatures()->tab_ui_helper()->GetTitle(),
      kContextMenuTabTitleMaxLength, &short_title);
  return base::ReplaceStringPlaceholders(format_string, short_title, nullptr);
}

tab_groups::TabGroupColorId BrowserTabStripController::GetGroupColorId(
    const tab_groups::TabGroupId& group) const {
  return model_->group_model()->GetTabGroup(group)->visual_data()->color();
}

TabGroup* BrowserTabStripController::GetTabGroup(
    const tab_groups::TabGroupId& group_id) const {
  return model_->group_model()->GetTabGroup(group_id);
}

bool BrowserTabStripController::IsGroupCollapsed(
    const tab_groups::TabGroupId& group) const {
  return model_->group_model()->ContainsTabGroup(group) &&
         model_->group_model()
             ->GetTabGroup(group)
             ->visual_data()
             ->is_collapsed();
}

void BrowserTabStripController::SetVisualDataForGroup(
    const tab_groups::TabGroupId& group,
    const tab_groups::TabGroupVisualData& visual_data) {
  model_->ChangeTabGroupVisuals(group, visual_data);
}

std::optional<int> BrowserTabStripController::GetFirstTabInGroup(
    const tab_groups::TabGroupId& group) const {
  tabs::TabInterface* tab =
      model_->group_model()->GetTabGroup(group)->GetFirstTab();
  if (!tab) {
    return std::nullopt;
  }
  return model_->GetIndexOfTab(tab);
}

gfx::Range BrowserTabStripController::ListTabsInGroup(
    const tab_groups::TabGroupId& group) const {
  return model_->group_model()->GetTabGroup(group)->ListTabs();
}

bool BrowserTabStripController::IsFrameCondensed() const {
  return GetFrameView()->IsFrameCondensed();
}

bool BrowserTabStripController::HasVisibleBackgroundTabShapes() const {
  return GetFrameView()->HasVisibleBackgroundTabShapes(
      BrowserFrameActiveState::kUseCurrent);
}

bool BrowserTabStripController::EverHasVisibleBackgroundTabShapes() const {
  return GetFrameView()->EverHasVisibleBackgroundTabShapes();
}

bool BrowserTabStripController::CanDrawStrokes() const {
  return GetFrameView()->CanDrawStrokes();
}

SkColor BrowserTabStripController::GetFrameColor(
    BrowserFrameActiveState active_state) const {
  return GetFrameView()->GetFrameColor(active_state);
}

std::optional<int> BrowserTabStripController::GetCustomBackgroundId(
    BrowserFrameActiveState active_state) const {
  return GetFrameView()->GetCustomBackgroundId(active_state);
}

std::u16string BrowserTabStripController::GetAccessibleTabName(
    const Tab* tab) const {
  return browser_view_->GetAccessibleTabLabel(
      tabstrip_->GetModelIndexOf(tab).value(), /*is_for_tab=*/true);
}

Profile* BrowserTabStripController::GetProfile() const {
  return model_->profile();
}

BrowserWindowInterface* BrowserTabStripController::GetBrowserWindowInterface() {
  return browser_view_->browser();
}

Browser* BrowserTabStripController::GetBrowser() {
  return browser_view_->browser();
}

#if BUILDFLAG(IS_CHROMEOS)
bool BrowserTabStripController::IsLockedForOnTask() {
  return browser_view_->browser()->IsLockedForOnTask();
}
#endif

////////////////////////////////////////////////////////////////////////////////
// BrowserTabStripController, TabStripModelObserver implementation:

void BrowserTabStripController::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  switch (change.type()) {
    case TabStripModelChange::kInserted: {
      std::vector<std::pair<WebContents*, int>> tabs_to_add;
      for (const auto& contents : change.GetInsert()->contents) {
        DCHECK(model_->ContainsIndex(contents.index));
        tabs_to_add.emplace_back(contents.contents, contents.index);
      }
      AddTabs(tabs_to_add);
      break;
    }
    case TabStripModelChange::kRemoved: {
      for (const auto& contents : change.GetRemove()->contents) {
        hover_tab_selector_.CancelTabTransition();
        tabstrip_->RemoveTabAt(contents.contents, contents.index,
                               contents.contents == selection.old_contents);
      }
      break;
    }
    case TabStripModelChange::kMoved: {
      auto* move = change.GetMove();
      // Cancel any pending tab transition.
      hover_tab_selector_.CancelTabTransition();

      // A move may have resulted in the pinned state changing, so pass in a
      // TabRendererData.
      tabstrip_->MoveTab(
          move->from_index, move->to_index,
          TabRendererData::FromTabInModel(model_, move->to_index));
      break;
    }
    case TabStripModelChange::kReplaced: {
      auto* replace = change.GetReplace();
      SetTabDataAt(replace->new_contents, replace->index);
      break;
    }
    case TabStripModelChange::kSelectionOnly:
      break;
  }

  if (tab_strip_model->empty()) {
    return;
  }

  if (selection.active_tab_changed()) {
    // It's possible for `new_contents` to be null when the final tab in a tab
    // strip is closed.
    content::WebContents* const new_contents = selection.new_contents;
    tabs::TabInterface* const new_tab_interface = selection.new_tab;
    std::optional<size_t> index = selection.new_model.active();
    if (new_contents && new_tab_interface && index.has_value()) {
      new_tab_interface->GetTabFeatures()
          ->tab_ui_helper()
          ->SetWasActiveAtLeastOnce();
      SetTabDataAt(new_contents, index.value());
    }
  }

  if (selection.selection_changed()) {
    tabstrip_->SetSelection(selection.new_model);
  }
}

void BrowserTabStripController::OnTabWillBeAdded() {
  tabstrip_->EndDrag(EndDragReason::END_DRAG_MODEL_ADDED_TAB);
}

void BrowserTabStripController::OnTabWillBeRemoved(
    content::WebContents* contents,
    int index) {
  tabstrip_->OnTabWillBeRemoved(contents, index);
}

void BrowserTabStripController::OnTabGroupChanged(
    const TabGroupChange& change) {
  switch (change.type) {
    case TabGroupChange::kCreated: {
      tabstrip_->OnGroupCreated(change.group);
      // Add tabs to the correct group if the group if re-inserted from a
      // different tabstrip as it is not an empty tab group.
      if (change.GetCreateChange()->reason() ==
          TabGroupChange::TabGroupCreationReason::
              kInsertedFromAnotherTabstrip) {
        const gfx::Range tabs_in_group =
            change.model->group_model()->GetTabGroup(change.group)->ListTabs();

        for (int i = static_cast<int>(tabs_in_group.start());
             i < static_cast<int>(tabs_in_group.end()); i++) {
          tabstrip_->AddTabToGroup(change.group, i);
        }
        tabstrip_->OnGroupContentsChanged(change.group);
      }
      break;
    }
    case TabGroupChange::kEditorOpened: {
      tabstrip_->OnGroupEditorOpened(change.group);
      break;
    }
    case TabGroupChange::kVisualsChanged: {
      const TabGroupChange::VisualsChange* visuals_delta =
          change.GetVisualsChange();
      const tab_groups::TabGroupVisualData* old_visuals =
          visuals_delta->old_visuals;
      const tab_groups::TabGroupVisualData* new_visuals =
          visuals_delta->new_visuals;
      if (old_visuals &&
          old_visuals->is_collapsed() != new_visuals->is_collapsed()) {
        gfx::Range tabs_in_group = ListTabsInGroup(change.group);
        for (auto i = tabs_in_group.start(); i < tabs_in_group.end(); ++i) {
          tabstrip_->tab_at(i)->SetVisible(!new_visuals->is_collapsed());
          if (base::FeatureList::IsEnabled(
                  features::kTabGroupsCollapseFreezing)) {
            if (new_visuals->is_collapsed()) {
              tabstrip_->tab_at(i)->CreateFreezingVote(
                  model_->GetWebContentsAt(i));
            } else {
              tabstrip_->tab_at(i)->ReleaseFreezingVote();
            }
          }
        }
      }

      tabstrip_->OnGroupVisualsChanged(change.group, old_visuals, new_visuals);
      break;
    }
    case TabGroupChange::kMoved: {
      tabstrip_->OnGroupMoved(change.group);
      break;
    }
    case TabGroupChange::kClosed: {
      tabstrip_->OnGroupClosed(change.group);
      break;
    }
  }
}

void BrowserTabStripController::TabChangedAt(WebContents* contents,
                                             int model_index,
                                             TabChangeType change_type) {
  SetTabDataAt(contents, model_index);
}

void BrowserTabStripController::TabPinnedStateChanged(
    TabStripModel* tab_strip_model,
    WebContents* contents,
    int model_index) {
  SetTabDataAt(contents, model_index);
}

void BrowserTabStripController::TabBlockedStateChanged(WebContents* contents,
                                                       int model_index) {
  SetTabDataAt(contents, model_index);
}

void BrowserTabStripController::TabGroupedStateChanged(
    TabStripModel* tab_strip_model,
    std::optional<tab_groups::TabGroupId> old_group,
    std::optional<tab_groups::TabGroupId> new_group,
    tabs::TabInterface* tab,
    int index) {
  tabstrip_->AddTabToGroup(new_group, index);

  if (old_group.has_value()) {
    tabstrip_->OnGroupContentsChanged(old_group.value());
  }

  if (new_group.has_value()) {
    tabstrip_->OnGroupContentsChanged(new_group.value());
  }
}

void BrowserTabStripController::SetTabNeedsAttentionAt(int index,
                                                       bool attention) {
  tabstrip_->SetTabNeedsAttention(index, attention);
}

bool BrowserTabStripController::IsFrameButtonsRightAligned() const {
#if BUILDFLAG(IS_MAC)
  return false;
#else
  return true;
#endif  // BUILDFLAG(IS_MAC)
}

void BrowserTabStripController::OnSplitTabChanged(
    const SplitTabChange& change) {
  if (change.type == SplitTabChange::Type::kAdded) {
    std::vector<int> split_indices;
    std::transform(
        change.GetAddedChange()->tabs().begin(),
        change.GetAddedChange()->tabs().end(),
        std::back_inserter(split_indices),
        [](const std::pair<tabs::TabInterface*, int>& p) { return p.second; });

    tabstrip_->OnSplitCreated(split_indices, change.split_id);

    // Stop animating if we are updating an active split.
    if (change.GetAddedChange()->reason() !=
        SplitTabChange::SplitTabAddReason::kNewSplitTabAdded) {
      tabstrip_->StopAnimating(true);
    }
  } else if (change.type == SplitTabChange::Type::kRemoved) {
    std::vector<int> split_indices;
    std::transform(
        change.GetRemovedChange()->tabs().begin(),
        change.GetRemovedChange()->tabs().end(),
        std::back_inserter(split_indices),
        [](const std::pair<tabs::TabInterface*, int>& p) { return p.second; });

    tabstrip_->OnSplitRemoved(split_indices);

    // Stop animating if we are updating an active split.
    if (change.GetRemovedChange()->reason() !=
        SplitTabChange::SplitTabRemoveReason::kSplitTabRemoved) {
      tabstrip_->StopAnimating(true);
    }
  } else if (change.type == SplitTabChange::Type::kContentsChanged) {
    std::vector<int> split_indices;
    std::transform(
        change.GetContentsChange()->new_tabs().begin(),
        change.GetContentsChange()->new_tabs().end(),
        std::back_inserter(split_indices),
        [](const std::pair<tabs::TabInterface*, int>& p) { return p.second; });
    tabstrip_->OnSplitContentsChanged(split_indices);
    tabstrip_->StopAnimating(true);
  }
}

BrowserNonClientFrameView* BrowserTabStripController::GetFrameView() {
  return browser_view_->frame()->GetFrameView();
}

const BrowserNonClientFrameView* BrowserTabStripController::GetFrameView()
    const {
  return browser_view_->frame()->GetFrameView();
}

void BrowserTabStripController::SetTabDataAt(content::WebContents* web_contents,
                                             int model_index) {
  tabstrip_->SetTabData(model_index,
                        TabRendererData::FromTabInModel(model_, model_index));
}

void BrowserTabStripController::AddTabs(
    std::vector<std::pair<WebContents*, int>> contents_list) {
  // Cancel any pending tab transition.
  hover_tab_selector_.CancelTabTransition();

  std::vector<std::pair<int, TabRendererData>> tabs_data;
  for (const auto& [contents, index] : contents_list) {
    tabs_data.emplace_back(index,
                           TabRendererData::FromTabInModel(model_, index));
  }

  tabstrip_->AddTabsAt(std::move(tabs_data));

  for (const auto& [contents, index] : contents_list) {
    tabstrip_->tab_at(index)->SetShouldShowDiscardIndicator(
        should_show_discard_indicator_);
  }

  // Try to show tab search IPH if needed.
  constexpr int kTabSearchIPHTriggerThreshold = 8;
  if (tabstrip_->GetTabCount() >= kTabSearchIPHTriggerThreshold) {
    browser_view_->MaybeShowFeaturePromo(
        feature_engagement::kIPHTabSearchFeature);
  }
}

void BrowserTabStripController::OnDiscardRingTreatmentEnabledChanged() {
  should_show_discard_indicator_ = g_browser_process->local_state()->GetBoolean(
      performance_manager::user_tuning::prefs::kDiscardRingTreatmentEnabled);
  for (int tab_index = 0; tab_index < tabstrip_->GetTabCount(); ++tab_index) {
    tabstrip_->tab_at(tab_index)->SetShouldShowDiscardIndicator(
        should_show_discard_indicator_);
  }
}
