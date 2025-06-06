// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_CONTENT_SUGGESTIONS_UI_BUNDLED_TAB_RESUMPTION_TAB_RESUMPTION_COMMANDS_H_
#define IOS_CHROME_BROWSER_CONTENT_SUGGESTIONS_UI_BUNDLED_TAB_RESUMPTION_TAB_RESUMPTION_COMMANDS_H_

@class TabResumptionItem;

// Command protocol for events for the Tab Resumption module.
@protocol TabResumptionCommands

// Opens the displayed tab resumption item.
- (void)openTabResumptionItem:(TabResumptionItem*)item;

// Track the URL correspodning to the ShopCard.
- (void)trackShopCardItem:(TabResumptionItem*)item;

@end

#endif  // IOS_CHROME_BROWSER_CONTENT_SUGGESTIONS_UI_BUNDLED_TAB_RESUMPTION_TAB_RESUMPTION_COMMANDS_H_
