// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/settings/ui_bundled/password/create_password_manager_title_view.h"

#import <Foundation/Foundation.h>

#import "ios/chrome/browser/shared/ui/symbols/symbols.h"
#import "ios/chrome/common/ui/elements/branded_navigation_item_title_view.h"
#import "ios/chrome/grit/ios_strings.h"
#import "ios/public/provider/chrome/browser/font/font_api.h"
#import "ui/base/l10n/l10n_util_mac.h"

namespace password_manager {
namespace {

// Point size of the SF Symbol used for the logo.
const CGFloat kSymbolPointSize = 17.0;

// Horizontal spacing between the logo and the title label.
const CGFloat kHorizontalSpacing = 9.0;

}  // namespace

BrandedNavigationItemTitleView* CreatePasswordManagerTitleView(
    NSString* title) {
  BrandedNavigationItemTitleView* title_view =
      [[BrandedNavigationItemTitleView alloc]
          initWithFont:ios::provider::GetBrandedProductRegularFont(
                           UIFont.labelFontSize)];
  title_view.title = title;
  title_view.imageLogo = MakeSymbolMulticolor(
      CustomSymbolWithPointSize(kPasswordManagerSymbol, kSymbolPointSize));
  title_view.accessibilityLabel = l10n_util::GetNSString(
      IDS_IOS_PASSWORD_MANAGER_TITLE_VIEW_ACCESSIBILITY_LABEL);
  title_view.titleLogoSpacing = kHorizontalSpacing;
  return title_view;
}

}  // namespace password_manager
