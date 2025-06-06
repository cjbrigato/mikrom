// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/printing/print_preview/print_preview_webcontents_adapter_ash.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/unguessable_token.h"
#include "chrome/browser/ash/printing/print_preview/print_preview_dialog_controller_cros.h"
#include "chromeos/crosapi/mojom/print_preview_cros.mojom.h"
#include "components/device_event_log/device_event_log.h"
#include "components/printing/common/print.mojom.h"

using ::printing::mojom::RequestPrintPreviewParams;

namespace ash::printing {

PrintPreviewWebcontentsAdapterAsh::PrintPreviewWebcontentsAdapterAsh()
    : dialog_controller_(std::make_unique<PrintPreviewDialogControllerCros>()) {
  dialog_controller_->AddObserver(this);
}

PrintPreviewWebcontentsAdapterAsh::~PrintPreviewWebcontentsAdapterAsh() {
  dialog_controller_->RemoveObserver(this);
}

void PrintPreviewWebcontentsAdapterAsh::RegisterAshClient(
    crosapi::mojom::PrintPreviewCrosClient* client) {
  ash_client_ = client;
}

void PrintPreviewWebcontentsAdapterAsh::RequestPrintPreview(
    const base::UnguessableToken& token,
    ::printing::mojom::RequestPrintPreviewParamsPtr params,
    RequestPrintPreviewCallback callback) {
  dialog_controller_->GetOrCreatePrintPreviewDialog(token, *params);
  std::move(callback).Run(/*success=*/true);
}

void PrintPreviewWebcontentsAdapterAsh::PrintPreviewDone(
    const base::UnguessableToken& token,
    PrintPreviewDoneCallback callback) {
  dialog_controller_->OnDialogClosed(token);
  std::move(callback).Run(/*success=*/true);
}

void PrintPreviewWebcontentsAdapterAsh::StartGetPreview(
    const base::UnguessableToken& token,
    crosapi::mojom::PrintSettingsPtr settings,
    base::OnceCallback<void(bool)> callback) {
  ash_client_->GeneratePrintPreview(token, std::move(settings),
                                    std::move(callback));
}

void PrintPreviewWebcontentsAdapterAsh::OnDialogClosed(
    const base::UnguessableToken& token) {
  ash_client_->HandleDialogClosed(
      token,
      base::BindOnce(&PrintPreviewWebcontentsAdapterAsh::OnDialogClosedCallback,
                     weak_ptr_factory_.GetWeakPtr()));
}

void PrintPreviewWebcontentsAdapterAsh::OnDialogClosedCallback(bool success) {
  if (!success) {
    PRINTER_LOG(ERROR) << "Failed to close cros print dialog";
  }
}

}  // namespace ash::printing
