// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PAYMENTS_CORE_FEATURES_H_
#define COMPONENTS_PAYMENTS_CORE_FEATURES_H_

#include "base/feature_list.h"

namespace payments {
namespace features {

// Master toggle for all experimental features that will ship in the next
// release.
BASE_DECLARE_FEATURE(kWebPaymentsExperimentalFeatures);

// Used to control whether the Payment Sheet can be skipped for Payment Requests
// with a single URL based payment app and no other info requested.
BASE_DECLARE_FEATURE(kWebPaymentsSingleAppUiSkip);

// Used to control whether the invoking TWA can handle payments for app store
// payment method identifiers.
BASE_DECLARE_FEATURE(kAppStoreBilling);

// Used to control whether to remove the restriction that TWA has to be
// installed from specific app stores.
BASE_DECLARE_FEATURE(kAppStoreBillingDebug);

// Used to control whether allow crawling just-in-time installable payment app.
BASE_DECLARE_FEATURE(kWebPaymentsJustInTimePaymentApp);

// Used to test icon refetch for JIT installed apps with missing icons.
BASE_DECLARE_FEATURE(kAllowJITInstallationWhenAppIconIsMissing);

// Used to reject the apps with partial delegation.
BASE_DECLARE_FEATURE(kEnforceFullDelegation);

// If enabled, the GooglePayPaymentApp handles communications between the native
// GPay app and the browser for dynamic updates on shipping and payment data.
BASE_DECLARE_FEATURE(kGPayAppDynamicUpdate);

// Used to control whether SecurePaymentConfirmation is able to rely on OS-level
// credential store APIs, or if it can only rely on the user-profile database.
BASE_DECLARE_FEATURE(kSecurePaymentConfirmationUseCredentialStoreAPIs);

// Used to enable the refreshed fallback flow for Secure Payment Confirmation.
BASE_DECLARE_FEATURE(kSecurePaymentConfirmationFallback);

}  // namespace features
}  // namespace payments

#endif  // COMPONENTS_PAYMENTS_CORE_FEATURES_H_
