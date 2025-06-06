// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_INTEREST_GROUP_INTEREST_GROUP_FEATURES_H_
#define CONTENT_BROWSER_INTEREST_GROUP_INTEREST_GROUP_FEATURES_H_

#include "base/feature_list.h"
#include "base/metrics/field_trial_params.h"
#include "base/time/time.h"
#include "content/common/content_export.h"
#include "third_party/blink/public/common/features.h"

namespace features {
// Please keep features in alphabetical order.
CONTENT_EXPORT BASE_DECLARE_FEATURE(kAlwaysUpdateKAnon);

CONTENT_EXPORT BASE_DECLARE_FEATURE(kDetectInconsistentPageImpl);

CONTENT_EXPORT BASE_DECLARE_FEATURE(kEnableBandAClickiness);
CONTENT_EXPORT BASE_DECLARE_FEATURE(kEnableBandAKAnonEnforcement);
CONTENT_EXPORT BASE_DECLARE_FEATURE(kEnableBandAPrivateAggregation);
CONTENT_EXPORT BASE_DECLARE_FEATURE(kEnableBandASampleDebugReports);
CONTENT_EXPORT BASE_DECLARE_FEATURE(kEnableBandATriggeredUpdates);

CONTENT_EXPORT BASE_DECLARE_FEATURE(kFledgeBiddingAndAuctionNonceSupport);

CONTENT_EXPORT BASE_DECLARE_FEATURE(kFledgeCacheKAnonHashedKeys);
CONTENT_EXPORT BASE_DECLARE_FEATURE_PARAM(base::TimeDelta,
                                          kFledgeCacheKAnonHashedKeysTtl);

CONTENT_EXPORT BASE_DECLARE_FEATURE(kFledgeDoSampleDebugReportForTesting);

CONTENT_EXPORT BASE_DECLARE_FEATURE(kFledgeEnableUnNoisedRealTimeReport);

CONTENT_EXPORT BASE_DECLARE_FEATURE(kFledgeEnableUserAgentOverrides);
CONTENT_EXPORT BASE_DECLARE_FEATURE(kFledgeEnableWALForInterestGroupStorage);
CONTENT_EXPORT BASE_DECLARE_FEATURE(kFledgeFacilitatedTestingSignalsHeaders);
CONTENT_EXPORT BASE_DECLARE_FEATURE(
    kFledgeModifyInterestGroupPolicyCheckOnOwner);

CONTENT_EXPORT BASE_DECLARE_FEATURE(
    kFledgeOnlyUseIpAddressSpaceInClientSecurityState);

CONTENT_EXPORT BASE_DECLARE_FEATURE(kFledgeQueryKAnonymity);

CONTENT_EXPORT BASE_DECLARE_FEATURE(kFledgeSendDebugReportCooldownsToBandA);

CONTENT_EXPORT BASE_DECLARE_FEATURE(kFledgeStartAnticipatoryProcesses);
CONTENT_EXPORT BASE_DECLARE_FEATURE_PARAM(
    base::TimeDelta,
    kFledgeStartAnticipatoryProcessExpirationTime);

CONTENT_EXPORT BASE_DECLARE_FEATURE(kFledgeStoreBandAKeysInDB);

CONTENT_EXPORT BASE_DECLARE_FEATURE(kFledgeUseKVv2SignalsCache);

CONTENT_EXPORT BASE_DECLARE_FEATURE(kFledgeUseNonTransientNIKForSeller);

CONTENT_EXPORT BASE_DECLARE_FEATURE(kFledgeUsePreconnectCache);

}  // namespace features

#endif  // CONTENT_BROWSER_INTEREST_GROUP_INTEREST_GROUP_FEATURES_H_
