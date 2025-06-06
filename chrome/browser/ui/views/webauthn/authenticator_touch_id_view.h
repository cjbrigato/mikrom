// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_WEBAUTHN_AUTHENTICATOR_TOUCH_ID_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_WEBAUTHN_AUTHENTICATOR_TOUCH_ID_VIEW_H_

#include <memory>
#include <optional>
#include <utility>

#include "chrome/browser/ui/views/webauthn/authenticator_request_sheet_view.h"
#include "chrome/browser/ui/webauthn/sheet_models.h"
#include "chrome/browser/webauthn/local_authentication_token.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

// Displays a sheet prompting the user to tap their Touch ID sensor to complete
// a passkey flow.
class AuthenticatorTouchIdView : public AuthenticatorRequestSheetView {
  METADATA_HEADER(AuthenticatorTouchIdView, AuthenticatorRequestSheetView)

 public:
  explicit AuthenticatorTouchIdView(
      std::unique_ptr<AuthenticatorTouchIdSheetModel> sheet_model);

  AuthenticatorTouchIdView(const AuthenticatorTouchIdView&) = delete;
  AuthenticatorTouchIdView& operator=(const AuthenticatorTouchIdView&) = delete;

  ~AuthenticatorTouchIdView() override;

 private:
  // Called after the user taps their Touch ID sensor.
  void OnTouchIDComplete(
      std::optional<webauthn::LocalAuthenticationToken> local_auth_token);

  // AuthenticatorRequestSheetView:
  std::unique_ptr<views::View> BuildStepSpecificHeader() override;
  std::pair<std::unique_ptr<views::View>, AutoFocus> BuildStepSpecificContent()
      override;
};

#endif  // CHROME_BROWSER_UI_VIEWS_WEBAUTHN_AUTHENTICATOR_TOUCH_ID_VIEW_H_
