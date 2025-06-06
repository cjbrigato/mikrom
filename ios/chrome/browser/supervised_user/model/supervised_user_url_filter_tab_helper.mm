// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/supervised_user/model/supervised_user_url_filter_tab_helper.h"

#import "base/functional/callback.h"
#import "base/memory/weak_ptr.h"
#import "base/strings/sys_string_conversions.h"
#import "components/supervised_user/core/browser/supervised_user_interstitial.h"
#import "components/supervised_user/core/browser/supervised_user_service.h"
#import "components/supervised_user/core/browser/supervised_user_url_filter.h"
#import "components/supervised_user/core/browser/supervised_user_utils.h"
#import "components/supervised_user/core/common/features.h"
#import "components/supervised_user/core/common/supervised_user_constants.h"
#import "ios/chrome/browser/shared/model/profile/profile_ios.h"
#import "ios/chrome/browser/supervised_user/model/supervised_user_capabilities.h"
#import "ios/chrome/browser/supervised_user/model/supervised_user_error.h"
#import "ios/chrome/browser/supervised_user/model/supervised_user_error_container.h"
#import "ios/chrome/browser/supervised_user/model/supervised_user_service_factory.h"
#import "ios/net/protocol_handler_util.h"
#import "net/base/apple/url_conversions.h"
#import "net/base/net_errors.h"
#import "url/gurl.h"

using PolicyDecision = web::WebStatePolicyDecider::PolicyDecision;

namespace {

void OnURLFilteringDone(
    base::WeakPtr<web::WebState> weak_web_state,
    web::WebStatePolicyDecider::RequestInfo request_info,
    web::WebStatePolicyDecider::PolicyDecisionCallback policy_decision_callback,
    supervised_user::SupervisedUserURLFilter::Result result) {
  // Allow navigation by default.
  PolicyDecision decision = PolicyDecision::Allow();
  web::WebState* web_state = weak_web_state.get();

  if (!web_state) {
    // Cancel the request if the corresponding `web_state` is destroyed.
    decision = PolicyDecision::Cancel();
  } else if (result.IsBlocked()) {
    SupervisedUserErrorContainer* container =
        SupervisedUserErrorContainer::FromWebState(web_state);
    CHECK(container);
    container->SetSupervisedUserErrorInfo(
        std::make_unique<SupervisedUserErrorContainer::SupervisedUserErrorInfo>(
            result.url, request_info.target_frame_is_main, result.reason));
    decision = CreateSupervisedUserInterstitialErrorDecision();
  }
  std::move(policy_decision_callback).Run(decision);
}

}  // namespace

SupervisedUserURLFilterTabHelper::~SupervisedUserURLFilterTabHelper() = default;

SupervisedUserURLFilterTabHelper::SupervisedUserURLFilterTabHelper(
    web::WebState* web_state)
    : web::WebStatePolicyDecider(web_state) {}

void SupervisedUserURLFilterTabHelper::ShouldAllowRequest(
    NSURLRequest* request,
    web::WebStatePolicyDecider::RequestInfo request_info,
    web::WebStatePolicyDecider::PolicyDecisionCallback callback) {
  ProfileIOS* profile =
      ProfileIOS::FromBrowserState(web_state()->GetBrowserState());

  // SupervisedUserService is not created for the off-the-record profile.
  if (profile->IsOffTheRecord()) {
    std::move(callback).Run(PolicyDecision::Allow());
    return;
  }

  if (!supervised_user::IsSubjectToParentalControls(profile)) {
    std::move(callback).Run(PolicyDecision::Allow());
    return;
  }

  supervised_user::SupervisedUserService* supervised_user_service =
      SupervisedUserServiceFactory::GetForProfile(profile);

  // Set up the callback taking filtering results, and perform URL filtering.
  GURL request_url = net::GURLWithNSURL(request.URL);
  supervised_user_service->GetURLFilter()->GetFilteringBehaviorWithAsyncChecks(
      request_url,
      base::BindOnce(&OnURLFilteringDone, web_state()->GetWeakPtr(),
                     request_info, std::move(callback)),
      /*skip_manual_parent_filter=*/false,
      supervised_user::FilteringContext::kNavigationThrottle,
      request_info.transition_type);
}
