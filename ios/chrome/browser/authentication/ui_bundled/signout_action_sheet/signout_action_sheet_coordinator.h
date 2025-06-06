// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_AUTHENTICATION_UI_BUNDLED_SIGNOUT_ACTION_SHEET_SIGNOUT_ACTION_SHEET_COORDINATOR_H_
#define IOS_CHROME_BROWSER_AUTHENTICATION_UI_BUNDLED_SIGNOUT_ACTION_SHEET_SIGNOUT_ACTION_SHEET_COORDINATOR_H_

#import <UIKit/UIKit.h>

#import "ios/chrome/browser/shared/coordinator/alert/action_sheet_coordinator.h"
#import "ios/chrome/browser/signin/model/constants.h"

namespace signin_metrics {
enum class ProfileSignout;
}  // namespace signin_metrics

class Browser;
@class SignoutActionSheetCoordinator;

// Delegate that handles user interactions with sign-out action sheet.
@protocol SignoutActionSheetCoordinatorDelegate

// Called when the sign-out flow is in progress. The UI needs to be blocked
// until the sign-out flow is done.
- (void)signoutActionSheetCoordinatorPreventUserInteraction:
    (SignoutActionSheetCoordinator*)coordinator;

// Called when the sign-out flow is done. The UI can be unblocked.
- (void)signoutActionSheetCoordinatorAllowUserInteraction:
    (SignoutActionSheetCoordinator*)coordinator;

@end

// Signs out and displays a toast. The user must be signed-in to use this. The
// SignoutActionSheetCoordinatorDelegate is responsible for blocking and
// unblocking the UI while the sign-out flow is in progress.
@interface SignoutActionSheetCoordinator : ChromeCoordinator

- (instancetype)initWithBaseViewController:(UIViewController*)viewController
                                   browser:(Browser*)browser NS_UNAVAILABLE;

// Designated initializer.
- (instancetype)
    initWithBaseViewController:(UIViewController*)viewController
                       browser:(Browser*)browser
                          rect:(CGRect)rect
                          view:(UIView*)view
      forceSnackbarOverToolbar:(BOOL)forceSnackbarOverToolbar
                    withSource:(signin_metrics::ProfileSignout)source
                    completion:(signin_ui::SignoutCompletionCallback)completion
    NS_DESIGNATED_INITIALIZER;

// The delegate.
@property(nonatomic, weak) id<SignoutActionSheetCoordinatorDelegate> delegate;

// The title displayed for the sign-out alert.
@property(nonatomic, copy, readonly) NSString* title;

// The message displayed for the sign-out alert.
@property(nonatomic, copy, readonly) NSString* message;

// Whether to warns feature won’t be available anymore when user is not
// synced.
@property(nonatomic, assign) BOOL showUnavailableFeatureDialogHeader;

@end

#endif  // IOS_CHROME_BROWSER_AUTHENTICATION_UI_BUNDLED_SIGNOUT_ACTION_SHEET_SIGNOUT_ACTION_SHEET_COORDINATOR_H_
