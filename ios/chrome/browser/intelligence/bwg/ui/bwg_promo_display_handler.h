// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_INTELLIGENCE_BWG_UI_BWG_PROMO_DISPLAY_HANDLER_H_
#define IOS_CHROME_BROWSER_INTELLIGENCE_BWG_UI_BWG_PROMO_DISPLAY_HANDLER_H_

#import "ios/chrome/browser/promos_manager/ui_bundled/standard_promo_display_handler.h"

// Handler for displaying the BWG promo.
//
// This handler is called by the Promos Manager and presents the BWG
// promo to eligible users.
@interface BWGPromoDisplayHandler : NSObject <StandardPromoDisplayHandler>

// PromosManagerCommands handler.
@property(nonatomic, weak) id<PromosManagerCommands> handler;

@end

#endif  // IOS_CHROME_BROWSER_INTELLIGENCE_BWG_UI_BWG_PROMO_DISPLAY_HANDLER_H_
