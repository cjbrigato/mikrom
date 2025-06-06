// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_WEBID_ACCOUNT_SELECTION_MODAL_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_WEBID_ACCOUNT_SELECTION_MODAL_VIEW_H_

#include <memory>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/views/webid/account_selection_view_base.h"
#include "components/image_fetcher/core/image_fetcher.h"
#include "content/public/browser/identity_request_account.h"
#include "content/public/browser/identity_request_dialog_controller.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "third_party/blink/public/mojom/webid/federated_auth_request.mojom.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/progress_bar.h"
#include "ui/views/controls/throbber.h"
#include "ui/views/window/dialog_delegate.h"

namespace views {
class BoxLayoutView;
}  // namespace views

namespace webid {

// This view is used for the "active" flow for fedCM. This is only ever shown as
// a result of user action (e.g. clicking a button).
class AccountSelectionModalView : public views::DialogDelegateView,
                                  public AccountSelectionViewBase {
  METADATA_HEADER(AccountSelectionModalView, views::DialogDelegateView)

 public:
  AccountSelectionModalView(
      const content::RelyingPartyData& rp_data,
      const std::optional<std::u16string>& idp_title,
      blink::mojom::RpContext rp_context,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      FedCmAccountSelectionView* owner);
  ~AccountSelectionModalView() override;
  AccountSelectionModalView(const AccountSelectionModalView&) = delete;
  AccountSelectionModalView& operator=(const AccountSelectionModalView&) =
      delete;

  // AccountSelectionViewBase:
  void ShowMultiAccountPicker(
      const std::vector<IdentityRequestAccountPtr>& accounts,
      const std::vector<IdentityProviderDataPtr>& idp_list,
      const gfx::Image& rp_icon,
      bool show_back_button) override;

  void ShowVerifyingSheet(const IdentityRequestAccountPtr& account,
                          const std::u16string& title) override;

  void ShowSingleAccountConfirmDialog(const IdentityRequestAccountPtr& account,
                                      bool show_back_button) override;

  void ShowFailureDialog(
      const std::u16string& idp_for_display,
      const content::IdentityProviderMetadata& idp_metadata) override;

  void ShowErrorDialog(
      const std::u16string& idp_for_display,
      const content::IdentityProviderMetadata& idp_metadata,
      const std::optional<content::IdentityCredentialTokenError>& error)
      override;

  void ShowRequestPermissionDialog(
      const IdentityRequestAccountPtr& account) override;

  std::string GetDialogTitle() const override;
  std::optional<std::string> GetDialogSubtitle() const override;

  // views::DialogDelegateView:
  views::View* GetInitiallyFocusedView() override;
  void VisibilityChanged(View* starting_from, bool is_visible) override;

  std::u16string GetQueuedAnnouncementForTesting();

 private:
  friend class AccountSelectionModalViewTest;

  // Returns a View for header of an account chooser. It contains text to prompt
  // the user to sign in to an RP with an account from an IDP.
  std::unique_ptr<views::View> CreateHeader();

  // Returns a View for multiple account chooser. It contains the info for each
  // account in a button, so the user can pick an account.
  std::unique_ptr<views::View> CreateMultipleAccountChooser(
      const std::vector<IdentityRequestAccountPtr>& accounts);

  // Returns a View to display account rows. It contains one row per account.
  // `should_hover` determines whether the rows are clickable.
  // `show_separator` determines whether rows should be surrounded by a
  // separator.
  std::unique_ptr<views::View> CreateAccountRows(
      const std::vector<IdentityRequestAccountPtr>& accounts,
      bool should_hover,
      bool show_separator,
      bool is_request_permission_dialog);

  // Returns a View for an account row that acts as a placeholder.
  std::unique_ptr<views::View> CreatePlaceholderAccountRow();

  // Returns a View for a row of custom buttons. A cancel button is always
  // shown. A continue button, use other account button and/or back button is
  // shown if `continue_callback`, `use_other_account_callback` and/or
  // `back_callback` is specified respectively. If `use_other_account_callback`
  // is specified, `back_callback` should NOT be specified and vice versa.
  std::unique_ptr<views::View> CreateButtonRow(
      std::optional<views::Button::PressedCallback> continue_callback,
      std::optional<views::Button::PressedCallback> use_other_account_callback,
      std::optional<views::Button::PressedCallback> back_callback);

  // Returns a View containing the background and icon containers. The icon
  // containers are not visible until they are configured through
  // `ConfigureBrandImageView`.
  std::unique_ptr<views::View> CreateIconHeaderView();

  // Returns a BoxLayoutView containing a spinner.
  std::unique_ptr<views::BoxLayoutView> CreateSpinnerIconView();

  // Returns a BoxLayoutView containing the IDP icon. If the image cannot be
  // fetched, a globe icon is shown.
  std::unique_ptr<views::BoxLayoutView> CreateIdpIconView();

  // Returns a BoxLayoutView containing the IDP icon, arrow icon and RP icon in
  // that order, horizontally.
  std::unique_ptr<views::BoxLayoutView> CreateCombinedIconsView();

  // Hides `header_icon_spinner_` and shows `idp_brand_icon_` upon successfully
  // setting the IDP icon.
  void OnIdpBrandIconSet();

  // Hides `header_icon_spinner_`, `idp_brand_icon_` and shows `combined_icons_`
  // upon successfully setting the IDP and RP icons.
  void OnCombinedIconsSet();

  // Removes all child views and dangling pointers and adjust header with
  // progress bar and body label if needed.
  void RemoveNonHeaderChildViewsAndUpdateHeaderIfNeeded();

  // Removes `combined_icons_` and all its child views, if available.
  void MaybeRemoveCombinedIconsView();

  // Notifies the observer of the account selection and updates the continue
  // button into a spinner button.
  void OnContinueButtonClicked(const IdentityRequestAccountPtr& account,
                               const ui::Event& event);

  // Notifies the observer of the use other account button being clicked.
  void OnUseOtherAccountButtonClicked(const GURL& idp_config_url,
                                      const GURL& idp_login_url,
                                      const ui::Event& event);

  // Updates the button to have a spinner appear in the middle of it.
  void ReplaceButtonWithSpinner(
      views::MdTextButton* button,
      ui::ColorId spinner_color = ui::kColorButtonForeground,
      ui::ColorId button_color = ui::kColorButtonBackground);

  // Helper method to show the given accounts.
  void ShowAccounts(const std::vector<IdentityRequestAccountPtr>& accounts,
                    bool is_single_account_chooser);

  // The following are raw_ptrs for views in the header. These do not need to be
  // reset by RemoveNonHeaderChildViewsAndUpdateHeaderIfNeeded().
  // View containing the header.
  raw_ptr<views::View> header_view_ = nullptr;
  // View containing the header icons.
  raw_ptr<views::View> header_icon_view_ = nullptr;
  // View containing the title.
  raw_ptr<views::Label> title_label_ = nullptr;
  // View containing the body.
  raw_ptr<views::Label> body_label_ = nullptr;
  // View containing the IDP brand icon image. This view is constructed in the
  // loading dialog but is only visible after the loading dialog.
  raw_ptr<BrandIconImageView> idp_brand_icon_ = nullptr;
  // View containing the spinner in the header. This spinner is shown until the
  // IDP brand icon is fetched.
  raw_ptr<views::Throbber> header_icon_spinner_ = nullptr;
  // View containing the IDP brand icon image meant to be shown in the request
  // permission dialog together with the RP icon. This icon is a smaller version
  // of `idp_brand_icon_` because it has to share the space in the header with
  // the RP icon.
  raw_ptr<BrandIconImageView> combined_icons_idp_brand_icon_ = nullptr;
  // View containing the RP brand icon image in the request permission dialog.
  raw_ptr<BrandIconImageView> combined_icons_rp_brand_icon_ = nullptr;
  // BoxLayoutView containing the IDP icon, arrow icon and RP icon in that
  // order, horizontally. This view is constructed in the loading dialog but
  // will be made visible only in the request permission dialog.
  raw_ptr<views::BoxLayoutView> combined_icons_ = nullptr;

  // The following are raw_ptrs for view outside of the header. These MUST be
  // reset in RemoveNonHeaderChildViewsAndUpdateHeaderIfNeeded(), as not
  // resetting could result in a UAF.
  // View containing the use other account button.
  raw_ptr<views::MdTextButton> use_other_account_button_ = nullptr;
  // View containing the back button.
  raw_ptr<views::View> back_button_ = nullptr;
  // View containing the continue button.
  raw_ptr<views::MdTextButton> continue_button_ = nullptr;
  // View containing the cancel button.
  raw_ptr<views::View> cancel_button_ = nullptr;
  // View containing the account chooser.
  raw_ptr<views::View> account_chooser_ = nullptr;
  // View containing the view to focus on in the verifying sheet.
  raw_ptr<views::View> verifying_focus_view_ = nullptr;

  // Whether a spinner is present.
  bool has_spinner_{false};

  // Whether the title has been announced for accessibility.
  bool has_announced_title_{false};

  // The announcement that should be made upon view focus, if screen reader is
  // turned on.
  std::u16string queued_announcement_;

  // The title for the modal dialog.
  std::u16string title_;

  // The subtitle for the modal dialog.
  std::u16string subtitle_;

  // Used to ensure that callbacks are not run if the AccountSelectionModalView
  // is destroyed.
  base::WeakPtrFactory<AccountSelectionModalView> weak_ptr_factory_{this};
};

}  // namespace webid

#endif  // CHROME_BROWSER_UI_VIEWS_WEBID_ACCOUNT_SELECTION_MODAL_VIEW_H_
