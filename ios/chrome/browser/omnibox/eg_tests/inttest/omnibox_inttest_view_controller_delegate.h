// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_OMNIBOX_EG_TESTS_INTTEST_OMNIBOX_INTTEST_VIEW_CONTROLLER_DELEGATE_H_
#define IOS_CHROME_BROWSER_OMNIBOX_EG_TESTS_INTTEST_OMNIBOX_INTTEST_VIEW_CONTROLLER_DELEGATE_H_

#import <UIKit/UIKit.h>

@class OmniboxInttestViewController;

@protocol OmniboxInttestViewControllerDelegate <NSObject>

/// Called when the omnibox cancel button has been tapped.
- (void)viewControllerDidTapCancelButton:
    (OmniboxInttestViewController*)viewController;

@end

#endif  // IOS_CHROME_BROWSER_OMNIBOX_EG_TESTS_INTTEST_OMNIBOX_INTTEST_VIEW_CONTROLLER_DELEGATE_H_
