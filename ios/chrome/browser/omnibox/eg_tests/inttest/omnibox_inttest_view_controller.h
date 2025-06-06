// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_OMNIBOX_EG_TESTS_INTTEST_OMNIBOX_INTTEST_VIEW_CONTROLLER_H_
#define IOS_CHROME_BROWSER_OMNIBOX_EG_TESTS_INTTEST_OMNIBOX_INTTEST_VIEW_CONTROLLER_H_

#import "ios/chrome/browser/omnibox/ui/popup/omnibox_popup_presenter.h"

@protocol OmniboxInttestViewControllerDelegate;
@protocol TextFieldViewContaining;

/// ViewController presenting Omnibox UI elements (OmniboxView, OmniboxPopupView
/// and Cancel button) for integration tests.
@interface OmniboxInttestViewController
    : UIViewController <OmniboxPopupPresenterDelegate>

/// Delegate for this class.
@property(nonatomic, weak) id<OmniboxInttestViewControllerDelegate> delegate;

/// Sets the omnibox edit view.
- (void)setEditView:(UIView<TextFieldViewContaining>*)editView;

@end

#endif  // IOS_CHROME_BROWSER_OMNIBOX_EG_TESTS_INTTEST_OMNIBOX_INTTEST_VIEW_CONTROLLER_H_
