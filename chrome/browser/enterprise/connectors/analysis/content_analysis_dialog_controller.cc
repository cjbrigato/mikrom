// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/enterprise/connectors/analysis/content_analysis_dialog_controller.h"

#include <cstddef>
#include <memory>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/enterprise/connectors/analysis/content_analysis_delegate.h"
#include "chrome/browser/enterprise/connectors/analysis/content_analysis_features.h"
#include "chrome/browser/enterprise/connectors/analysis/content_analysis_views.h"
#include "chrome/browser/safe_browsing/cloud_content_scanning/deep_scanning_utils.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/grit/theme_resources.h"
#include "components/constrained_window/constrained_window_views.h"
#include "components/strings/grit/components_strings.h"
#include "components/web_modal/web_contents_modal_dialog_manager.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_types.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_frame_view.h"
#include "ui/views/controls/link.h"
#include "ui/views/controls/textarea/textarea.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/layout/table_layout_view.h"

#include "base/win/windows_h_disallowed.h"

namespace enterprise_connectors {

namespace {

constexpr base::TimeDelta kResizeAnimationDuration = base::Milliseconds(100);

constexpr int kSideImageSize = 24;
constexpr int kLineHeight = 20;

constexpr gfx::Insets kSideImageInsets(8);
constexpr int kMessageAndIconRowLeadingPadding = 32;
constexpr int kMessageAndIconRowTrailingPadding = 48;
constexpr int kSideIconBetweenChildSpacing = 16;
constexpr int kPaddingBeforeBypassJustification = 16;

constexpr size_t kMaxBypassJustificationLength = 280;

// These time values are non-const in order to be overridden in test so they
// complete faster.
base::TimeDelta minimum_pending_dialog_time_ = base::Seconds(2);
base::TimeDelta success_dialog_timeout_ = base::Seconds(1);
base::TimeDelta show_dialog_delay_ = base::Seconds(1);

ContentAnalysisDialogController::TestObserver* observer_for_testing = nullptr;

}  // namespace

// static
base::TimeDelta ContentAnalysisDialogController::GetMinimumPendingDialogTime() {
  return minimum_pending_dialog_time_;
}

// static
base::TimeDelta ContentAnalysisDialogController::GetSuccessDialogTimeout() {
  return success_dialog_timeout_;
}

// static
base::TimeDelta ContentAnalysisDialogController::ShowDialogDelay() {
  return show_dialog_delay_;
}

ContentAnalysisDialogController::ContentAnalysisDialogController(
    std::unique_ptr<ContentAnalysisDelegateBase> delegate,
    bool is_cloud,
    content::WebContents* contents,
    safe_browsing::DeepScanAccessPoint access_point,
    int files_count,
    FinalContentAnalysisResult final_result,
    download::DownloadItem* download_item)
    : content::WebContentsObserver(contents),
      delegate_base_(std::move(delegate)),
      final_result_(final_result),
      access_point_(std::move(access_point)),
      files_count_(files_count),
      download_item_(download_item),
      is_cloud_(is_cloud) {
  DVLOG(1) << __func__;
  DCHECK(delegate_base_);
  SetOwnedByWidget(OwnedByWidgetPassKey());
  set_fixed_width(views::LayoutProvider::Get()->GetDistanceMetric(
      views::DISTANCE_MODAL_DIALOG_PREFERRED_WIDTH));

  if (observer_for_testing) {
    observer_for_testing->ConstructorCalled(this, base::TimeTicks::Now());
  }

  if (final_result_ != FinalContentAnalysisResult::SUCCESS) {
    UpdateStateFromFinalResult(final_result_);
  }

  SetupButtons();

  if (download_item_) {
    download_item_->AddObserver(this);
  }

  // Because the display of the dialog is delayed, it won't block UI
  // interaction with the top level web contents until it is visible.  To block
  // interaction as of now, ignore input events manually.
  top_level_contents_ =
      constrained_window::GetTopLevelWebContents(web_contents())->GetWeakPtr();

  top_level_contents_->StoreFocus();
  scoped_ignore_input_events_ =
      top_level_contents_->IgnoreInputEvents(std::nullopt);

  if (ShowDialogDelay().is_zero() || !is_pending()) {
    DVLOG(1) << __func__ << ": Showing in ctor";
    ShowDialogNow();
  } else {
    content::GetUIThreadTaskRunner({})->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&ContentAnalysisDialogController::ShowDialogNow,
                       weak_ptr_factory_.GetWeakPtr()),
        ShowDialogDelay());
  }

  if (is_warning() && bypass_requires_justification()) {
    bypass_justification_text_length_->SetEnabledColor(
        bypass_justification_text_length_->GetColorProvider()->GetColor(
            ui::kColorAlertHighSeverity));
  }
}

void ContentAnalysisDialogController::ShowDialogNow() {
  if (will_be_deleted_soon_) {
    DVLOG(1) << __func__ << ": aborting since dialog will be deleted soon";
    return;
  }

  auto* manager =
      web_modal::WebContentsModalDialogManager::FromWebContents(web_contents());
  if (!manager) {
    // `manager` being null indicates that `web_contents()` doesn't correspond
    // to a browser tab (ex: an extension background page reading the
    // clipboard). In such a case, we don't show a dialog and instead simply
    // accept/cancel the result immediately. See crbug.com/374120523 and
    // crbug.com/388049470 for more context.
    if (!is_pending()) {
      CancelButtonCallback();
    }
    return;
  }

  // If the web contents is still valid when the delay timer goes off and the
  // dialog has not yet been shown, show it now.
  if (web_contents() && !contents_view_) {
    DVLOG(1) << __func__ << ": first time";
    first_shown_timestamp_ = base::TimeTicks::Now();
    constrained_window::ShowWebModalDialogViews(this, web_contents());
    if (observer_for_testing) {
      observer_for_testing->ViewsFirstShown(this, first_shown_timestamp_);
    }
  }
}

std::u16string ContentAnalysisDialogController::GetWindowTitle() const {
  return std::u16string();
}

void ContentAnalysisDialogController::AcceptButtonCallback() {
  DCHECK(delegate_base_);
  DCHECK(is_warning());
  accepted_or_cancelled_ = true;
  std::optional<std::u16string> justification = std::nullopt;
  if (delegate_base_->BypassRequiresJustification() && bypass_justification_) {
    justification = bypass_justification_->GetText();
  }
  delegate_base_->BypassWarnings(justification);
}

void ContentAnalysisDialogController::CancelButtonCallback() {
  accepted_or_cancelled_ = true;
  if (delegate_base_) {
    delegate_base_->Cancel(is_warning());
  }
}

void ContentAnalysisDialogController::LearnMoreLinkClickedCallback(
    const ui::Event& event) {
  DCHECK(has_learn_more_url());
  web_contents()->OpenURL(
      content::OpenURLParams((*delegate_base_->GetCustomLearnMoreUrl()),
                             content::Referrer(),
                             WindowOpenDisposition::NEW_FOREGROUND_TAB,
                             ui::PAGE_TRANSITION_LINK, false),
      /*navigation_handle_callback=*/{});
}

void ContentAnalysisDialogController::SuccessCallback() {
#if defined(USE_AURA)
  if (web_contents()) {
    // It's possible focus has been lost and gained back incorrectly if the user
    // clicked on the page between the time the scan started and the time the
    // dialog closes. This results in the behaviour detailed in
    // crbug.com/1139050. The fix is to preemptively take back focus when this
    // dialog closes on its own.
    scoped_ignore_input_events_.reset();
    web_contents()->Focus();
  }
#endif
}

void ContentAnalysisDialogController::ContentsChanged(
    views::Textfield* sender,
    const std::u16string& new_contents) {
  if (bypass_justification_text_length_) {
    bypass_justification_text_length_->SetText(l10n_util::GetStringFUTF16(
        IDS_DEEP_SCANNING_DIALOG_BYPASS_JUSTIFICATION_TEXT_LIMIT_LABEL,
        base::NumberToString16(new_contents.size()),
        base::NumberToString16(kMaxBypassJustificationLength)));
  }

  if (new_contents.size() == 0 ||
      new_contents.size() > kMaxBypassJustificationLength) {
    DialogDelegate::SetButtonEnabled(ui::mojom::DialogButton::kOk, false);
    if (bypass_justification_text_length_) {
      bypass_justification_text_length_->SetEnabledColor(
          bypass_justification_text_length_->GetColorProvider()->GetColor(
              ui::kColorAlertHighSeverity));
    }
  } else {
    DialogDelegate::SetButtonEnabled(ui::mojom::DialogButton::kOk, true);
    if (bypass_justification_text_length_ && justification_text_label_) {
      bypass_justification_text_length_->SetEnabledColor(
          justification_text_label_->GetEnabledColor());
    }
  }
}

bool ContentAnalysisDialogController::ShouldShowCloseButton() const {
  return false;
}

views::View* ContentAnalysisDialogController::GetContentsView() {
  if (!contents_view_) {
    DVLOG(1) << __func__ << ": first time";
    contents_view_ = new views::BoxLayoutView();  // Owned by caller.
    contents_view_->SetOrientation(views::BoxLayout::Orientation::kVertical);
    // Padding to distance the top image from the icon and message.
    contents_view_->SetBetweenChildSpacing(16);

    // padding to distance the message from the button(s).  When doing a cloud
    // based analysis, a top image is added to the view and the top padding
    // looks fine.  When not doing a cloud-based analysis set the top padding
    // to make things look nice.
    contents_view_->SetInsideBorderInsets(
        gfx::Insets::TLBR(is_cloud_ ? 0 : 24, 0, 10, 0));

    // Add the top image for cloud-based analysis.
    if (is_cloud_) {
      image_ = contents_view_->AddChildView(
          std::make_unique<ContentAnalysisTopImageView>(this));
    }

    // Create message area layout.
    contents_layout_ = contents_view_->AddChildView(
        std::make_unique<views::TableLayoutView>());
    contents_layout_
        ->AddPaddingColumn(views::TableLayout::kFixedSize,
                           kMessageAndIconRowLeadingPadding)
        .AddColumn(views::LayoutAlignment::kStart,
                   views::LayoutAlignment::kStart,
                   views::TableLayout::kFixedSize,
                   views::TableLayout::ColumnSize::kUsePreferred, 0, 0)
        .AddPaddingColumn(views::TableLayout::kFixedSize,
                          kSideIconBetweenChildSpacing)
        .AddColumn(views::LayoutAlignment::kStretch,
                   views::LayoutAlignment::kStretch, 1.0f,
                   views::TableLayout::ColumnSize::kUsePreferred, 0, 0)
        .AddPaddingColumn(views::TableLayout::kFixedSize,
                          kMessageAndIconRowTrailingPadding)
        // There is initially only 1 row in the table for the side icon and
        // message. Rows are added later when other elements are needed.
        .AddRows(1, views::TableLayout::kFixedSize);

    // Add the side icon.
    contents_layout_->AddChildView(CreateSideIcon());

    // Add the message.
    message_ =
        contents_layout_->AddChildView(std::make_unique<views::StyledLabel>());
    message_->SetText(GetDialogMessage());
    message_->SetLineHeight(kLineHeight);

    // Calculate the width of the side icon column with insets and padding.
    int side_icon_column_width = kMessageAndIconRowLeadingPadding +
                                 kSideImageInsets.width() + kSideImageSize +
                                 kSideIconBetweenChildSpacing;
    message_->SizeToFit(fixed_width() - side_icon_column_width -
                        kMessageAndIconRowTrailingPadding);
    message_->SetHorizontalAlignment(gfx::ALIGN_LEFT);

    if (!is_pending()) {
      UpdateDialog();
    }
  }

  return contents_view_;
}

views::Widget* ContentAnalysisDialogController::GetWidget() {
  return contents_view_->GetWidget();
}

const views::Widget* ContentAnalysisDialogController::GetWidget() const {
  return contents_view_->GetWidget();
}

ui::mojom::ModalType ContentAnalysisDialogController::GetModalType() const {
  return ui::mojom::ModalType::kChild;
}

void ContentAnalysisDialogController::WebContentsDestroyed() {
  // If WebContents are destroyed, then the scan results don't matter so the
  // delegate can be destroyed as well.
  CancelDialogWithoutCallback();
}

void ContentAnalysisDialogController::PrimaryPageChanged(content::Page& page) {
  // If the primary page is changed, the scan results would be stale. So the
  // delegate should be reset and dialog should be cancelled.
  CancelDialogWithoutCallback();
}

void ContentAnalysisDialogController::ShowResult(
    FinalContentAnalysisResult result) {
  DCHECK(is_pending());

  UpdateStateFromFinalResult(result);

  // Update the pending dialog only after it has been shown for a minimum amount
  // of time.
  base::TimeDelta time_shown = base::TimeTicks::Now() - first_shown_timestamp_;
  if (time_shown >= GetMinimumPendingDialogTime()) {
    UpdateDialog();
  } else {
    content::GetUIThreadTaskRunner({})->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&ContentAnalysisDialogController::UpdateDialog,
                       weak_ptr_factory_.GetWeakPtr()),
        GetMinimumPendingDialogTime() - time_shown);
  }
}

ContentAnalysisDialogController::~ContentAnalysisDialogController() {
  DVLOG(1) << __func__;

  if (bypass_justification_) {
    bypass_justification_->SetController(nullptr);
  }

  if (top_level_contents_) {
    scoped_ignore_input_events_.reset();
    top_level_contents_->RestoreFocus();
  }
  if (download_item_) {
    download_item_->RemoveObserver(this);
  }
  if (observer_for_testing) {
    observer_for_testing->DestructorCalled(this);
  }
}

void ContentAnalysisDialogController::UpdateStateFromFinalResult(
    FinalContentAnalysisResult final_result) {
  final_result_ = final_result;
  switch (final_result_) {
    case FinalContentAnalysisResult::ENCRYPTED_FILES:
    case FinalContentAnalysisResult::LARGE_FILES:
    case FinalContentAnalysisResult::FAIL_CLOSED:
    case FinalContentAnalysisResult::FAILURE:
      dialog_state_ = State::FAILURE;
      break;
    case FinalContentAnalysisResult::SUCCESS:
      dialog_state_ = State::SUCCESS;
      break;
    case FinalContentAnalysisResult::WARNING:
      dialog_state_ = State::WARNING;
      break;
  }
}

void ContentAnalysisDialogController::UpdateViews() {
  DCHECK(contents_view_);

  // Update the style of the dialog to reflect the new state.
  if (image_) {
    image_->Update();
  }

  side_icon_image_->Update();

  // There isn't always a spinner, for instance when the dialog is started in a
  // state other than the "pending" state.
  if (side_icon_spinner_) {
    // Calling `Update` leads to the deletion of the spinner.
    side_icon_spinner_.ExtractAsDangling()->Update();
  }

  // Update the buttons.
  SetupButtons();

  // Update the message's text, and send an alert for screen readers since the
  // text changed.
  std::u16string new_message = GetDialogMessage();
  UpdateDialogMessage(std::move(new_message));

  // Add bypass justification views when required on warning verdicts. The order
  // of the helper functions needs to be preserved for them to appear in the
  // correct order.
  if (is_warning() && bypass_requires_justification()) {
    AddJustificationTextLabelToDialog();
    AddJustificationTextAreaToDialog();
    AddJustificationTextLengthToDialog();
  }
}

bool ContentAnalysisDialogController::ShouldShowDialogNow() {
  DCHECK(!is_pending());
  // If the final result is fail closed, display ui regardless of cloud or local
  // analysis.
  if (final_result_ == FinalContentAnalysisResult::FAIL_CLOSED) {
    DVLOG(1) << __func__ << ": show fail-closed ui.";
    return true;
  }
  // Otherwise, show dialog now only if it is cloud analysis and the verdict is
  // not success.
  return is_cloud_ && !is_success();
}

void ContentAnalysisDialogController::UpdateDialog() {
  if (!contents_view_ && !is_pending()) {
    // If the dialog is no longer pending, a final verdict was received before
    // the dialog was displayed.  Show the verdict right away only if
    // ShouldShowDialogNow() returns true.
    ShouldShowDialogNow() ? ShowDialogNow() : CancelDialogAndDelete();
    return;
  }

  DCHECK(is_result());

  int height_before = contents_view_->GetPreferredSize().height();

  UpdateViews();

  // Resize the dialog's height. This is needed since the text might take more
  // lines after changing.
  int height_after = contents_view_->GetHeightForWidth(contents_view_->width());

  int height_to_add = std::max(height_after - height_before, 0);
  if (height_to_add > 0 && GetWidget()) {
    Resize(height_to_add);
  }

  // Update the dialog.
  DialogDelegate::DialogModelChanged();
  contents_view_->InvalidateLayout();

  // Schedule the dialog to close itself in the success case.
  if (is_success()) {
    content::GetUIThreadTaskRunner({})->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&DialogDelegate::CancelDialog,
                       weak_ptr_factory_.GetWeakPtr()),
        GetSuccessDialogTimeout());
  }

  if (observer_for_testing) {
    observer_for_testing->DialogUpdated(this, final_result_);
  }

  // Cancel the dialog as it is updated in tests in the failure dialog case.
  // This is necessary to terminate tests that end when the dialog is closed.
  if (observer_for_testing && is_failure()) {
    CancelDialog();
  }
}

void ContentAnalysisDialogController::Resize(int height_to_add) {
  // Only resize if the dialog is updated to show a result.
  DCHECK(is_result());
  views::Widget* widget = GetWidget();
  DCHECK(widget);

  gfx::Rect dialog_rect = widget->GetContentsView()->GetContentsBounds();
  int new_height = dialog_rect.height();

  // Remove the button row's height if it's removed in the success case.
  if (is_success()) {
    DCHECK(contents_view_->parent());
    DCHECK_EQ(contents_view_->parent()->children().size(), 2ul);
    DCHECK_EQ(contents_view_->parent()->children()[0], contents_view_);

    views::View* button_row_view = contents_view_->parent()->children()[1];
    new_height -= button_row_view->GetContentsBounds().height();
  }

  // Apply the message lines delta.
  new_height += height_to_add;
  dialog_rect.set_height(new_height);

  // Setup the animation.
  bounds_animator_ =
      std::make_unique<views::BoundsAnimator>(widget->GetRootView());
  bounds_animator_->SetAnimationDuration(kResizeAnimationDuration);

  DCHECK(widget->GetRootView());
  views::View* view_to_resize = widget->GetRootView()->children()[0];

  // Start the animation.
  bounds_animator_->AnimateViewTo(view_to_resize, dialog_rect);

  // Change the widget's size.
  gfx::Size new_size = view_to_resize->size();
  new_size.set_height(new_height);
  widget->SetSize(new_size);
}

void ContentAnalysisDialogController::SetupButtons() {
  if (is_warning()) {
    // Include the Ok and Cancel buttons if there is a bypassable warning.
    DialogDelegate::SetButtons(
        static_cast<int>(ui::mojom::DialogButton::kCancel) |
        static_cast<int>(ui::mojom::DialogButton::kOk));
    DialogDelegate::SetDefaultButton(
        static_cast<int>(ui::mojom::DialogButton::kCancel));

    DialogDelegate::SetButtonLabel(ui::mojom::DialogButton::kCancel,
                                   GetCancelButtonText());
    DialogDelegate::SetCancelCallback(
        base::BindOnce(&ContentAnalysisDialogController::CancelButtonCallback,
                       weak_ptr_factory_.GetWeakPtr()));

    DialogDelegate::SetButtonLabel(ui::mojom::DialogButton::kOk,
                                   GetBypassWarningButtonText());
    DialogDelegate::SetAcceptCallback(
        base::BindOnce(&ContentAnalysisDialogController::AcceptButtonCallback,
                       weak_ptr_factory_.GetWeakPtr()));

    if (delegate_base_->BypassRequiresJustification()) {
      DialogDelegate::SetButtonEnabled(ui::mojom::DialogButton::kOk, false);
    }
  } else if (is_failure() || is_pending()) {
    // Include the Cancel button when the scan is pending or failing.
    DialogDelegate::SetButtons(
        static_cast<int>(ui::mojom::DialogButton::kCancel));
    DialogDelegate::SetDefaultButton(
        static_cast<int>(ui::mojom::DialogButton::kNone));

    DialogDelegate::SetButtonLabel(ui::mojom::DialogButton::kCancel,
                                   GetCancelButtonText());
    DialogDelegate::SetCancelCallback(
        base::BindOnce(&ContentAnalysisDialogController::CancelButtonCallback,
                       weak_ptr_factory_.GetWeakPtr()));
  } else {
    // Include no buttons otherwise.
    DialogDelegate::SetButtons(
        static_cast<int>(ui::mojom::DialogButton::kNone));
    DialogDelegate::SetCancelCallback(
        base::BindOnce(&ContentAnalysisDialogController::SuccessCallback,
                       weak_ptr_factory_.GetWeakPtr()));
  }
}

std::u16string ContentAnalysisDialogController::GetDialogMessage() const {
  switch (dialog_state_) {
    case State::PENDING:
      return GetPendingMessage();
    case State::FAILURE:
      return GetFailureMessage();
    case State::SUCCESS:
      return GetSuccessMessage();
    case State::WARNING:
      return GetWarningMessage();
  }
}

std::u16string ContentAnalysisDialogController::GetCancelButtonText() const {
  int text_id;
  auto overriden_text = delegate_base_->OverrideCancelButtonText();
  if (overriden_text) {
    return overriden_text.value();
  }

  switch (dialog_state_) {
    case State::SUCCESS:
      NOTREACHED();
    case State::PENDING:
      text_id = IDS_DEEP_SCANNING_DIALOG_CANCEL_UPLOAD_BUTTON;
      break;
    case State::FAILURE:
      text_id = IDS_CLOSE;
      break;
    case State::WARNING:
      text_id = IDS_DEEP_SCANNING_DIALOG_CANCEL_WARNING_BUTTON;
      break;
  }
  return l10n_util::GetStringUTF16(text_id);
}

std::u16string ContentAnalysisDialogController::GetBypassWarningButtonText()
    const {
  DCHECK(is_warning());
  return l10n_util::GetStringUTF16(IDS_DEEP_SCANNING_DIALOG_PROCEED_BUTTON);
}

std::unique_ptr<views::View> ContentAnalysisDialogController::CreateSideIcon() {
  // The icon left of the text has the appearance of a blue "Enterprise" logo
  // with a spinner when the scan is pending.
  auto icon = std::make_unique<views::View>();
  icon->SetLayoutManager(std::make_unique<views::FillLayout>());
  side_icon_image_ = icon->AddChildView(
      std::make_unique<ContentAnalysisSideIconImageView>(this));

  // Add a spinner if the scan result is pending.
  if (is_pending()) {
    auto spinner = std::make_unique<ContentAnalysisSideIconSpinnerView>(this);
    spinner->Start();
    side_icon_spinner_ = icon->AddChildView(std::move(spinner));
  }

  return icon;
}

ui::ColorId ContentAnalysisDialogController::GetSideImageBackgroundColor()
    const {
  DCHECK(is_result());
  DCHECK(contents_view_);

  switch (dialog_state_) {
    case State::PENDING:
      NOTREACHED();
    case State::SUCCESS:
      return ui::kColorAccent;
    case State::FAILURE:
      return ui::kColorAlertHighSeverity;
    case State::WARNING:
      return ui::kColorAlertMediumSeverityIcon;
  }
}

int ContentAnalysisDialogController::GetTopImageId() const {
  if (ShouldUseDarkTopImage()) {
    switch (dialog_state_) {
      case State::PENDING:
        return IDR_UPLOAD_SCANNING_DARK;
      case State::SUCCESS:
        return IDR_UPLOAD_SUCCESS_DARK;
      case State::FAILURE:
        return IDR_UPLOAD_VIOLATION_DARK;
      case State::WARNING:
        return IDR_UPLOAD_WARNING_DARK;
    }
  } else {
    switch (dialog_state_) {
      case State::PENDING:
        return IDR_UPLOAD_SCANNING;
      case State::SUCCESS:
        return IDR_UPLOAD_SUCCESS;
      case State::FAILURE:
        return IDR_UPLOAD_VIOLATION;
      case State::WARNING:
        return IDR_UPLOAD_WARNING;
    }
  }
}

std::u16string ContentAnalysisDialogController::GetPendingMessage() const {
  DCHECK(is_pending());
  if (is_print_scan()) {
    return l10n_util::GetStringUTF16(
        IDS_DEEP_SCANNING_DIALOG_PRINT_PENDING_MESSAGE);
  }

  return l10n_util::GetPluralStringFUTF16(
      IDS_DEEP_SCANNING_DIALOG_UPLOAD_PENDING_MESSAGE, files_count_);
}

std::u16string ContentAnalysisDialogController::GetFailureMessage() const {
  DCHECK(is_failure());

  // If the admin has specified a custom message for this failure, it takes
  // precedence over the generic ones.
  if (has_custom_message()) {
    return GetCustomMessage();
  }

  if (final_result_ == FinalContentAnalysisResult::FAIL_CLOSED) {
    DVLOG(1) << __func__ << ": display fail-closed message.";
    return l10n_util::GetStringUTF16(
        IDS_DEEP_SCANNING_DIALOG_UPLOAD_FAIL_CLOSED_MESSAGE);
  }

  if (final_result_ == FinalContentAnalysisResult::LARGE_FILES) {
    if (is_print_scan()) {
      return l10n_util::GetStringUTF16(
          IDS_DEEP_SCANNING_DIALOG_LARGE_PRINT_FAILURE_MESSAGE);
    }
    return l10n_util::GetPluralStringFUTF16(
        IDS_DEEP_SCANNING_DIALOG_LARGE_FILE_FAILURE_MESSAGE, files_count_);
  }

  if (final_result_ == FinalContentAnalysisResult::ENCRYPTED_FILES) {
    return l10n_util::GetPluralStringFUTF16(
        IDS_DEEP_SCANNING_DIALOG_ENCRYPTED_FILE_FAILURE_MESSAGE, files_count_);
  }

  if (is_print_scan()) {
    return l10n_util::GetStringUTF16(
        IDS_DEEP_SCANNING_DIALOG_PRINT_WARNING_MESSAGE);
  }

  return l10n_util::GetPluralStringFUTF16(
      IDS_DEEP_SCANNING_DIALOG_UPLOAD_FAILURE_MESSAGE, files_count_);
}

std::u16string ContentAnalysisDialogController::GetWarningMessage() const {
  DCHECK(is_warning());

  // If the admin has specified a custom message for this warning, it takes
  // precedence over the generic one.
  if (has_custom_message()) {
    return GetCustomMessage();
  }

  if (is_print_scan()) {
    return l10n_util::GetStringUTF16(
        IDS_DEEP_SCANNING_DIALOG_PRINT_WARNING_MESSAGE);
  }

  return l10n_util::GetPluralStringFUTF16(
      IDS_DEEP_SCANNING_DIALOG_UPLOAD_WARNING_MESSAGE, files_count_);
}

std::u16string ContentAnalysisDialogController::GetSuccessMessage() const {
  DCHECK(is_success());
  if (is_print_scan()) {
    return l10n_util::GetStringUTF16(
        IDS_DEEP_SCANNING_DIALOG_PRINT_SUCCESS_MESSAGE);
  }
  return l10n_util::GetPluralStringFUTF16(
      IDS_DEEP_SCANNING_DIALOG_SUCCESS_MESSAGE, files_count_);
}

std::u16string ContentAnalysisDialogController::GetCustomMessage() const {
  DCHECK(is_warning() || is_failure());
  DCHECK(has_custom_message());
  return *(delegate_base_->GetCustomMessage());
}

void ContentAnalysisDialogController::AddLearnMoreLinkToDialog() {
  DCHECK(contents_view_);
  DCHECK(contents_layout_);
  DCHECK(is_warning() || is_failure());

  // There is only ever up to one link in the dialog, so return early instead of
  // adding another one.
  if (learn_more_link_) {
    return;
  }

  // Add a row for the new element, and add an empty view to skip the first
  // column.
  contents_layout_->AddRows(1, views::TableLayout::kFixedSize);
  contents_layout_->AddChildView(std::make_unique<views::View>());

  // Since `learn_more_link_` is not as wide as the column it's a part of,
  // instead of being added directly to it, it has a parent with a BoxLayout so
  // that its width corresponds to its own text size instead of the full column
  // width.
  views::View* learn_more_column =
      contents_layout_->AddChildView(std::make_unique<views::View>());
  learn_more_column->SetLayoutManager(std::make_unique<views::BoxLayout>());

  learn_more_link_ = learn_more_column->AddChildView(
      std::make_unique<views::Link>(l10n_util::GetStringUTF16(
          IDS_DEEP_SCANNING_DIALOG_CUSTOM_MESSAGE_LEARN_MORE_LINK)));
  learn_more_link_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  learn_more_link_->SetCallback(base::BindRepeating(
      &ContentAnalysisDialogController::LearnMoreLinkClickedCallback,
      base::Unretained(this)));
}

void ContentAnalysisDialogController::AddJustificationTextLabelToDialog() {
  DCHECK(contents_view_);
  DCHECK(contents_layout_);
  DCHECK(is_warning());

  // There is only ever up to one justification section in the dialog, so return
  // early instead of adding another one.
  if (justification_text_label_) {
    return;
  }

  // Add a row for the new element, and add an empty view to skip the first
  // column.
  contents_layout_->AddRows(1, views::TableLayout::kFixedSize);
  contents_layout_->AddChildView(std::make_unique<views::View>());

  justification_text_label_ =
      contents_layout_->AddChildView(std::make_unique<views::Label>());
  justification_text_label_->SetText(
      delegate_base_->GetBypassJustificationLabel());
  justification_text_label_->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(kPaddingBeforeBypassJustification, 0, 0, 0)));
  justification_text_label_->SetLineHeight(kLineHeight);
  justification_text_label_->SetMultiLine(true);
  justification_text_label_->SetVerticalAlignment(gfx::ALIGN_MIDDLE);
  justification_text_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
}

void ContentAnalysisDialogController::AddJustificationTextAreaToDialog() {
  DCHECK(contents_view_);
  DCHECK(contents_layout_);
  DCHECK(justification_text_label_);
  DCHECK(is_warning());

  // There is only ever up to one justification text box in the dialog, so
  // return early instead of adding another one.
  if (bypass_justification_) {
    return;
  }

  // Add a row for the new element, and add an empty view to skip the first
  // column.
  contents_layout_->AddRows(1, views::TableLayout::kFixedSize);
  contents_layout_->AddChildView(std::make_unique<views::View>());

  bypass_justification_ =
      contents_layout_->AddChildView(std::make_unique<views::Textarea>());
  bypass_justification_->GetViewAccessibility().SetName(
      *justification_text_label_);
  bypass_justification_->SetController(this);
}

void ContentAnalysisDialogController::AddJustificationTextLengthToDialog() {
  DCHECK(contents_view_);
  DCHECK(contents_layout_);
  DCHECK(is_warning());

  // There is only ever up to one justification text length indicator in the
  // dialog, so return early instead of adding another one.
  if (bypass_justification_text_length_) {
    return;
  }

  // Add a row for the new element, and add an empty view to skip the first
  // column.
  contents_layout_->AddRows(1, views::TableLayout::kFixedSize);
  contents_layout_->AddChildView(std::make_unique<views::View>());

  bypass_justification_text_length_ =
      contents_layout_->AddChildView(std::make_unique<views::Label>());
  bypass_justification_text_length_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  bypass_justification_text_length_->SetText(l10n_util::GetStringFUTF16(
      IDS_DEEP_SCANNING_DIALOG_BYPASS_JUSTIFICATION_TEXT_LIMIT_LABEL,
      base::NumberToString16(0),
      base::NumberToString16(kMaxBypassJustificationLength)));

  // Set the color to red initially because a 0 length message is invalid. Skip
  // this if the color provider is unavailable.
  if (bypass_justification_text_length_->GetColorProvider()) {
    bypass_justification_text_length_->SetEnabledColor(
        bypass_justification_text_length_->GetColorProvider()->GetColor(
            ui::kColorAlertHighSeverity));
  }
}

void ContentAnalysisDialogController::AddLinksToDialogMessage() {
  if (!has_custom_message_ranges()) {
    return;
  }

  std::vector<std::pair<gfx::Range, GURL>> ranges =
      *(delegate_base_->GetCustomRuleMessageRanges());
  for (const auto& range : ranges) {
    if (!range.second.is_valid()) {
      continue;
    }
    message_->AddStyleRange(
        gfx::Range(range.first.start(), range.first.end()),
        views::StyledLabel::RangeStyleInfo::CreateForLink(base::BindRepeating(
            [](base::WeakPtr<content::WebContents> web_contents, GURL url,
               const ui::Event& event) {
              if (!web_contents) {
                return;
              }
              web_contents->OpenURL(
                  content::OpenURLParams(
                      url, content::Referrer(),
                      WindowOpenDisposition::NEW_FOREGROUND_TAB,
                      ui::PAGE_TRANSITION_LINK,
                      /*is_renderer_initiated=*/false),
                  /*navigation_handle_callback=*/{});
            },
            web_contents()->GetWeakPtr(), range.second)));
  }
}

void ContentAnalysisDialogController::UpdateDialogMessage(
    std::u16string new_message) {
  if ((is_failure() || is_warning()) && has_custom_message()) {
    message_->SetText(new_message);
    AddLinksToDialogMessage();
    message_->GetViewAccessibility().AnnounceText(std::move(new_message));
    if (has_learn_more_url()) {
      AddLearnMoreLinkToDialog();
    }
  } else {
    message_->SetText(new_message);
    message_->GetViewAccessibility().AnnounceText(std::move(new_message));

    // Add a "Learn More" link for warnings/failures when one is provided.
    if ((is_failure() || is_warning()) && has_learn_more_url()) {
      AddLearnMoreLinkToDialog();
    }
  }
}

bool ContentAnalysisDialogController::ShouldUseDarkTopImage() const {
  return color_utils::IsDark(
      contents_view_->GetColorProvider()->GetColor(ui::kColorDialogBackground));
}

bool ContentAnalysisDialogController::is_print_scan() const {
  return access_point_ == safe_browsing::DeepScanAccessPoint::PRINT;
}

void ContentAnalysisDialogController::CancelDialogAndDelete() {
  if (observer_for_testing) {
    observer_for_testing->CancelDialogAndDeleteCalled(this, final_result_);
  }

  if (contents_view_) {
    DVLOG(1) << __func__ << ": dialog will be canceled";
    CancelDialog();
  } else {
    DVLOG(1) << __func__ << ": dialog will be deleted soon";
    will_be_deleted_soon_ = true;
    content::GetUIThreadTaskRunner({})->DeleteSoon(FROM_HERE, this);
  }
}

ui::ColorId ContentAnalysisDialogController::GetSideImageLogoColor() const {
  DCHECK(contents_view_);

  switch (dialog_state_) {
    case State::PENDING:
      // In the dialog's pending state, the side image is just an enterprise
      // logo surrounded by a throbber, so we use the throbber color for it.
      return ui::kColorThrobber;
    case State::SUCCESS:
    case State::FAILURE:
    case State::WARNING:
      // In a result state, the side image is a circle colored with the result's
      // color and an enterprise logo in front of it, so the logo should have
      // the same color as the dialog's overall background.
      return ui::kColorDialogBackground;
  }
}

// static
void ContentAnalysisDialogController::SetMinimumPendingDialogTimeForTesting(
    base::TimeDelta delta) {
  minimum_pending_dialog_time_ = delta;
}

// static
void ContentAnalysisDialogController::SetSuccessDialogTimeoutForTesting(
    base::TimeDelta delta) {
  success_dialog_timeout_ = delta;
}

// static
void ContentAnalysisDialogController::SetShowDialogDelayForTesting(
    base::TimeDelta delta) {
  show_dialog_delay_ = delta;
}

// static
void ContentAnalysisDialogController::SetObserverForTesting(
    TestObserver* observer) {
  observer_for_testing = observer;
}

views::ImageView* ContentAnalysisDialogController::GetTopImageForTesting()
    const {
  return image_;
}

views::Throbber* ContentAnalysisDialogController::GetSideIconSpinnerForTesting()
    const {
  return side_icon_spinner_;
}

views::StyledLabel* ContentAnalysisDialogController::GetMessageForTesting()
    const {
  return message_;
}

views::Link* ContentAnalysisDialogController::GetLearnMoreLinkForTesting()
    const {
  return learn_more_link_;
}

views::Label*
ContentAnalysisDialogController::GetBypassJustificationLabelForTesting() const {
  return justification_text_label_;
}

views::Textarea*
ContentAnalysisDialogController::GetBypassJustificationTextareaForTesting()
    const {
  return bypass_justification_;
}

views::Label*
ContentAnalysisDialogController::GetJustificationTextLengthForTesting() const {
  return bypass_justification_text_length_;
}

void ContentAnalysisDialogController::OnDownloadUpdated(
    download::DownloadItem* download) {
  if (download->GetDangerType() ==
          download::DOWNLOAD_DANGER_TYPE_USER_VALIDATED &&
      !accepted_or_cancelled_) {
    // The user validated the verdict in another instance of
    // `ContentAnalysisDialogController`, so this one is now pointless and can
    // go away.
    CancelDialogWithoutCallback();
  }
}

void ContentAnalysisDialogController::OnDownloadOpened(
    download::DownloadItem* download) {
  if (!accepted_or_cancelled_) {
    CancelDialogWithoutCallback();
  }
}

void ContentAnalysisDialogController::OnDownloadDestroyed(
    download::DownloadItem* download) {
  if (!accepted_or_cancelled_) {
    CancelDialogWithoutCallback();
  }
  download_item_ = nullptr;
}

void ContentAnalysisDialogController::CancelDialogWithoutCallback() {
  // Reset `delegate` so no logic runs when the dialog is cancelled.
  delegate_base_.reset(nullptr);

  // view may be null if the dialog was delayed and never shown before the
  // verdict is known.
  if (contents_view_) {
    CancelDialog();
  }
}

bool ContentAnalysisDialogController::is_result() const {
  return !is_pending();
}

}  // namespace enterprise_connectors
