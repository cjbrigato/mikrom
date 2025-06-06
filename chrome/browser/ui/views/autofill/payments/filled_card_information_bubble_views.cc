// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/autofill/payments/filled_card_information_bubble_views.h"

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/strings/strcat.h"
#include "chrome/browser/ui/views/autofill/payments/payments_view_util.h"
#include "chrome/browser/ui/views/chrome_layout_provider.h"
#include "components/autofill/core/browser/data_model/payments/credit_card.h"
#include "components/autofill/core/browser/ui/payments/bubble_show_options.h"
#include "components/strings/grit/components_strings.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_frame_view.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/separator.h"
#include "ui/views/controls/styled_label.h"
#include "ui/views/controls/theme_tracking_image_view.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/style/typography.h"

namespace autofill {

namespace {

std::unique_ptr<views::Label> CreateRowItemLabel(std::u16string text) {
  auto label = std::make_unique<views::Label>(
      text, views::style::CONTEXT_DIALOG_BODY_TEXT,
      views::style::STYLE_SECONDARY);
  label->SetMultiLine(false);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  return label;
}

// Create a button container which has smaller paddings between the label and
// the button, and also make sure buttons don't stretch horizontally.
std::unique_ptr<views::BoxLayoutView> CreateButtonContainer() {
  auto button_container = std::make_unique<views::BoxLayoutView>();
  button_container->SetOrientation(views::BoxLayout::Orientation::kVertical);
  button_container->SetCrossAxisAlignment(
      views::BoxLayout::CrossAxisAlignment::kStart);
  button_container->SetBetweenChildSpacing(
      ChromeLayoutProvider::Get()->GetDistanceMetric(
          views::DISTANCE_RELATED_CONTROL_VERTICAL));
  return button_container;
}

}  // namespace

FilledCardInformationBubbleViews::FilledCardInformationBubbleViews(
    views::View* anchor_view,
    content::WebContents* web_contents,
    FilledCardInformationBubbleController* controller)
    : AutofillLocationBarBubble(anchor_view, web_contents),
      controller_(controller) {
  DCHECK(controller_);
  SetShowIcon(true);
  SetShowCloseButton(true);
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  set_fixed_width(views::LayoutProvider::Get()->GetDistanceMetric(
      views::DISTANCE_BUBBLE_PREFERRED_WIDTH));
  set_margins(ChromeLayoutProvider::Get()->GetDialogInsetsForContentType(
      views::DialogContentType::kText, views::DialogContentType::kControl));
  // Since this bubble provides full card information for filling the credit
  // card form, users may interact with this bubble multiple times. This is to
  // make sure the bubble does not dismiss itself after one interaction. The
  // bubble will instead be dismissed when page navigates or the close button is
  // clicked.
  set_close_on_deactivate(false);
}

FilledCardInformationBubbleViews::~FilledCardInformationBubbleViews() {
  Hide();
}

void FilledCardInformationBubbleViews::Hide() {
  CloseBubble();
  if (controller_) {
    controller_->OnBubbleClosed(
        GetPaymentsUiClosedReasonFromWidget(GetWidget()));
  }
  controller_ = nullptr;
}

void FilledCardInformationBubbleViews::Init() {
  auto* const layout_provider = ChromeLayoutProvider::Get();
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(),
      layout_provider->GetDistanceMetric(
          views::DISTANCE_UNRELATED_CONTROL_VERTICAL)));

  AddCardDescriptionView(this);

  // Construct a separator view.
  AddChildView(std::make_unique<views::Separator>());

  // Construct a label view to show the secondary explanation text.
  std::u16string explanation = controller_->GetEducationalBodyLabel();
  if (!explanation.empty()) {
    auto* const explanation_label =
        AddChildView(std::make_unique<views::StyledLabel>());
    explanation_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    explanation_label->SetTextContext(views::style::CONTEXT_DIALOG_BODY_TEXT);
    explanation_label->SetDefaultTextStyle(views::style::STYLE_SECONDARY);
    explanation_label->SetText(explanation);

    if (controller_->EducationalBodyHasLearnMoreLink()) {
      views::StyledLabel::RangeStyleInfo style_info =
          views::StyledLabel::RangeStyleInfo::CreateForLink(base::BindRepeating(
              &FilledCardInformationBubbleViews::LearnMoreLinkClicked,
              weak_ptr_factory_.GetWeakPtr()));

      uint32_t offset =
          explanation.length() - controller_->GetLearnMoreLinkText().length();
      explanation_label->AddStyleRange(
          gfx::Range(offset,
                     offset + controller_->GetLearnMoreLinkText().length()),
          style_info);
    }
  }

  AddCardDetailButtons(this);
}

void FilledCardInformationBubbleViews::AddedToWidget() {
  if (controller_->ShouldShowGooglePayIconInTitle()) {
    GetBubbleFrameView()->SetTitleView(
        std::make_unique<TitleWithIconAfterLabelView>(
            GetWindowTitle(), TitleWithIconAfterLabelView::Icon::GOOGLE_PAY));
  } else {
    auto title_view = std::make_unique<views::Label>(
        GetWindowTitle(), views::style::CONTEXT_DIALOG_TITLE);
    title_view->SetHorizontalAlignment(gfx::ALIGN_TO_HEAD);
    title_view->SetMultiLine(true);
    GetBubbleFrameView()->SetTitleView(std::move(title_view));
  }
}

std::u16string FilledCardInformationBubbleViews::GetWindowTitle() const {
  return controller_ ? controller_->GetBubbleTitleText() : std::u16string();
}

void FilledCardInformationBubbleViews::WindowClosing() {
  if (controller_) {
    controller_->OnBubbleClosed(
        GetPaymentsUiClosedReasonFromWidget(GetWidget()));
    controller_ = nullptr;
  }
}

void FilledCardInformationBubbleViews::OnWidgetDestroying(
    views::Widget* widget) {
  LocationBarBubbleDelegateView::OnWidgetDestroying(widget);
  if (!widget->IsClosed()) {
    return;
  }
  DCHECK_NE(widget->closed_reason(),
            views::Widget::ClosedReason::kAcceptButtonClicked);
  DCHECK_NE(widget->closed_reason(),
            views::Widget::ClosedReason::kCancelButtonClicked);
}

std::unique_ptr<views::MdTextButton>
FilledCardInformationBubbleViews::CreateRowItemButtonForField(
    FilledCardInformationBubbleField field) {
  std::u16string text = controller_->GetValueForField(field);
  auto button = std::make_unique<views::MdTextButton>(
      base::BindRepeating(&FilledCardInformationBubbleViews::OnFieldClicked,
                          weak_ptr_factory_.GetWeakPtr(), field),
      text, views::style::CONTEXT_BUTTON);
  button->SetCornerRadius(ChromeLayoutProvider::Get()->GetCornerRadiusMetric(
      views::Emphasis::kMaximum, button->GetPreferredSize()));
  fields_to_buttons_map_[field] = button.get();
  return button;
}

void FilledCardInformationBubbleViews::AddCardDescriptionView(
    views::View* parent) {
  const FilledCardInformationBubbleOptions& options =
      controller_->GetBubbleOptions();
  auto* const layout_provider = ChromeLayoutProvider::Get();

  /*
  |----------------------------------------------------------------|
  |             |  masked_card_name | masked_card_number_last_four |
  | card_image  |                                                  |
  |             |  card_indicator                                  |
  |----------------------------------------------------------------|
  */
  // Construct the container view as above.
  auto* card_information_container =
      parent->AddChildView(std::make_unique<views::BoxLayoutView>());
  card_information_container->SetCrossAxisAlignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  card_information_container->SetBetweenChildSpacing(
      layout_provider->GetDistanceMetric(
          views::DISTANCE_RELATED_CONTROL_HORIZONTAL));

  std::pair<ui::ImageModel, std::optional<ui::ImageModel>> card_images =
      controller_->GetCardImageForDescriptionView();
  auto* card_image_view = card_information_container->AddChildView(
      std::make_unique<views::ThemeTrackingImageView>(
          /*light_image_model=*/card_images.first,
          /*dark_image_model=*/card_images.second.value_or(card_images.first),
          /*get_background_color_callback=*/
          base::BindRepeating(
              [](views::View* view) {
                return ui::ColorVariant(view->GetColorProvider()->GetColor(
                    ui::kColorDialogBackground));
              },
              base::Unretained(this))));
  card_image_view->SetID(kCardImage);

  // Add a child container view for the two-line text view.
  auto* card_text_view = card_information_container->AddChildView(
      std::make_unique<views::BoxLayoutView>());
  card_text_view->SetOrientation(views::BoxLayout::Orientation::kVertical);
  card_text_view->SetCrossAxisAlignment(
      views::BoxLayout::CrossAxisAlignment::kStart);
  card_information_container->SetBetweenChildSpacing(
      layout_provider->GetDistanceMetric(DISTANCE_TOAST_LABEL_VERTICAL));

  // First line of the text content, the card network/description and last four.
  // Note that the description can be truncated, but the last four digits never
  // are.
  auto* first_line =
      card_text_view->AddChildView(std::make_unique<views::BoxLayoutView>());
  first_line->SetBetweenChildSpacing(layout_provider->GetDistanceMetric(
      DISTANCE_RELATED_LABEL_HORIZONTAL_LIST));
  // `kEnd` aligns the child views to the end edge of the parent view so that
  // `card_last_four_view` is positioned first at the end and is never pushed
  // out.
  first_line->SetMainAxisAlignment(views::BoxLayout::MainAxisAlignment::kEnd);
  auto* card_name_view =
      first_line->AddChildView(std::make_unique<views::Label>(
          controller_->GetMaskedCardNameForDescriptionView(),
          views::style::CONTEXT_DIALOG_BODY_TEXT, views::style::STYLE_PRIMARY));
  card_name_view->SetID(kCardName);
  auto* card_last_four_view =
      first_line->AddChildView(std::make_unique<views::Label>(
          options.masked_card_number_last_four,
          views::style::CONTEXT_DIALOG_BODY_TEXT, views::style::STYLE_PRIMARY));
  // Keep card_last_four_view at its natural size.
  first_line->SetFlexForView(card_last_four_view, /*flex=*/0);

  // Second line of the text content, the "Card" indicator label.
  card_text_view->AddChildView(std::make_unique<views::Label>(
      controller_->GetCardIndicatorLabel(),
      views::style::CONTEXT_DIALOG_BODY_TEXT, views::style::STYLE_SECONDARY));
}

void FilledCardInformationBubbleViews::AddCardDetailButtons(
    views::View* parent) {
  auto* const layout_provider = ChromeLayoutProvider::Get();

  // Card number.
  auto* card_number_container = parent->AddChildView(CreateButtonContainer());
  card_number_container->AddChildView(
      CreateRowItemLabel(controller_->GetCardNumberFieldLabel()));
  card_number_container->AddChildView(CreateRowItemButtonForField(
      FilledCardInformationBubbleField::kCardNumber));

  // Expiration date.
  auto* expiration_date_container =
      parent->AddChildView(CreateButtonContainer());
  expiration_date_container->AddChildView(
      CreateRowItemLabel(controller_->GetExpirationDateFieldLabel()));
  auto* expiry_row =
      expiration_date_container->AddChildView(std::make_unique<views::View>());
  expiry_row->SetLayoutManager(std::make_unique<views::FlexLayout>())
      ->SetOrientation(views::LayoutOrientation::kHorizontal)
      .SetMainAxisAlignment(views::LayoutAlignment::kStart)
      .SetCrossAxisAlignment(views::LayoutAlignment::kCenter)
      // Don't apply default host view margin for the `expiry-row`.
      .SetIgnoreDefaultMainAxisMargins(true)
      // Make sure between-child padding is not double of the child view margin
      // set below.
      .SetCollapseMargins(true)
      // Set child view margin.
      .SetDefault(
          views::kMarginsKey,
          gfx::Insets::VH(0, layout_provider->GetDistanceMetric(
                                 views::DISTANCE_RELATED_BUTTON_HORIZONTAL)));
  expiry_row->AddChildView(CreateRowItemButtonForField(
      FilledCardInformationBubbleField::kExpirationMonth));
  expiry_row->AddChildView(std::make_unique<views::Label>(u"/"));
  // TODO(crbug.com/40176273): Validate this works when the expiration year
  // field is for two-digit numbers
  expiry_row->AddChildView(CreateRowItemButtonForField(
      FilledCardInformationBubbleField::kExpirationYear));

  // Cardholder name.
  auto* cardholder_name_container =
      parent->AddChildView(CreateButtonContainer());
  cardholder_name_container->AddChildView(
      CreateRowItemLabel(controller_->GetCardholderNameFieldLabel()));
  cardholder_name_container->AddChildView(CreateRowItemButtonForField(
      FilledCardInformationBubbleField::kCardholderName));

  // CVC.
  auto* cvc_container = parent->AddChildView(CreateButtonContainer());
  cvc_container->AddChildView(
      CreateRowItemLabel(controller_->GetCvcFieldLabel()));
  cvc_container->AddChildView(
      CreateRowItemButtonForField(FilledCardInformationBubbleField::kCvc));

  UpdateButtonTooltipsAndAccessibleNames();
}

void FilledCardInformationBubbleViews::OnFieldClicked(
    FilledCardInformationBubbleField field) {
  controller_->OnFieldClicked(field);
  UpdateButtonTooltipsAndAccessibleNames();
}

void FilledCardInformationBubbleViews::
    UpdateButtonTooltipsAndAccessibleNames() {
  for (auto& pair : fields_to_buttons_map_) {
    std::u16string tooltip = controller_->GetFieldButtonTooltip(pair.first);
    pair.second->SetTooltipText(tooltip);
    pair.second->GetViewAccessibility().SetName(
        base::StrCat({pair.second->GetText(), u" ", tooltip}));
  }
}

void FilledCardInformationBubbleViews::LearnMoreLinkClicked() {
  if (controller_) {
    controller_->OnLinkClicked();
  }
}

BEGIN_METADATA(FilledCardInformationBubbleViews)
END_METADATA

}  // namespace autofill
