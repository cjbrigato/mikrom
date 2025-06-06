// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_PUSH_NOTIFICATION_MODEL_PUSH_NOTIFICATION_DELEGATE_H_
#define IOS_CHROME_BROWSER_PUSH_NOTIFICATION_MODEL_PUSH_NOTIFICATION_DELEGATE_H_

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <UserNotifications/UserNotifications.h>

@class AppState;
class ProfileIOS;

@interface PushNotificationDelegate
    : NSObject <UNUserNotificationCenterDelegate>

- (instancetype)initWithAppState:(AppState*)appState NS_DESIGNATED_INITIALIZER;

- (instancetype)init NS_UNAVAILABLE;

// Passes the contents of an incoming push notification to the appropriate
// `PushNotificationClient` for processing and logs the time it takes for the
// client to process the notification. Calls `completionHandler` with the result
// of the processing.
- (void)applicationWillProcessIncomingRemoteNotification:(NSDictionary*)userInfo
                                  fetchCompletionHandler:
                                      (void (^)(UIBackgroundFetchResult result))
                                          completionHandler;

// When the device successfully registers with APNS and receives its APNS device
// token this function aggregates all the necessary information and registers
// the device to the Push Notification server.
- (void)applicationDidRegisterWithAPNS:(NSData*)deviceToken
                               profile:(ProfileIOS*)profile;

@end

#endif  // IOS_CHROME_BROWSER_PUSH_NOTIFICATION_MODEL_PUSH_NOTIFICATION_DELEGATE_H_
