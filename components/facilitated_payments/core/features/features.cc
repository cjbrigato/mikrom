// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/facilitated_payments/core/features/features.h"

namespace payments::facilitated {

// When enabled, Chrome will offer to pay with accounts supporting Pix to users
// using their devices in landscape mode. Chrome always offers to pay with Pix
// accounts for users using their devices in portrait mode.
BASE_FEATURE(kEnablePixPaymentsInLandscapeMode,
             "EnablePixPaymentsInLandscapeMode",
             base::FEATURE_DISABLED_BY_DEFAULT);

#if BUILDFLAG(IS_ANDROID)
// When enabled, the check for matching the main frame domain with the
// allowlisted domains will be disabled.
BASE_FEATURE(kDisableFacilitatedPaymentsMerchantAllowlist,
             "DisableFacilitatedPaymentsMerchantAllowlist",
             base::FEATURE_DISABLED_BY_DEFAULT);

// When enabled, Chrome will prompt users without linked Pix accounts to link
// their Pix accounts to Google Wallet.
BASE_FEATURE(kEnablePixAccountLinking,
             "EnablePixAccountLinking",
             base::FEATURE_DISABLED_BY_DEFAULT);

// When enabled, Chrome will offer to pay with eWallet accounts if a payment
// link is detected.
BASE_FEATURE(kEwalletPayments,
             "EwalletPayments",
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif  // BUILDFLAG(IS_ANDROID)

// When enabled, Pix will be able to send multiple server request at a time.
BASE_FEATURE(kSupportMultipleServerRequestsForPixPayments,
             "SupportMultipleServerRequestsForPixPayments",
             base::FEATURE_DISABLED_BY_DEFAULT);

}  // namespace payments::facilitated
