// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_PUSH_NOTIFICATION_UI_BUNDLED_NOTIFICATIONS_OPT_IN_ITEM_IDENTIFIER_H_
#define IOS_CHROME_BROWSER_PUSH_NOTIFICATION_UI_BUNDLED_NOTIFICATIONS_OPT_IN_ITEM_IDENTIFIER_H_

// Enum representing the different types of notifications that can be opted in
// or out of in the settings page.
enum NotificationsOptInItemIdentifier {
  kContent,
  kTips,
  kPriceTracking,
  kSafetyCheck,
  kMaxValue = kSafetyCheck,
};

#endif  // IOS_CHROME_BROWSER_PUSH_NOTIFICATION_UI_BUNDLED_NOTIFICATIONS_OPT_IN_ITEM_IDENTIFIER_H_
