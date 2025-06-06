// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/accessibility/dictation_button_tray.h"

#include "ash/accessibility/accessibility_controller.h"
#include "ash/constants/ash_pref_names.h"
#include "ash/constants/tray_background_view_catalog.h"
#include "ash/display/window_tree_host_manager.h"
#include "ash/metrics/user_metrics_recorder.h"
#include "ash/public/cpp/accessibility_controller_enums.h"
#include "ash/public/cpp/shelf_config.h"
#include "ash/resources/vector_icons/vector_icons.h"
#include "ash/session/session_controller_impl.h"
#include "ash/shell.h"
#include "ash/strings/grit/ash_strings.h"
#include "ash/style/ash_color_id.h"
#include "ash/system/progress_indicator/progress_indicator.h"
#include "ash/system/tray/tray_constants.h"
#include "ash/system/tray/tray_container.h"
#include "ash/system/tray/tray_utils.h"
#include "components/prefs/pref_service.h"
#include "ui/accessibility/accessibility_features.h"
#include "ui/base/ime/text_input_client.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "ui/views/controls/image_view.h"

namespace ash {
namespace {

// Helper function that creates an image for the dictation icon.
// |active| means Dictation is actively listening for speech. The icon
// changes to an "on" icon from "off" when Dictation is listening.
// |enabled| indicates whether the tray button is enabled, i.e. clickable.
// A secondary color is used to indicate the icon is not enabled.
ui::ImageModel GetIconImage(bool active, bool enabled) {
  // The color will change based on whether this tray is active or not.
  ui::ColorId color_id =
      enabled ? (active ? cros_tokens::kCrosSysSystemOnPrimaryContainer
                        : cros_tokens::kCrosSysOnSurface)
              : cros_tokens::kCrosSysSecondary;

  return active
             ? ui::ImageModel::FromVectorIcon(kDictationOnNewuiIcon, color_id)
             : ui::ImageModel::FromVectorIcon(kDictationOffNewuiIcon, color_id);
}

bool IsDictationActive() {
  return Shell::Get()->accessibility_controller()->dictation_active();
}

}  // namespace

DictationButtonTray::DictationButtonTray(
    Shelf* shelf,
    TrayBackgroundViewCatalogName catalog_name)
    : TrayBackgroundView(shelf, catalog_name), download_progress_(0) {
  SetCallback(base::BindRepeating(
      &DictationButtonTray::OnDictationButtonPressed, base::Unretained(this)));

  Shell* shell = Shell::Get();
  ui::TextInputClient* client =
      shell->window_tree_host_manager()->input_method()->GetTextInputClient();
  in_text_input_ =
      (client && client->GetTextInputType() != ui::TEXT_INPUT_TYPE_NONE);

  // If a view that accepts text input is focused, make the tray enabled (i.e.
  // clickable). However, at this point the dictation is not active (i.e.
  // dictation is not listening to speech).
  SetEnabled(in_text_input_);
  SetIsActive(false);

  const ui::ImageModel icon_image =
      GetIconImage(/*active=*/false, /*enabled=*/GetEnabled());
  const int vertical_padding = (kTrayItemSize - icon_image.Size().height()) / 2;
  const int horizontal_padding =
      (kTrayItemSize - icon_image.Size().height()) / 2;
  auto icon = std::make_unique<views::ImageView>();
  icon->SetImage(icon_image);
  icon->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::VH(vertical_padding, horizontal_padding)));
  icon->SetTooltipText(
      l10n_util::GetStringUTF16(IDS_ASH_STATUS_TRAY_ACCESSIBILITY_DICTATION));
  icon_ = tray_container()->AddChildView(std::move(icon));

  shell->AddShellObserver(this);
  shell->accessibility_controller()->AddObserver(this);
  shell->session_controller()->AddObserver(this);

  GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_ASH_DICTATION_BUTTON_ACCESSIBLE_NAME));
}

DictationButtonTray::~DictationButtonTray() {
  // This may be called during shutdown in which case some of the
  // ash objects may already be destroyed.
  Shell* shell = Shell::Get();
  if (!shell) {
    return;
  }
  shell->RemoveShellObserver(this);
  auto* accessibility_controller = shell->accessibility_controller();
  if (accessibility_controller) {
    accessibility_controller->RemoveObserver(this);
  }
  auto* session_controller = shell->session_controller();
  if (session_controller) {
    session_controller->RemoveObserver(this);
  }
  input_method_observation_.Reset();
}

void DictationButtonTray::OnDictationStarted() {
  UpdateStateAndIcon(/*is_dictation_active=*/true, GetEnabled());
}

void DictationButtonTray::OnDictationEnded() {
  UpdateStateAndIcon(/*is_dictation_active=*/false, GetEnabled());
}

void DictationButtonTray::OnAccessibilityStatusChanged() {
  UpdateVisibility();
  CheckDictationStatusAndUpdateIcon();
}

void DictationButtonTray::OnSessionStateChanged(
    session_manager::SessionState state) {
  CheckDictationStatusAndUpdateIcon();
}

void DictationButtonTray::Initialize() {
  TrayBackgroundView::Initialize();
  UpdateVisibility();
}

void DictationButtonTray::ClickedOutsideBubble(const ui::LocatedEvent& event) {}

void DictationButtonTray::UpdateTrayItemColor(bool is_active) {
  if (progress_indicator_) {
    progress_indicator_->SetColorId(
        is_active ? cros_tokens::kCrosSysSystemOnPrimaryContainer
                  : cros_tokens::kCrosSysPrimary);
  }
}

void DictationButtonTray::HandleLocaleChange() {
  icon_->SetTooltipText(
      l10n_util::GetStringUTF16(IDS_ASH_STATUS_TRAY_ACCESSIBILITY_DICTATION));
}

void DictationButtonTray::HideBubbleWithView(
    const TrayBubbleView* bubble_view) {
  // This class has no bubbles to hide.
}

void DictationButtonTray::OnThemeChanged() {
  TrayBackgroundView::OnThemeChanged();
  if (progress_indicator_)
    progress_indicator_->InvalidateLayer();
}

void DictationButtonTray::Layout(PassKey) {
  LayoutSuperclass<TrayBackgroundView>(this);
  UpdateProgressIndicatorBounds();
}

void DictationButtonTray::HideBubble(const TrayBubbleView* bubble_view) {
  // This class has no bubbles to hide.
}

void DictationButtonTray::OnCaretBoundsChanged(
    const ui::TextInputClient* client) {
  TextInputChanged(client);
}

void DictationButtonTray::OnTextInputStateChanged(
    const ui::TextInputClient* client) {
  TextInputChanged(client);
}

void DictationButtonTray::UpdateOnSpeechRecognitionDownloadChanged(
    int download_progress) {
  if (!visible_preferred())
    return;

  const bool download_in_progress =
      download_progress > 0 && download_progress < 100;
  const bool is_dictation_enabled = !download_in_progress && in_text_input_;
  UpdateStateAndIcon(IsDictationActive(), is_dictation_enabled);
  icon_->SetTooltipText(l10n_util::GetStringUTF16(
      download_in_progress
          ? IDS_ASH_ACCESSIBILITY_DICTATION_BUTTON_TOOLTIP_SODA_DOWNLOADING
          : IDS_ASH_STATUS_TRAY_ACCESSIBILITY_DICTATION));

  // Progress indicator.
  download_progress_ = download_progress;
  if (!progress_indicator_) {
    // A progress indicator that is only visible when a SODA download is
    // in-progress and a subscription to receive notification of progress
    // changed events.
    progress_indicator_ =
        ProgressIndicator::CreateDefaultInstance(base::BindRepeating(
            [](DictationButtonTray* tray) -> std::optional<float> {
              // If download is in-progress, return the progress as a decimal.
              // Otherwise, the progress indicator shouldn't be painted.
              const int progress = tray->download_progress();
              return (progress > 0 && progress < 100)
                         ? progress / 100.f
                         : ProgressIndicator::kProgressComplete;
            },
            base::Unretained(this)));
    progress_indicator_->SetInnerIconVisible(false);
    layer()->Add(progress_indicator_->CreateLayer(base::BindRepeating(
        [](const DictationButtonTray* self, ui::ColorId color_id) {
          return self->GetColorProvider()->GetColor(color_id);
        },
        base::Unretained(this))));
    UpdateProgressIndicatorBounds();
    UpdateTrayItemColor(is_active());
  }
  progress_indicator_->InvalidateLayer();
}

void DictationButtonTray::OnDictationButtonPressed(const ui::Event& event) {
  Shell::Get()->accessibility_controller()->ToggleDictationFromSource(
      DictationToggleSource::kButton);
  CheckDictationStatusAndUpdateIcon();
}

void DictationButtonTray::UpdateProgressIndicatorBounds() {
  if (progress_indicator_)
    progress_indicator_->layer()->SetBounds(GetBackgroundBounds());
}

void DictationButtonTray::UpdateVisibility() {
  const bool is_visible =
      Shell::Get()->accessibility_controller()->dictation().enabled();
  if (is_visible && !input_method_observation_.IsObserving()) {
    input_method_observation_.Observe(
        Shell::Get()->window_tree_host_manager()->input_method());
  } else if (!is_visible) {
    input_method_observation_.Reset();
  }

  SetVisiblePreferred(is_visible);
}

void DictationButtonTray::UpdateStateAndIcon(bool is_dictation_active,
                                             bool is_dictation_enabled) {
  const bool should_update_icon = is_active() != is_dictation_active ||
                                  GetEnabled() != is_dictation_enabled;
  SetIsActive(is_dictation_active);
  SetEnabled(is_dictation_enabled);

  if (should_update_icon) {
    icon_->SetImage(GetIconImage(is_dictation_active, is_dictation_enabled));
  }
}

void DictationButtonTray::CheckDictationStatusAndUpdateIcon() {
  UpdateStateAndIcon(IsDictationActive(), GetEnabled());
}

void DictationButtonTray::TextInputChanged(const ui::TextInputClient* client) {
  in_text_input_ =
      client && client->GetTextInputType() != ui::TEXT_INPUT_TYPE_NONE;
  const bool is_dictation_enabled =
      (download_progress_ <= 0 || download_progress_ >= 100) && in_text_input_;
  UpdateStateAndIcon(IsDictationActive(), is_dictation_enabled);
}

BEGIN_METADATA(DictationButtonTray)
END_METADATA

}  // namespace ash
