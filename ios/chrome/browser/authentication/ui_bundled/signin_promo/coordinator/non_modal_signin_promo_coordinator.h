// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_AUTHENTICATION_UI_BUNDLED_SIGNIN_PROMO_COORDINATOR_NON_MODAL_SIGNIN_PROMO_COORDINATOR_H_
#define IOS_CHROME_BROWSER_AUTHENTICATION_UI_BUNDLED_SIGNIN_PROMO_COORDINATOR_NON_MODAL_SIGNIN_PROMO_COORDINATOR_H_

#import "ios/chrome/browser/authentication/ui_bundled/signin_promo/signin_promo_types.h"
#import "ios/chrome/browser/infobars/ui_bundled/coordinators/infobar_coordinator.h"

@class NonModalSignInPromoCoordinator;

// The delegate for the NonModalSignInPromoCoordinator.
@protocol NonModalSignInPromoCoordinatorDelegate

// Dismisses the non-modal sign-in promo.
- (void)dismissNonModalSignInPromo:(NonModalSignInPromoCoordinator*)coordinator;

@end

// Coordinator managing the non modal sign in promo.
@interface NonModalSignInPromoCoordinator : InfobarCoordinator

// The delegate to stop this coordinator.
@property(nonatomic, weak) id<NonModalSignInPromoCoordinatorDelegate> delegate;

// Creates a coordinator that uses `viewController`,`browser` and `promoType`.
- (instancetype)initWithBaseViewController:(UIViewController*)viewController
                                   browser:(Browser*)browser
                                 promoType:(SignInPromoType)promoType
    NS_DESIGNATED_INITIALIZER;

- (instancetype)initWithBaseViewController:(UIViewController*)viewController
                                   browser:(Browser*)browser
                                      type:(InfobarType)infobarType
    NS_UNAVAILABLE;

@end

#endif  // IOS_CHROME_BROWSER_AUTHENTICATION_UI_BUNDLED_SIGNIN_PROMO_COORDINATOR_NON_MODAL_SIGNIN_PROMO_COORDINATOR_H_
