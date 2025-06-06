// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_AUTHENTICATION_UI_BUNDLED_SIGNIN_CONSISTENCY_PROMO_SIGNIN_CONSISTENCY_DEFAULT_ACCOUNT_CONSISTENCY_DEFAULT_ACCOUNT_MEDIATOR_H_
#define IOS_CHROME_BROWSER_AUTHENTICATION_UI_BUNDLED_SIGNIN_CONSISTENCY_PROMO_SIGNIN_CONSISTENCY_DEFAULT_ACCOUNT_CONSISTENCY_DEFAULT_ACCOUNT_MEDIATOR_H_

#import <Foundation/Foundation.h>

namespace signin {
class IdentityManager;
}  // namespace signin

namespace signin_metrics {
enum class AccessPoint;
}  // namespace signin_metrics

namespace syncer {
class SyncService;
}  // namespace syncer

class ChromeAccountManagerService;
@class ConsistencyDefaultAccountMediator;
@protocol ConsistencyDefaultAccountConsumer;
@protocol SystemIdentity;
enum class SigninContextStyle;

// Mediator for ConsistencyDefaultAccountCoordinator.
@interface ConsistencyDefaultAccountMediator : NSObject

// The designated initializer.
- (instancetype)
    initWithIdentityManager:(signin::IdentityManager*)identityManager
      accountManagerService:(ChromeAccountManagerService*)accountManagerService
                syncService:(syncer::SyncService*)syncService
               contextStyle:(SigninContextStyle)contextStyle
                accessPoint:(signin_metrics::AccessPoint)accessPoint
    NS_DESIGNATED_INITIALIZER;

- (instancetype)init NS_UNAVAILABLE;

@property(nonatomic, strong) id<ConsistencyDefaultAccountConsumer> consumer;
// Identity presented to the user.
@property(nonatomic, strong) id<SystemIdentity> selectedIdentity;

// Disconnect the mediator.
- (void)disconnect;

@end

#endif  // IOS_CHROME_BROWSER_AUTHENTICATION_UI_BUNDLED_SIGNIN_CONSISTENCY_PROMO_SIGNIN_CONSISTENCY_DEFAULT_ACCOUNT_CONSISTENCY_DEFAULT_ACCOUNT_MEDIATOR_H_
