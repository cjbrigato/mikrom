// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/web_applications/sub_apps_install_dialog_controller.h"

#include <memory>
#include <string>
#include <vector>

#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/i18n/message_formatter.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/types/cxx23_to_underlying.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/web_applications/sub_apps_install_dialog_controller.h"
#include "chrome/browser/ui/web_applications/web_app_dialogs.h"
#include "chrome/browser/ui/web_applications/web_app_info_image_source.h"
#include "chrome/browser/ui/web_applications/web_app_ui_utils.h"
#include "chrome/browser/web_applications/web_app_install_info.h"
#include "chrome/grit/generated_resources.h"
#include "components/constrained_window/constrained_window_views.h"
#include "components/omnibox/browser/vector_icons.h"
#include "components/webapps/common/web_app_id.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/styled_label.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/widget/widget.h"

namespace web_app {

namespace {

std::optional<SubAppsInstallDialogController::DialogActionForTesting>
    g_dialog_override_for_testing;

constexpr int kSubAppIconSize = 32;

ui::ImageModel GetInstallAppIcon() {
  return ui::ImageModel::FromVectorIcon(
      omnibox::kInstallDesktopIcon, ui::kColorIcon,
      views::LayoutProvider::Get()->GetDistanceMetric(
          views::DISTANCE_BUBBLE_HEADER_VECTOR_ICON_SIZE));
}

std::u16string DialogTitle(int num_sub_apps) {
  return base::i18n::MessageFormatter::FormatWithNamedArgs(
      l10n_util::GetStringUTF16(IDS_SUB_APPS_INSTALL_DIALOG_TITLE),
      "NUM_SUB_APP_INSTALLS", num_sub_apps);
}

ui::DialogModelLabel DialogDescription(int num_sub_apps,
                                       std::u16string parent_app_name) {
  std::u16string description =
      base::i18n::MessageFormatter::FormatWithNamedArgs(
          l10n_util::GetStringUTF16(IDS_SUB_APPS_INSTALL_DIALOG_DESCRIPTION),
          /*name0=*/"NUM_SUB_APP_INSTALLS", num_sub_apps,
          /*name1=*/"APP_NAME", parent_app_name);
  ui::DialogModelLabel label = ui::DialogModelLabel(description);
  label.set_is_secondary().set_allow_character_break();
  return label;
}

std::unique_ptr<views::BubbleDialogModelHost::CustomView>
PermissionsExplanation(int num_sub_apps,
                       std::u16string parent_app_name,
                       base::RepeatingClosure settings_page_callback) {
  std::u16string explanation_string = l10n_util::GetPluralStringFUTF16(
      IDS_SUB_APPS_INSTALL_DIALOG_PERMISSIONS_DESCRIPTION, num_sub_apps);
  const std::u16string manage_permissions_link_string =
      l10n_util::GetStringUTF16(
          IDS_SUB_APPS_INSTALL_DIALOG_MANAGE_PERMISSIONS_LINK);

  std::vector<size_t> offsets;
  const std::u16string formatted_string = base::ReplaceStringPlaceholders(
      explanation_string, {parent_app_name, manage_permissions_link_string},
      &offsets);

  auto label = std::make_unique<views::StyledLabel>();
  label->SetText(formatted_string);
  label->SetDefaultTextStyle(views::style::STYLE_SECONDARY);
  label->SetID(base::to_underlying(
      SubAppsInstallDialogController::SubAppsInstallDialogViewID::
          MANAGE_PERMISSIONS_LINK));

  // Styles the "Manage" part of the string as a link and binds the callback
  // (that opens the parent app's settings page) as the link's action
  label->AddStyleRange(
      gfx::Range(offsets.back(),
                 offsets.back() + manage_permissions_link_string.length()),
      views::StyledLabel::RangeStyleInfo::CreateForLink(
          std::move(settings_page_callback)));

  return std::make_unique<views::BubbleDialogModelHost::CustomView>(
      std::move(label), views::BubbleDialogModelHost::FieldType::kText);
}

std::u16string AcceptButtonLabel() {
  return l10n_util::GetStringUTF16(
      IDS_SUB_APPS_INSTALL_DIALOG_PERMISSIONS_BUTTON);
}

std::u16string CancelButtonLabel() {
  return l10n_util::GetStringUTF16(IDS_SUB_APPS_INSTALL_DIALOG_CANCEL_BUTTON);
}

std::unique_ptr<views::ScrollView> CreateSubAppsListView(
    const std::vector<std::unique_ptr<WebAppInstallInfo>>& sub_apps) {
  // Create vertically scrollable area to contain the list of sub apps.
  auto scroll_view = std::make_unique<views::ScrollView>();
  scroll_view->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  const auto* const layout_provider = views::LayoutProvider::Get();
  scroll_view->ClipHeightTo(
      0, layout_provider->GetDistanceMetric(
             views::DISTANCE_DIALOG_SCROLLABLE_AREA_MAX_HEIGHT));

  // Set the content for the scrollable area to a sensibly layout view.
  auto* sub_app_list =
      scroll_view->SetContents(std::make_unique<views::BoxLayoutView>());

  sub_app_list->SetOrientation(views::BoxLayout::Orientation::kVertical);
  sub_app_list->SetBetweenChildSpacing(layout_provider->GetDistanceMetric(
      views::DISTANCE_CONTROL_LIST_VERTICAL));
  sub_app_list->SetInsideBorderInsets(
      gfx::Insets().set_left(layout_provider->GetDistanceMetric(
          views::DISTANCE_UNRELATED_CONTROL_HORIZONTAL)));

  // Add a box view for each sub app containing the app's icon and title.
  for (const std::unique_ptr<WebAppInstallInfo>& sub_app : sub_apps) {
    auto* box =
        sub_app_list->AddChildView(std::make_unique<views::BoxLayoutView>());
    box->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
    box->SetBetweenChildSpacing(layout_provider->GetDistanceMetric(
        views::DISTANCE_RELATED_LABEL_HORIZONTAL));

    auto* sub_app_icon =
        box->AddChildView(std::make_unique<views::ImageView>());
    sub_app_icon->SetImage(ui::ImageModel::FromImageSkia(
        gfx::ImageSkia(std::make_unique<WebAppInfoImageSource>(
                           kSubAppIconSize, sub_app->icon_bitmaps.any),
                       gfx::Size(kSubAppIconSize, kSubAppIconSize))));
    sub_app_icon->SetGroup(
        base::to_underlying(SubAppsInstallDialogController::
                                SubAppsInstallDialogViewID::SUB_APP_ICON));

    auto* sub_app_label =
        box->AddChildView(std::make_unique<views::Label>(sub_app->title));
    sub_app_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    sub_app_label->SetMultiLine(true);
    sub_app_label->SetGroup(
        base::to_underlying(SubAppsInstallDialogController::
                                SubAppsInstallDialogViewID::SUB_APP_LABEL));
  }

  return scroll_view;
}

}  // namespace

// static
[[nodiscard]] base::AutoReset<
    std::optional<SubAppsInstallDialogController::DialogActionForTesting>>
SubAppsInstallDialogController::SetAutomaticActionForTesting(
    DialogActionForTesting auto_accept) {
  return base::AutoReset<std::optional<DialogActionForTesting>>(
      &g_dialog_override_for_testing, auto_accept);
}

SubAppsInstallDialogController::SubAppsInstallDialogController() = default;

SubAppsInstallDialogController::~SubAppsInstallDialogController() {
  if (widget_) {
    widget_observation_.Reset();
    widget_->CloseWithReason(views::Widget::ClosedReason::kUnspecified);
  }
}

void SubAppsInstallDialogController::Init(
    base::OnceCallback<void(bool)> callback,
    const std::vector<std::unique_ptr<WebAppInstallInfo>>& sub_apps,
    const std::string& parent_app_name,
    const webapps::AppId& parent_app_id,
    Profile* profile,
    gfx::NativeWindow window) {
  if (g_dialog_override_for_testing) {
    switch (g_dialog_override_for_testing.value()) {
      case DialogActionForTesting::kAccept:
        std::move(callback).Run(true);
        break;
      case DialogActionForTesting::kCancel:
        std::move(callback).Run(false);
        break;
    }
    return;
  }

  callback_ = std::move(callback);

  widget_ = CreateWidget(
      base::UTF8ToUTF16(parent_app_name), sub_apps,
      base::BindRepeating(OpenAppSettingsForParentApp, parent_app_id, profile),
      window);
  widget_observation_.Observe(widget_);
  widget_->Show();
}

views::Widget* SubAppsInstallDialogController::CreateWidget(
    const std::u16string parent_app_name,
    const std::vector<std::unique_ptr<web_app::WebAppInstallInfo>>& sub_apps,
    base::RepeatingClosure settings_page_callback,
    gfx::NativeWindow window) {
  int num_sub_apps = sub_apps.size();
  std::unique_ptr<ui::DialogModel> dialog_model =
      ui::DialogModel::Builder()
          .SetInternalName("SubAppsInstallDialogController")
          .SetIcon(GetInstallAppIcon())
          .SetTitle(DialogTitle(num_sub_apps))
          .AddParagraph(DialogDescription(num_sub_apps, parent_app_name))
          .AddCustomField(
              std::make_unique<views::BubbleDialogModelHost::CustomView>(
                  CreateSubAppsListView(sub_apps),
                  views::BubbleDialogModelHost::FieldType::kMenuItem))
          .AddCustomField(
              PermissionsExplanation(sub_apps.size(), parent_app_name,
                                     std::move(settings_page_callback)))
          .AddOkButton(
              base::DoNothing(),
              ui::DialogModel::Button::Params().SetLabel(AcceptButtonLabel()))
          .AddCancelButton(
              base::DoNothing(),
              ui::DialogModel::Button::Params().SetLabel(CancelButtonLabel()))
          .OverrideShowCloseButton(false)
          .Build();

  auto dialog = views::BubbleDialogModelHost::CreateModal(
      std::move(dialog_model), ui::mojom::ModalType::kWindow);
  dialog->SetOwnedByWidget(views::WidgetDelegate::OwnedByWidgetPassKey());

  views::Widget* widget = constrained_window::CreateBrowserModalDialogViews(
      std::move(dialog), window);

  return widget;
}

views::Widget* SubAppsInstallDialogController::GetWidgetForTesting() {
  return widget_;
}

void SubAppsInstallDialogController::OnWidgetDestroying(views::Widget* widget) {
  widget_observation_.Reset();
  widget_ = nullptr;
  switch (widget->closed_reason()) {
    case views::Widget::ClosedReason::kAcceptButtonClicked:
      std::move(callback_).Run(true);
      break;
    case views::Widget::ClosedReason::kUnspecified:
    case views::Widget::ClosedReason::kEscKeyPressed:
    case views::Widget::ClosedReason::kCloseButtonClicked:
    case views::Widget::ClosedReason::kLostFocus:
    case views::Widget::ClosedReason::kCancelButtonClicked:
      std::move(callback_).Run(false);
      break;
  }
}

}  // namespace web_app
