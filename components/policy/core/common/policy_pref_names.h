// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_POLICY_CORE_COMMON_POLICY_PREF_NAMES_H_
#define COMPONENTS_POLICY_CORE_COMMON_POLICY_PREF_NAMES_H_

#include "build/build_config.h"
#include "components/policy/policy_export.h"

namespace policy {

// Possible values for Incognito mode availability. Please, do not change
// the order of entries since numeric values are exposed to users.
enum class IncognitoModeAvailability {
  // Incognito mode enabled. Users may open pages in both Incognito mode and
  // normal mode (usually the default behaviour).
  kEnabled = 0,
  // Incognito mode disabled. Users may not open pages in Incognito mode.
  // Only normal mode is available for browsing.
  kDisabled,
  // Incognito mode forced. Users may open pages *ONLY* in Incognito mode.
  // Normal mode is not available for browsing.
  kForced,

  kNumTypes
};

// The enum cocorresponding to the type of download restriction.
enum class DownloadRestriction {
  NONE = 0,
  DANGEROUS_FILES = 1,
  POTENTIALLY_DANGEROUS_FILES = 2,
  ALL_FILES = 3,
  // MALICIOUS_FILES has a stricter definition of harmful file than
  // DANGEROUS_FILES and does not block based on file extension.
  MALICIOUS_FILES = 4,
};

namespace policy_prefs {

#if BUILDFLAG(IS_WIN)
extern const char kAzureActiveDirectoryManagement[];
extern const char kEnterpriseMDMManagementWindows[];
#endif
extern const char kCloudManagementEnrollmentMandatory[];
extern const char kDlpClipboardCheckSizeLimit[];
extern const char kDlpReportingEnabled[];
extern const char kDlpRulesList[];
#if BUILDFLAG(IS_MAC)
extern const char kEnterpriseMDMManagementMac[];
#endif
extern const char kFeedbackSurveysEnabled[];
extern const char kLastPolicyStatisticsUpdate[];
extern const char kNativeWindowOcclusionEnabled[];
extern const char kSafeSitesFilterBehavior[];
extern const char kSystemFeaturesDisableList[];
extern const char kSystemFeaturesDisableMode[];
extern const char kUrlBlocklist[];
extern const char kUrlAllowlist[];
extern const char kUserPolicyRefreshRate[];
extern const char kIntensiveWakeUpThrottlingEnabled[];
#if BUILDFLAG(IS_ANDROID)
extern const char kBackForwardCacheEnabled[];
extern const char kReadAloudEnabled[];
#endif  // BUILDFLAG(IS_ANDROID)
#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
extern const char kLastPolicyCheckTime[];
#endif
#if BUILDFLAG(IS_IOS)
extern const char kUserPolicyNotificationWasShown[];
extern const char kSyncDisabledAlertShown[];
#endif
extern const char kForceGoogleSafeSearch[];
extern const char kForceYouTubeRestrict[];
extern const char kHideWebStoreIcon[];
extern const char kIncognitoModeAvailability[];
extern const char kStandardizedBrowserZoomEnabled[];
extern const char kPolicyTestPageEnabled[];
extern const char kHasDismissedPolicyPagePromotionBanner[];
extern const char kAllowBackForwardCacheForCacheControlNoStorePageEnabled[];
extern const char kLocalTestPoliciesForNextStartup[];
extern const char kCSSCustomStateDeprecatedSyntaxEnabled[];
extern const char kSelectParserRelaxationEnabled[];
extern const char kForcePermissionPolicyUnloadDefaultEnabled[];
extern const char kDownloadRestrictions[];
#if BUILDFLAG(IS_CHROMEOS)
extern const char kAlwaysOnVpnPreConnectUrlAllowlist[];
extern const char kFloatingWorkspaceEnabled[];
#endif
extern const char kBuiltInAIAPIsEnabled[];
#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_WIN) || \
    BUILDFLAG(IS_MAC)
extern const char kPasswordManagerBlocklist[];
#endif  // BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)
}  // namespace policy_prefs
}  // namespace policy

#endif  // COMPONENTS_POLICY_CORE_COMMON_POLICY_PREF_NAMES_H_
