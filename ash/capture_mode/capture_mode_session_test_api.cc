// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/capture_mode/capture_mode_session_test_api.h"

#include "ash/capture_mode/action_button_container_view.h"
#include "ash/capture_mode/action_button_view.h"
#include "ash/capture_mode/capture_mode_controller.h"
#include "ash/capture_mode/capture_mode_session.h"
#include "ash/capture_mode/capture_mode_types.h"
#include "ash/capture_mode/capture_region_overlay_controller.h"
#include "ash/capture_mode/recording_type_menu_view.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace ash {

CaptureModeSessionTestApi::CaptureModeSessionTestApi()
    // Will we have to change this test API to use BaseCaptureModeSession,
    // despite it not being used for the null session?
    : session_(static_cast<CaptureModeSession*>(
          CaptureModeController::Get()->capture_mode_session())) {
  DCHECK(CaptureModeController::Get()->IsActive());
  DCHECK(session_);
  CHECK_EQ(session_->session_type(), SessionType::kReal);
}

CaptureModeSessionTestApi::CaptureModeSessionTestApi(
    BaseCaptureModeSession* session)
    : session_(static_cast<CaptureModeSession*>(session)) {
  DCHECK(session_);
  CHECK_EQ(session_->session_type(), SessionType::kReal);
}

CaptureModeBarView* CaptureModeSessionTestApi::GetCaptureModeBarView() {
  return session_->capture_mode_bar_view_;
}

CaptureModeSettingsView*
CaptureModeSessionTestApi::GetCaptureModeSettingsView() {
  return session_->capture_mode_settings_view_;
}

CaptureLabelView* CaptureModeSessionTestApi::GetCaptureLabelView() {
  return session_->capture_label_view_;
}

views::Label* CaptureModeSessionTestApi::GetCaptureLabelInternalView() {
  return GetCaptureLabelView()->label_;
}

RecordingTypeMenuView* CaptureModeSessionTestApi::GetRecordingTypeMenuView() {
  auto* widget = GetRecordingTypeMenuWidget();
  DCHECK(widget);
  return static_cast<RecordingTypeMenuView*>(widget->GetContentsView());
}

views::Widget* CaptureModeSessionTestApi::GetCaptureModeSettingsWidget() {
  return session_->capture_mode_settings_widget_.get();
}

views::Widget* CaptureModeSessionTestApi::GetCaptureLabelWidget() {
  return session_->capture_label_widget_.get();
}

views::Widget* CaptureModeSessionTestApi::GetActionContainerWidget() {
  return session_->action_container_widget_.get();
}

views::Widget* CaptureModeSessionTestApi::GetDisclaimerWidget() {
  return session_->disclaimer_.get();
}

views::Widget* CaptureModeSessionTestApi::GetRecordingTypeMenuWidget() {
  return session_->recording_type_menu_widget_.get();
}

views::Widget* CaptureModeSessionTestApi::GetDimensionsLabelWidget() {
  return session_->dimensions_label_widget_.get();
}

UserNudgeController* CaptureModeSessionTestApi::GetUserNudgeController() {
  return session_->user_nudge_controller_.get();
}

MagnifierGlass& CaptureModeSessionTestApi::GetMagnifierGlass() {
  return session_->magnifier_glass_;
}

bool CaptureModeSessionTestApi::IsUsingCustomCursor(CaptureModeType type) {
  return session_->IsUsingCustomCursor(type);
}

CaptureModeSessionFocusCycler::FocusGroup
CaptureModeSessionTestApi::GetCurrentFocusGroup() {
  return session_->focus_cycler_->current_focus_group_;
}

size_t CaptureModeSessionTestApi::GetCurrentFocusIndex() {
  return session_->focus_cycler_->focus_index_;
}

CaptureModeSessionFocusCycler::HighlightableWindow*
CaptureModeSessionTestApi::GetHighlightableWindow(aura::Window* window) {
  return session_->focus_cycler_->highlightable_windows_[window].get();
}

CaptureModeSessionFocusCycler::HighlightableView*
CaptureModeSessionTestApi::GetCurrentFocusedView() {
  const auto items =
      session_->focus_cycler_->GetGroupItems(GetCurrentFocusGroup());
  CHECK(!items.empty());
  return items[GetCurrentFocusIndex()];
}

bool CaptureModeSessionTestApi::HasAxVirtualWidget() const {
  return !!session_->focus_cycler_->ax_widget_;
}

size_t CaptureModeSessionTestApi::GetAxVirtualViewsCount() const {
  return session_->focus_cycler_->ax_virtual_views_.size();
}

bool CaptureModeSessionTestApi::HasFocus() const {
  return session_->focus_cycler_->HasFocus();
}

bool CaptureModeSessionTestApi::IsFolderSelectionDialogShown() {
  return session_->folder_selection_dialog_controller_ &&
         session_->folder_selection_dialog_controller_->dialog_window();
}

bool CaptureModeSessionTestApi::AreAllUisVisible() {
  return session_->is_all_uis_visible_;
}

gfx::Rect CaptureModeSessionTestApi::GetSelectedWindowTargetBounds() {
  return session_->GetSelectedWindowTargetBounds();
}

std::vector<ActionButtonView*> CaptureModeSessionTestApi::GetActionButtons()
    const {
  std::vector<ActionButtonView*> action_buttons;

  // The action container widget, and thus the container view, may not have been
  // created yet when this function is called. In this case, return an empty
  // vector.
  if (session_->action_container_widget_) {
    CHECK(session_->action_container_view_);
    for (views::View* button :
         session_->action_container_view_->GetActionButtons()) {
      action_buttons.emplace_back(views::AsViewClass<ActionButtonView>(button));
    }
  }

  return action_buttons;
}

ActionButtonView* CaptureModeSessionTestApi::GetActionButtonByViewId(
    ActionButtonViewID id) const {
  raw_ptr<ActionButtonContainerView> container =
      session_->action_container_view_;
  return container
             ? views::AsViewClass<ActionButtonView>(container->GetViewByID(id))
             : nullptr;
}

ActionButtonContainerView::ErrorView*
CaptureModeSessionTestApi::GetActionContainerErrorView() const {
  raw_ptr<ActionButtonContainerView> container =
      session_->action_container_view_;
  return container ? views::AsViewClass<ActionButtonContainerView::ErrorView>(
                         container->error_view_for_testing())
                   : nullptr;
}

CaptureRegionOverlayController*
CaptureModeSessionTestApi::GetCaptureRegionOverlayController() const {
  return session_->capture_region_overlay_controller_.get();
}

}  // namespace ash
