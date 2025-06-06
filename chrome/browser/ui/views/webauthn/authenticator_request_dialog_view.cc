// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/webauthn/authenticator_request_dialog_view.h"

#include <memory>
#include <string>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/memory/scoped_refptr.h"
#include "base/notreached.h"
#include "chrome/browser/ui/views/webauthn/authenticator_gpm_account_info_view.h"
#include "chrome/browser/ui/views/webauthn/authenticator_request_sheet_view.h"
#include "chrome/browser/ui/views/webauthn/pin_options_button.h"
#include "chrome/browser/ui/webauthn/authenticator_request_sheet_model.h"
#include "chrome/browser/ui/webauthn/sheet_models.h"
#include "chrome/browser/webauthn/authenticator_request_dialog_model.h"
#include "chrome/grit/generated_resources.h"
#include "components/constrained_window/constrained_window_views.h"
#include "components/vector_icons/vector_icons.h"
#include "components/web_modal/web_contents_modal_dialog_manager.h"
#include "components/web_modal/web_contents_modal_dialog_manager_delegate.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/visibility.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/base/ui_base_types.h"
#include "ui/color/color_id.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_frame_view.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/throbber.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/layout/layout_manager.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/view.h"
#include "ui/views/window/dialog_delegate.h"

namespace {
// View ID used to find the spinner container added to the accept button.
constexpr int kAcceptButtonSpinnerContainerId = 1327;
}  // namespace

using Step = AuthenticatorRequestDialogModel::Step;

AuthenticatorRequestDialogView::AuthenticatorRequestDialogView(
    content::WebContents* web_contents,
    AuthenticatorRequestDialogModel* model)
    : content::WebContentsObserver(web_contents),
      model_(model),
      web_contents_hidden_(web_contents->GetVisibility() ==
                           content::Visibility::HIDDEN) {
  // TODO(crbug.com/338254375): Remove the following line once this is the
  // default state for widgets.
  SetOwnershipOfNewWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);

  SetShowTitle(false);

  SetModalType(ui::mojom::ModalType::kChild);
  SetShowCloseButton(false);
  set_fixed_width(views::LayoutProvider::Get()->GetDistanceMetric(
      views::DISTANCE_MODAL_DIALOG_PREFERRED_WIDTH));

  // Currently, all sheets have a label on top and controls at the bottom.
  // Consider moving this to AuthenticatorRequestSheetView if this changes.
  SetLayoutManager(std::make_unique<views::FillLayout>());
}

AuthenticatorRequestDialogView::~AuthenticatorRequestDialogView() = default;

void AuthenticatorRequestDialogView::Show() {
  if (web_contents_hidden_) {
    // Calling Widget::Show() while the tab is not in foreground shows the
    // dialog on the foreground tab (https://crbug.com/969153). Instead, wait
    // for OnVisibilityChanged() to signal the tab going into foreground again,
    // and then show the widget.
    return;
  }

  GetWidget()->Show();
}

void AuthenticatorRequestDialogView::ReplaceCurrentSheetWith(
    std::unique_ptr<AuthenticatorRequestSheetView> new_sheet) {
  DCHECK(new_sheet);

  if (sheet_) {
    RemoveChildViewT(sheet_);
  }
  CHECK(children().empty());

  sheet_ = new_sheet.get();
  AddChildView(std::move(new_sheet));

  UpdateUIForCurrentSheet();
}

void AuthenticatorRequestDialogView::UpdateUIForCurrentSheet() {
  DCHECK(sheet_);

  sheet_->ReInitChildViews();

  const AuthenticatorRequestSheetModel::AcceptButtonState accept_state =
      sheet_->model()->GetAcceptButtonState();
  const bool accept_button_visible =
      accept_state !=
      AuthenticatorRequestSheetModel::AcceptButtonState::kNotVisible;

  int buttons = static_cast<int>(ui::mojom::DialogButton::kNone);
  if (accept_button_visible) {
    buttons |= static_cast<int>(ui::mojom::DialogButton::kOk);
  }
  if (sheet_->model()->IsCancelButtonVisible()) {
    buttons |= static_cast<int>(ui::mojom::DialogButton::kCancel);
  }
  SetButtons(buttons);
  SetDefaultButton(buttons & static_cast<int>(ui::mojom::DialogButton::kOk)
                       ? static_cast<int>(ui::mojom::DialogButton::kOk)
                       : static_cast<int>(ui::mojom::DialogButton::kNone));
  SetButtonLabel(ui::mojom::DialogButton::kOk,
                 sheet_->model()->GetAcceptButtonLabel());
  SetButtonLabel(ui::mojom::DialogButton::kCancel,
                 sheet_->model()->GetCancelButtonLabel());
  if (model_->step() == Step::kTrustThisComputerAssertion ||
      model_->step() == Step::kTrustThisComputerCreation ||
      model_->step() == Step::kGPMCreatePasskey ||
      model_->step() == Step::kGPMEnterPin ||
      model_->step() == Step::kGPMEnterArbitraryPin ||
      model_->step() == Step::kGPMCreatePin ||
      model_->step() == Step::kGPMCreateArbitraryPin ||
      model_->step() == Step::kGPMChangePin ||
      model_->step() == Step::kGPMChangeArbitraryPin) {
    SetButtonStyle(ui::mojom::DialogButton::kCancel, ui::ButtonStyle::kTonal);
  }

  if (ShouldOtherMechanismsButtonBeVisible()) {
    auto* other_mechanisms = SetExtraView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(
            &AuthenticatorRequestDialogView::OtherMechanismsButtonPressed,
            base::Unretained(this)),
        sheet_->model()->GetOtherMechanismButtonLabel()));
    other_mechanisms->SetEnabled(!model_->ui_disabled_);
  } else if (sheet_->model()->IsManageDevicesButtonVisible()) {
    auto* manage_devices = SetExtraView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(
            &AuthenticatorRequestDialogView::ManageDevicesButtonPressed,
            base::Unretained(this)),
        l10n_util::GetStringUTF16(IDS_WEBAUTHN_MANAGE_DEVICES)));
    manage_devices->SetEnabled(!model_->ui_disabled_);
  } else if (sheet_->model()->IsForgotGPMPinButtonVisible()) {
    auto forgot_pin_button = std::make_unique<views::MdTextButton>(
        base::BindRepeating(
            &AuthenticatorRequestDialogView::ForgotGPMPinPressed,
            base::Unretained(this)),
        l10n_util::GetStringUTF16(IDS_WEBAUTHN_FORGOT_GPM_PIN_BUTTON));
    forgot_pin_button->SetEnabled(!model_->ui_disabled_);
    SetExtraView(std::move(forgot_pin_button));
  } else if (sheet_->model()->IsGPMPinOptionsButtonVisible()) {
    PinOptionsButton::CommandId checked_command_id =
        (model_->step() == Step::kGPMCreateArbitraryPin ||
         model_->step() == Step::kGPMChangeArbitraryPin)
            ? PinOptionsButton::CommandId::CHOOSE_ARBITRARY_PIN
            : PinOptionsButton::CommandId::CHOOSE_SIX_DIGIT_PIN;
    auto pin_options_button = std::make_unique<PinOptionsButton>(
        l10n_util::GetStringUTF16(IDS_WEBAUTHN_GPM_PIN_OPTIONS_BUTTON),
        checked_command_id,
        base::BindRepeating(&AuthenticatorRequestDialogView::GPMPinOptionChosen,
                            base::Unretained(this)));
    pin_options_button->SetEnabled(!model_->ui_disabled_);
    SetExtraView(std::move(pin_options_button));
  } else {
    SetExtraView(std::make_unique<views::View>());
  }

  DialogModelChanged();

  // If the widget is not yet shown or already being torn down, we are done. In
  // the former case, sizing/layout will happen once the dialog is visible.
  if (!GetWidget()) {
    return;
  }

  views::MdTextButton* ok_button = GetOkButton();
  if (ok_button) {
    const bool show_spinner =
        accept_state ==
        AuthenticatorRequestSheetModel::AcceptButtonState::kDisabledWithSpinner;

    views::View* existing_container =
        ok_button->GetViewByID(kAcceptButtonSpinnerContainerId);

    if (show_spinner && !existing_container) {
      constexpr int kDialogButtonSpinnerSize = 16;
      auto spinner = std::make_unique<views::Throbber>();
      spinner->SetPreferredSize(
          gfx::Size(kDialogButtonSpinnerSize, kDialogButtonSpinnerSize));
      spinner->SetColorId(ui::kColorButtonForegroundProminent);
      spinner->Start();

      auto spinner_container = std::make_unique<views::BoxLayoutView>();
      spinner_container->SetOrientation(
          views::BoxLayout::Orientation::kHorizontal);
      spinner_container->SetMainAxisAlignment(views::LayoutAlignment::kCenter);
      spinner_container->SetCrossAxisAlignment(views::LayoutAlignment::kCenter);
      spinner_container->AddChildView(std::move(spinner));
      spinner_container->SetVisible(false);  // Initially hidden
      spinner_container->SetID(kAcceptButtonSpinnerContainerId);

      ok_button->SetUseDefaultFillLayout(true);
      existing_container = ok_button->AddChildView(std::move(spinner_container));
    }

    if (show_spinner) {
      // Show the spinner and hide the button text.
      existing_container->SetVisible(true);
      ok_button->SetBgColorIdOverride(ui::kColorButtonBackgroundProminent);
      ok_button->SetTextColor(views::Button::ButtonState::STATE_DISABLED,
                              ui::kColorButtonBackgroundProminent);
    } else {
      if (existing_container) {
        existing_container->SetVisible(false);
      }
      ok_button->SetBgColorIdOverride(std::nullopt);
    }
  }

  auto* frame_view = GetBubbleFrameView();
  if (model_->step() == Step::kGPMCreatePin ||
      model_->step() == Step::kGPMCreateArbitraryPin ||
      model_->step() == Step::kGPMChangePin ||
      model_->step() == Step::kGPMChangeArbitraryPin ||
      model_->step() == Step::kGPMEnterPin ||
      model_->step() == Step::kGPMEnterArbitraryPin) {
    frame_view->SetFootnoteView(
        std::make_unique<AuthenticatorGpmAccountInfoView>(
            static_cast<AuthenticatorGpmPinSheetModelBase*>(sheet_->model())));
  } else {
    frame_view->SetFootnoteView(nullptr);
  }

  // Force re-layout of the entire dialog client view, which includes the sheet
  // content as well as the button row on the bottom.
  // TODO(ellyjones): Why is this necessary?
  GetWidget()->GetRootView()->DeprecatedLayoutImmediately();

  // The accessibility title is also sourced from the |sheet_|'s step title.
  GetWidget()->UpdateWindowTitle();

  // TODO(crbug.com/41392632): Investigate how a web-modal dialog's
  // lifetime compares to that of the parent WebContents. Take a conservative
  // approach for now.
  if (!web_contents()) {
    return;
  }

  // The |dialog_manager| might temporarily be unavailable while the tab is
  // being dragged from one browser window to the other.
  auto* dialog_manager =
      web_modal::WebContentsModalDialogManager::FromWebContents(
          constrained_window::GetTopLevelWebContents(web_contents()));
  if (!dialog_manager) {
    return;
  }

  // Update the dialog size and position, as the preferred size of the sheet
  // might have changed.
  constrained_window::UpdateWebContentsModalDialogPosition(
      GetWidget(), dialog_manager->delegate()->GetWebContentsModalDialogHost());

  // Reset focus to the highest priority control on the new/updated sheet.
  if (GetInitiallyFocusedView()) {
    GetInitiallyFocusedView()->RequestFocus();
  }
  if (model_->ui_disabled_ && sheet_->model()->IsActivityIndicatorVisible()) {
    // Announce the loading state after request focus; otherwise the view that
    // has the focus will suppress the loading announcement.
    GetViewAccessibility().AnnounceText(
        l10n_util::GetStringUTF16(IDS_WEBAUTHN_LOADING));
  }
}

bool AuthenticatorRequestDialogView::ShouldOtherMechanismsButtonBeVisible()
    const {
  return sheet_->model()->IsOtherMechanismButtonVisible();
}

bool AuthenticatorRequestDialogView::Accept() {
  sheet_->model()->OnAccept();
  return false;
}

bool AuthenticatorRequestDialogView::Cancel() {
  sheet_->model()->OnCancel();
  return false;
}

bool AuthenticatorRequestDialogView::IsDialogButtonEnabled(
    ui::mojom::DialogButton button) const {
  switch (button) {
    case ui::mojom::DialogButton::kNone:
      break;
    case ui::mojom::DialogButton::kOk:
      return sheet_ &&
             sheet_->model()->GetAcceptButtonState() ==
                 AuthenticatorRequestSheetModel::AcceptButtonState::kEnabled;
    case ui::mojom::DialogButton::kCancel:
      return true;  // Cancel is always enabled if visible.
  }
  NOTREACHED();
}

views::View* AuthenticatorRequestDialogView::GetInitiallyFocusedView() {
  // Need to provide a custom implementation, as most dialog sheets will not
  // have a default button which gets initial focus. The focus priority is:
  //  1. Step-specific content, e.g. transport selection list, if any.
  //  2. Accept button, if visible and enabled.
  //  3. Other transport selection button, if visible.
  //  4. `Cancel` / `Close` button.

  // During widget creation, there is no sheet yet.
  if (!sheet_) {
    return nullptr;
  }

  views::View* intially_focused_sheet_control =
      sheet_->GetInitiallyFocusedView();
  if (intially_focused_sheet_control) {
    return intially_focused_sheet_control;
  }

  if (sheet_->model()->GetAcceptButtonState() ==
      AuthenticatorRequestSheetModel::AcceptButtonState::kEnabled) {
    return GetOkButton();
  }

  if (ShouldOtherMechanismsButtonBeVisible()) {
    return GetExtraView();
  }

  if (sheet_->model()->IsCancelButtonVisible()) {
    return GetCancelButton();
  }

  return nullptr;
}

std::u16string AuthenticatorRequestDialogView::GetWindowTitle() const {
  // During widget creation, there is no sheet yet. The title will be set later.
  return sheet_ ? sheet_->model()->GetStepTitle() : std::u16string();
}

void AuthenticatorRequestDialogView::OnVisibilityChanged(
    content::Visibility visibility) {
  const bool web_contents_was_hidden = web_contents_hidden_;
  web_contents_hidden_ = visibility == content::Visibility::HIDDEN;

  // Show() does not actually show the dialog while the parent WebContents are
  // hidden. Instead, show it when the WebContents become visible again.
  if (web_contents_was_hidden && !web_contents_hidden_ &&
      !GetWidget()->IsVisible()) {
    GetWidget()->Show();
  }
}

void AuthenticatorRequestDialogView::OtherMechanismsButtonPressed() {
  sheet_->model()->OnBack();
}

void AuthenticatorRequestDialogView::ManageDevicesButtonPressed() {
  sheet_->model()->OnManageDevices();
}

void AuthenticatorRequestDialogView::ForgotGPMPinPressed() {
  sheet_->model()->OnForgotGPMPin();
}

void AuthenticatorRequestDialogView::GPMPinOptionChosen(bool is_arbitrary) {
  sheet_->model()->OnGPMPinOptionChosen(is_arbitrary);
}

BEGIN_METADATA(AuthenticatorRequestDialogView)
END_METADATA
