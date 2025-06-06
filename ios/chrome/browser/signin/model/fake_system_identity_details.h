// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_SIGNIN_MODEL_FAKE_SYSTEM_IDENTITY_DETAILS_H_
#define IOS_CHROME_BROWSER_SIGNIN_MODEL_FAKE_SYSTEM_IDENTITY_DETAILS_H_

#import <UIKit/UIKit.h>

#import <string>

#import "base/containers/flat_map.h"
#import "base/functional/callback_forward.h"
#import "components/signin/public/identity_manager/account_capabilities.h"
#import "components/signin/public/identity_manager/account_capabilities_test_mutator.h"
#import "ios/chrome/browser/signin/model/capabilities_types.h"
#import "ios/chrome/browser/signin/model/refresh_access_token_error.h"
#import "ios/chrome/browser/signin/model/system_identity_manager.h"

@class FakeRefreshAccessTokenError;
@class FakeSystemIdentity;

using FakeSystemIdentityCapabilitiesMap = base::flat_map<std::string, bool>;

using GetAccessTokenCallback =
    base::RepeatingCallback<id<RefreshAccessTokenError>(
        SystemIdentityManager::AccessTokenCallback)>;

// Helper object used by FakeSystemIdentityManager to attach state to
// a SystemIdentity object via an association.
@interface FakeSystemIdentityDetails : NSObject

// The identity.
@property(nonatomic, readonly, strong) FakeSystemIdentity* fakeIdentity;

// The capabilities for the associated SystemIdentity.
@property(nonatomic, readonly)
    const FakeSystemIdentityCapabilitiesMap& visibleCapabilities;

// The avatar cached for the associated SystemIdentity. May be nil.
@property(nonatomic, strong) UIImage* cachedAvatar;

// If non-nil, fetching access token for the associated SystemIdentity
// will be considered as failing, and the `error` value will be passed
// to the observers.
@property(nonatomic, strong) FakeRefreshAccessTokenError* error;

@property(nonatomic) GetAccessTokenCallback getAccessTokenCallback;

// Allows callers to modify internal capability state mappings for tests.
@property(nonatomic, readonly)
    AccountCapabilitiesTestMutator* pendingCapabilitiesMutator;

// Designated initializer.
- (instancetype)initWithFakeIdentity:(FakeSystemIdentity*)fakeIdentity
    NS_DESIGNATED_INITIALIZER;

- (instancetype)init NS_UNAVAILABLE;

// Updates the visible capabilities with the changes made to the internal
// state through the `pendingCapabilitiesMutator`. This simulates Chrome
// fetching capabilities from the server and copying them to account services.
- (void)updateVisibleCapabilities;

@end

#endif  // IOS_CHROME_BROWSER_SIGNIN_MODEL_FAKE_SYSTEM_IDENTITY_DETAILS_H_
