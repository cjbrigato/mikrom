// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/credential_provider/model/features.h"

BASE_FEATURE(kCredentialProviderAutomaticPasskeyUpgrade,
             "CredentialProviderAutomaticPasskeyUpgrade",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kCredentialProviderPasskeyPRF,
             "CredentialProviderPasskeyPRF",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kCredentialProviderPerformanceImprovements,
             "CredentialProviderPerformanceImprovements",
             base::FEATURE_DISABLED_BY_DEFAULT);

bool IsCPEPerformanceImprovementsEnabled() {
  return base::FeatureList::IsEnabled(
      kCredentialProviderPerformanceImprovements);
}
