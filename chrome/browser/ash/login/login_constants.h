// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_LOGIN_LOGIN_CONSTANTS_H_
#define CHROME_BROWSER_ASH_LOGIN_LOGIN_CONSTANTS_H_

#include "base/time/time.h"

namespace ash {
namespace constants {

// This constant value comes from the policy definitions of the offline signin
// limiter policies. The value -1 means that online authentication will not be
// enforced by `OfflineSigninLimiter` so the user will be allowed to use offline
// authentication until a different reason than this policy enforces an online
// login.
inline constexpr int kOfflineSigninTimeLimitNotSet = -1;

// The value -2 means match the offline sign in time limit of the login screen,
// so policies GaiaLockScreenOfflineSigninTimeLimitDays would get the same value
// as GaiaOfflineSigninTimeLimitDays and
// SamlLockScreenOfflineSigninTimeLimitDays would get the same value as
// SAMLOfflineSigninTimeLimit
inline constexpr int kLockScreenOfflineSigninTimeLimitDaysMatchLogin = -2;

inline constexpr int kDefaultGaiaOfflineSigninTimeLimitDays =
    kOfflineSigninTimeLimitNotSet;
inline constexpr int kDefaultSAMLOfflineSigninTimeLimit =
    base::Days(14).InSeconds();

inline constexpr int kDefaultGaiaLockScreenOfflineSigninTimeLimitDays =
    kOfflineSigninTimeLimitNotSet;
inline constexpr int kDefaultSamlLockScreenOfflineSigninTimeLimitDays =
    kOfflineSigninTimeLimitNotSet;

// In-session password-change feature (includes password expiry notifications).
inline constexpr bool kDefaultSamlInSessionPasswordChangeEnabled = false;
inline constexpr int kDefaultSamlPasswordExpirationAdvanceWarningDays = 14;

// Online reauthentication on the lock screen.
inline constexpr bool kDefaultLockScreenReauthenticationEnabled = false;

// Default value for authentication flow automatic reloading controlled by
// `DeviceAuthenticationFlowAutoReloadInterval` policy
// A value of zero indicates the policy being disabled (no auto reload is
// triggered). While a value greater than zero indicates automatically reloading
// the authentication flow by this interval specified in minutes.
inline constexpr int kDefaultAuthenticationFlowAutoReloadInterval = 0;

}  // namespace constants
}  // namespace ash

#endif  // CHROME_BROWSER_ASH_LOGIN_LOGIN_CONSTANTS_H_
