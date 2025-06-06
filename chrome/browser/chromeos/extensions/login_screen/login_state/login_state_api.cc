// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/extensions/login_screen/login_state/login_state_api.h"

#include "base/functional/bind.h"
#include "chrome/browser/ash/crosapi/crosapi_ash.h"
#include "chrome/browser/ash/crosapi/crosapi_manager.h"
#include "chrome/browser/ash/crosapi/login_state_ash.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/extensions/api/login_state.h"
#include "chromeos/crosapi/mojom/login_state.mojom.h"
#include "content/public/browser/browser_context.h"

namespace {
bool IsSigninProfile(const Profile* profile) {
  return profile && profile->GetBaseName().value() == chrome::kInitialProfile;
}
}  // namespace

namespace extensions {

api::login_state::SessionState ToApiEnum(crosapi::mojom::SessionState state) {
  switch (state) {
    case crosapi::mojom::SessionState::kUnknown:
      return api::login_state::SessionState::kUnknown;
    case crosapi::mojom::SessionState::kInOobeScreen:
      return api::login_state::SessionState::kInOobeScreen;
    case crosapi::mojom::SessionState::kInLoginScreen:
      return api::login_state::SessionState::kInLoginScreen;
    case crosapi::mojom::SessionState::kInSession:
      return api::login_state::SessionState::kInSession;
    case crosapi::mojom::SessionState::kInLockScreen:
      return api::login_state::SessionState::kInLockScreen;
    case crosapi::mojom::SessionState::kInRmaScreen:
      return api::login_state::SessionState::kInRmaScreen;
  }
  NOTREACHED();
}

crosapi::mojom::LoginState* GetLoginStateApi() {
  return crosapi::CrosapiManager::Get()->crosapi_ash()->login_state_ash();
}

ExtensionFunction::ResponseAction LoginStateGetProfileTypeFunction::Run() {
  bool is_signin_profile =
      IsSigninProfile(Profile::FromBrowserContext(browser_context()));
  api::login_state::ProfileType profile_type =
      is_signin_profile ? api::login_state::ProfileType::kSigninProfile
                        : api::login_state::ProfileType::kUserProfile;
  return RespondNow(WithArguments(api::login_state::ToString(profile_type)));
}

ExtensionFunction::ResponseAction LoginStateGetSessionStateFunction::Run() {
  auto callback =
      base::BindOnce(&LoginStateGetSessionStateFunction::OnResult, this);

  GetLoginStateApi()->GetSessionState(std::move(callback));
  return did_respond() ? AlreadyResponded() : RespondLater();
}

void LoginStateGetSessionStateFunction::OnResult(
    crosapi::mojom::GetSessionStateResultPtr result) {
  using Result = crosapi::mojom::GetSessionStateResult;
  switch (result->which()) {
    case Result::Tag::kErrorMessage:
      Respond(Error(result->get_error_message()));
      return;
    case Result::Tag::kSessionState:
      api::login_state::SessionState session_state =
          ToApiEnum(result->get_session_state());
      Respond(WithArguments(api::login_state::ToString(session_state)));
      return;
  }
}

}  // namespace extensions
