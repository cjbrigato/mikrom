// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_SHARED_PUBLIC_COMMANDS_PROMOS_MANAGER_COMMANDS_H_
#define IOS_CHROME_BROWSER_SHARED_PUBLIC_COMMANDS_PROMOS_MANAGER_COMMANDS_H_

#import <StoreKit/StoreKit.h>

@class ShowSigninCommand;

// Commands to show app-wide promos.
@protocol PromosManagerCommands <NSObject>

// Displays an eligible promo (selected by the Promos Manager) if one exists.
- (void)showPromo;

// Makes a request to Apple to present the user the App Store Rating Promo.
- (void)showAppStoreReviewPrompt;

// Asks the presenter to display the signin UI configured by `command`.
- (void)showSignin:(ShowSigninCommand*)command;

// Display WhatsNew as a promo.
- (void)showWhatsNewPromo;

// Display default browser promo.
- (void)showDefaultBrowserPromo;

// Shows the default browser promo after the user tapped Remind Me Later.
- (void)showDefaultBrowserPromoAfterRemindMeLater;

// Shows the sign-in fullscreen promo.
- (void)showSigninPromo;

// Shows BWG promo.
- (void)showBWGPromo;

@end

#endif  // IOS_CHROME_BROWSER_SHARED_PUBLIC_COMMANDS_PROMOS_MANAGER_COMMANDS_H_
