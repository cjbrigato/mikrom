// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_AUTOFILL_PAYMENTS_FILLED_CARD_INFORMATION_BUBBLE_CONTROLLER_IMPL_H_
#define CHROME_BROWSER_UI_AUTOFILL_PAYMENTS_FILLED_CARD_INFORMATION_BUBBLE_CONTROLLER_IMPL_H_

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/autofill/autofill_bubble_controller_base.h"
#include "chrome/browser/ui/autofill/payments/filled_card_information_bubble_controller.h"
#include "components/autofill/core/browser/metrics/payments/filled_card_information_bubble_metrics.h"
#include "components/autofill/core/browser/ui/payments/bubble_show_options.h"
#include "content/public/browser/web_contents_user_data.h"

namespace autofill {

// Implementation of per-tab class to control the filled card information bubble
// and the omnibox icon.
class FilledCardInformationBubbleControllerImpl
    : public AutofillBubbleControllerBase,
      public FilledCardInformationBubbleController,
      public content::WebContentsUserData<
          FilledCardInformationBubbleControllerImpl> {
 public:
  class ObserverForTest {
   public:
    virtual void OnBubbleShown() = 0;
  };

  ~FilledCardInformationBubbleControllerImpl() override;
  FilledCardInformationBubbleControllerImpl(
      const FilledCardInformationBubbleControllerImpl&) = delete;
  FilledCardInformationBubbleControllerImpl& operator=(
      const FilledCardInformationBubbleControllerImpl&) = delete;

  // Show the bubble view.
  void ShowBubble(const FilledCardInformationBubbleOptions& options);

  // Invoked when the omnibox icon is clicked.
  void ReshowBubble();

  // FilledCardInformationBubbleController:
  AutofillBubbleBase* GetBubble() const override;
  std::u16string GetBubbleTitleText() const override;
  const FilledCardInformationBubbleOptions& GetBubbleOptions() const override;
  std::u16string GetCardIndicatorLabel() const override;
  std::u16string GetLearnMoreLinkText() const override;
  std::u16string GetEducationalBodyLabel() const override;
  std::u16string GetCardNumberFieldLabel() const override;
  std::u16string GetExpirationDateFieldLabel() const override;
  std::u16string GetCardholderNameFieldLabel() const override;
  std::u16string GetCvcFieldLabel() const override;
  std::u16string GetValueForField(
      FilledCardInformationBubbleField field) const override;
  std::u16string GetFieldButtonTooltip(
      FilledCardInformationBubbleField field) const override;
  bool ShouldIconBeVisible() const override;
  void OnLinkClicked() override;
  void OnBubbleClosed(PaymentsUiClosedReason closed_reason) override;
  void OnFieldClicked(FilledCardInformationBubbleField field) override;
  bool ShouldShowGooglePayIconInTitle() const override;
  std::u16string GetMaskedCardNameForDescriptionView() const override;
  std::pair<ui::ImageModel, std::optional<ui::ImageModel>>
  GetCardImageForDescriptionView() const override;
  bool EducationalBodyHasLearnMoreLink() const override;

 protected:
  explicit FilledCardInformationBubbleControllerImpl(
      content::WebContents* web_contents);

  // AutofillBubbleControllerBase:
  void PrimaryPageChanged(content::Page& page) override;
  void OnVisibilityChanged(content::Visibility visibility) override;
  PageActionIconType GetPageActionIconType() override;
  void DoShowBubble() override;

 private:
  friend class content::WebContentsUserData<
      FilledCardInformationBubbleControllerImpl>;
  friend class FilledCardInformationBubbleViewsInteractiveUiTest;

  // Updates the system clipboard with the |text|.
  void UpdateClipboard(const std::u16string& text) const;

  // Logs which field was clicked when the user selects a field from the manual
  // fallback bubble.
  void LogFilledCardInformationBubbleFieldClicked(
      FilledCardInformationBubbleField field) const;

  // Returns whether the webcontents related to the controller is active.
  bool IsWebContentsActive();

  void SetEventObserverForTesting(ObserverForTest* observer_for_test);

  // Returns the URL for the learn more link.
  GURL GetLearnMoreUrl() const;

  // Returns whether the filled card information is part of a BNPL flow.
  bool IsBnplFlow() const;

  // Options containing information to show in the bubble.
  FilledCardInformationBubbleOptions options_;

  // Denotes whether the bubble is shown due to user gesture. If this is true,
  // it means the bubble is a reshown bubble.
  bool is_user_gesture_ = false;

  // Whether the omnibox icon for the bubble should be visible.
  bool should_icon_be_visible_ = false;

  // Whether the bubble has been shown at least once. This needs to be reset
  // when there is a page navigation and bubble is therefore no longer
  // applicable.
  bool bubble_has_been_shown_ = false;

  // The field of the most-recently-clicked button, whose value
  // has been copied to the clipboard.
  std::optional<FilledCardInformationBubbleField> clicked_field_;

  raw_ptr<ObserverForTest> observer_for_test_ = nullptr;

  base::WeakPtrFactory<FilledCardInformationBubbleControllerImpl>
      weak_ptr_factory_{this};

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace autofill

#endif  // CHROME_BROWSER_UI_AUTOFILL_PAYMENTS_FILLED_CARD_INFORMATION_BUBBLE_CONTROLLER_IMPL_H_
