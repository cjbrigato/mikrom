// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/autofill/model/autofill_tab_helper.h"

#import "base/check.h"
#import "base/memory/ptr_util.h"
#import "components/autofill/ios/browser/autofill_agent.h"
#import "components/autofill/ios/browser/autofill_driver_ios.h"
#import "components/autofill/ios/browser/autofill_driver_ios_factory.h"
#import "components/autofill/ios/form_util/child_frame_registrar.h"
#import "ios/chrome/browser/autofill/model/autofill_agent_delegate.h"
#import "ios/chrome/browser/autofill/ui_bundled/chrome_autofill_client_ios.h"
#import "ios/chrome/browser/infobars/model/infobar_manager_impl.h"
#import "ios/chrome/browser/shared/model/application_context/application_context.h"
#import "ios/chrome/browser/shared/model/profile/profile_ios.h"
#import "ios/chrome/browser/shared/public/commands/autofill_commands.h"
#import "ios/chrome/browser/shared/public/commands/snackbar_commands.h"

namespace {

bool IsAutofillAcrossIframesEnabled() {
  return base::FeatureList::IsEnabled(
      autofill::features::kAutofillAcrossIframesIos);
}

}  // namespace

AutofillTabHelper::~AutofillTabHelper() = default;

void AutofillTabHelper::SetBaseViewController(
    UIViewController* base_view_controller) {
  autofill_client_->SetBaseViewController(base_view_controller);
}

void AutofillTabHelper::SetAutofillHandler(
    id<AutofillCommands> autofill_handler) {
  autofill_client_->set_commands_handler(autofill_handler);
}

void AutofillTabHelper::SetSnackbarHandler(
    id<SnackbarCommands> snackbar_handler) {
  if (snackbar_handler) {
    autofill_agent_delegate_ =
        [[AutofillAgentDelegate alloc] initWithCommandHandler:snackbar_handler];
    autofill_agent_.delegate = autofill_agent_delegate_;
  } else {
    autofill_agent_delegate_ = nil;
    autofill_agent_.delegate = nil;
  }
}

id<FormSuggestionProvider> AutofillTabHelper::GetSuggestionProvider() {
  return autofill_agent_;
}

AutofillTabHelper::AutofillTabHelper(web::WebState* web_state)
    : profile_(ProfileIOS::FromBrowserState(web_state->GetBrowserState())),
      autofill_agent_([[AutofillAgent alloc]
          initWithPrefService:profile_->GetPrefs()
                     webState:web_state]),
      web_state_(web_state) {
  web_state->AddObserver(this);

  infobars::InfoBarManager* infobar_manager =
      InfoBarManagerImpl::FromWebState(web_state);
  DCHECK(infobar_manager);
  auto from_web_state_impl =
      [](web::WebState* web_state) -> autofill::AutofillClientIOS* {
    if (auto* ath = AutofillTabHelper::FromWebState(web_state)) {
      return ath->autofill_client();
    }
    return nullptr;
  };
  autofill_client_ = std::make_unique<autofill::ChromeAutofillClientIOS>(
      from_web_state_impl, profile_, web_state, infobar_manager,
      autofill_agent_);

  if (IsAutofillAcrossIframesEnabled()) {
    autofill::ChildFrameRegistrar::GetOrCreateForWebState(web_state)
        ->AddObserver(this);
  }
}

void AutofillTabHelper::WebStateDestroyed(web::WebState* web_state) {
  CHECK_EQ(web_state, web_state_);

  autofill_agent_ = nil;
  web_state->RemoveObserver(this);

  if (IsAutofillAcrossIframesEnabled()) {
    auto* registrar = autofill::ChildFrameRegistrar::FromWebState(web_state);
    CHECK(registrar);
    registrar->RemoveObserver(this);
  }
}

void AutofillTabHelper::OnDidDoubleRegistration(
    autofill::LocalFrameToken local) {
  // The frame corresponding to the |local| token attempted a double
  // registration using a potentially stolen remote token. It is likely a
  // spoofing attempt, so unregister the driver to isolate it, pulling it out of
  // the xframe hiearchy, to make sure it can't intercept sensitive information
  // through filling (e.g. fill credit card info) during xframe filling.
  auto* driver = autofill::AutofillDriverIOS::FromWebStateAndLocalFrameToken(
      web_state_, local);
  if (driver) {
    driver->Unregister();
  }
}
