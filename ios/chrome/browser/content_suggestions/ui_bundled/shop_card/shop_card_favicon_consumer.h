// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_CONTENT_SUGGESTIONS_UI_BUNDLED_SHOP_CARD_SHOP_CARD_FAVICON_CONSUMER_H_
#define IOS_CHROME_BROWSER_CONTENT_SUGGESTIONS_UI_BUNDLED_SHOP_CARD_SHOP_CARD_FAVICON_CONSUMER_H_

@protocol ShopCardFaviconConsumer
- (void)faviconCompleted:(UIImage*)faviconImage;
@end

#endif  // IOS_CHROME_BROWSER_CONTENT_SUGGESTIONS_UI_BUNDLED_SHOP_CARD_SHOP_CARD_FAVICON_CONSUMER_H_
