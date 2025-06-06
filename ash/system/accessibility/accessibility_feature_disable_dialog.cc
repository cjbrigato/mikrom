// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/accessibility/accessibility_feature_disable_dialog.h"

#include <memory>
#include <utility>

#include "ash/public/cpp/shell_window_ids.h"
#include "ash/session/session_controller_impl.h"
#include "ash/shell.h"
#include "ash/strings/grit/ash_strings.h"
#include "base/functional/bind.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/base/ui_base_types.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/widget/widget.h"

namespace ash {

AccessibilityFeatureDisableDialog::AccessibilityFeatureDisableDialog(
    int window_title_text_id,
    base::OnceClosure on_accept_callback,
    base::OnceClosure on_cancel_callback)
    : on_cancel_callback_(std::move(on_cancel_callback)) {
  SetModalType(ui::mojom::ModalType::kSystem);
  SetButtonLabel(ui::mojom::DialogButton::kOk,
                 l10n_util::GetStringUTF16(IDS_ASH_YES_BUTTON));
  SetAcceptCallback(std::move(on_accept_callback));
  SetShowCloseButton(false);

  auto on_cancel = [](AccessibilityFeatureDisableDialog* dialog) {
    std::move(dialog->on_cancel_callback_).Run();
  };
  SetCancelCallback(base::BindOnce(on_cancel, base::Unretained(this)));

  SetLayoutManager(std::make_unique<views::FillLayout>());
  SetBorder(views::CreateEmptyBorder(
      views::LayoutProvider::Get()->GetDialogInsetsForContentType(
          views::DialogContentType::kText, views::DialogContentType::kText)));

  auto body_label = std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(window_title_text_id),
      views::style::CONTEXT_DIALOG_BODY_TEXT, views::style::STYLE_PRIMARY);
  body_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  AddChildViewRaw(body_label.release());

  set_margins(views::LayoutProvider::Get()->GetDialogInsetsForContentType(
      views::DialogContentType::kText, views::DialogContentType::kText));

  // Parent the dialog widget to the LockSystemModalContainer, or
  // OverlayContainer to ensure that it will get displayed on respective
  // lock/signin or OOBE screen.
  SessionControllerImpl* session_controller =
      Shell::Get()->session_controller();
  int container_id = kShellWindowId_SystemModalContainer;
  if (session_controller->GetSessionState() ==
      session_manager::SessionState::OOBE) {
    container_id = kShellWindowId_OverlayContainer;
  } else if (session_controller->IsUserSessionBlocked()) {
    container_id = kShellWindowId_LockSystemModalContainer;
  }

  views::Widget* widget = CreateDialogWidget(
      this, nullptr,
      Shell::GetContainer(Shell::GetPrimaryRootWindow(), container_id));
  widget->Show();
}

AccessibilityFeatureDisableDialog::~AccessibilityFeatureDisableDialog() =
    default;

base::WeakPtr<AccessibilityFeatureDisableDialog>
AccessibilityFeatureDisableDialog::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

BEGIN_METADATA(AccessibilityFeatureDisableDialog)
END_METADATA

}  // namespace ash
