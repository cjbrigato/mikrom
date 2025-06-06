// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_SETTINGS_UI_BUNDLED_ADDRESS_BAR_PREFERENCE_ADDRESS_BAR_PREFERENCE_COORDINATOR_H_
#define IOS_CHROME_BROWSER_SETTINGS_UI_BUNDLED_ADDRESS_BAR_PREFERENCE_ADDRESS_BAR_PREFERENCE_COORDINATOR_H_

#import "ios/chrome/browser/shared/coordinator/chrome_coordinator/chrome_coordinator.h"

@class AddressBarPreferenceCoordinator;
class Browser;

@protocol AddressBarPreferenceCoordinatorDelegate

// Called when the view controller is removed from navigation controller.
- (void)addressBarPreferenceCoordinatorViewControllerWasRemoved:
    (AddressBarPreferenceCoordinator*)coordinator;

@end

// This class is the coordinator for the address bar setting.
@interface AddressBarPreferenceCoordinator : ChromeCoordinator

@property(nonatomic, weak) id<AddressBarPreferenceCoordinatorDelegate> delegate;

// Designated initializer.
- (instancetype)initWithBaseNavigationController:
                    (UINavigationController*)navigationController
                                         browser:(Browser*)browser
    NS_DESIGNATED_INITIALIZER;

- (instancetype)initWithBaseViewController:(UIViewController*)viewController
                                   browser:(Browser*)browser NS_UNAVAILABLE;

@end

#endif  // IOS_CHROME_BROWSER_SETTINGS_UI_BUNDLED_ADDRESS_BAR_PREFERENCE_ADDRESS_BAR_PREFERENCE_COORDINATOR_H_
