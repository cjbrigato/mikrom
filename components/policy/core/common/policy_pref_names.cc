// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/policy/core/common/policy_pref_names.h"

#include "build/build_config.h"
#include "policy_pref_names.h"

namespace policy {
namespace policy_prefs {

#if BUILDFLAG(IS_WIN)
// Integer pref that stores Azure Active Directory management authority.
const char kAzureActiveDirectoryManagement[] =
    "management.platform.azure_active_directory";

// Integer pref that stores the Windows enterprise MDM management authority.
const char kEnterpriseMDMManagementWindows[] =
    "management.platform.enterprise_mdm_win";
#elif BUILDFLAG(IS_MAC)
// Integer pref that stores the Mac enterprise MDM management authority.
const char kEnterpriseMDMManagementMac[] =
    "management.platform.enterprise_mdm_mac";
#endif

// Enterprise policy controlled value representing whether the user may be shown
// HaTS surveys.
const char kFeedbackSurveysEnabled[] = "policy.feedback_surveys_enabled";

// 64-bit serialization of the time last policy usage statistics were collected
// by UMA_HISTOGRAM_ENUMERATION.
const char kLastPolicyStatisticsUpdate[] = "policy.last_statistics_update";

// Enum specifying if/how the SafeSites content filter should be applied.
// See the SafeSitesFilterBehavior policy for details.
const char kSafeSitesFilterBehavior[] = "policy.safe_sites_filter_behavior";

// A list of system features to be disabled (see policy
// "SystemFeaturesDisableList").
const char kSystemFeaturesDisableList[] = "policy.system_features_disable_list";

// Enum specifying the user experience of disabled features.
// See the SystemFeaturesDisableMode policy for details.
const char kSystemFeaturesDisableMode[] = "policy.system_features_disable_mode";

// Blocks access to the listed host patterns.
const char kUrlBlocklist[] = "policy.url_blocklist";

// Allows access to the listed host patterns, as exceptions to the blacklist.
const char kUrlAllowlist[] = "policy.url_allowlist";

// Integer that specifies the policy refresh rate for user-policy in
// milliseconds. Not all values are meaningful, so it is clamped to a sane range
// by the cloud policy subsystem.
const char kUserPolicyRefreshRate[] = "policy.user_refresh_rate";

// Boolean indicates whether the cloud management enrollment is mandatory or
// not.
const char kCloudManagementEnrollmentMandatory[] =
    "policy.cloud_management_enrollment_mandatory";

// Integer that sets the minimal limit on the data size in the clipboard to be
// checked against Data Leak Prevention rules.
const char kDlpClipboardCheckSizeLimit[] =
    "policy.dlp_clipboard_check_size_limit";

// Boolean policy preference to enable reporting of data leak prevention events.
const char kDlpReportingEnabled[] = "policy.dlp_reporting_enabled";

// A list of Data leak prevention rules.
const char kDlpRulesList[] = "policy.dlp_rules_list";

// A boolean value that can be used to disable native window occlusion
// calculation, even if the Finch feature is enabled.
const char kNativeWindowOcclusionEnabled[] =
    "policy.native_window_occlusion_enabled";

// Boolean policy preference for force enabling or disabling the
// IntensiveWakeUpThrottling web feature. Only applied if the policy is managed.
const char kIntensiveWakeUpThrottlingEnabled[] =
    "policy.intensive_wake_up_throttling_enabled";

#if BUILDFLAG(IS_ANDROID)
// Boolean policy preference to disable the BackForwardCache feature.
const char kBackForwardCacheEnabled[] = "policy.back_forward_cache_enabled";

// Boolean policy preference to disable the Read Aloud feature.
const char kReadAloudEnabled[] = "policy.read_aloud_enabled";
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
// Last time that a check for cloud policy management was done. This time is
// recorded on Android and iOS so that retries aren't attempted on every
// startup. Instead the cloud policy registration is retried at least 1 or 3
// days later.
const char kLastPolicyCheckTime[] = "policy.last_policy_check_time";
#endif

#if BUILDFLAG(IS_IOS)
const char kUserPolicyNotificationWasShown[] =
    "policy.user_policy_notification_was_shown";

// A bool for storing whether the user has seen the sync disabled alert since
// sync was disabled.
const char kSyncDisabledAlertShown[] = "sync.disabled_alert_shown";
#endif

// Boolean controlling whether SafeSearch is mandatory for Google Web Searches.
const char kForceGoogleSafeSearch[] = "settings.force_google_safesearch";

// Integer controlling whether Restrict Mode (moderate/strict) is mandatory on
// YouTube. See |safe_search_api::YouTubeRestrictMode| for possible values.
const char kForceYouTubeRestrict[] = "settings.force_youtube_restrict";

// A boolean pref set to true if the Chrome Web Store icons should be hidden
// from the New Tab Page and app launcher.
const char kHideWebStoreIcon[] = "hide_web_store_icon";

// Enum that specifies whether Incognito mode is:
// 0 - Enabled. Default behaviour. Default mode is available on demand.
// 1 - Disabled. User cannot browse pages in Incognito mode.
// 2 - Forced. All pages/sessions are forced into Incognito.
const char kIncognitoModeAvailability[] = "incognito.mode_availability";

// Enables the newly-specified behavior of the CSS "zoom" property.
const char kStandardizedBrowserZoomEnabled[] =
    "policy.standardized_browser_zoom_enabled";

// Boolean indicating whether Policy Test Page is Enabled.
// The value is controlled by the PolicyTestPageEnabled policy.
// If this is set to True, the page will be accessible.
const char kPolicyTestPageEnabled[] = "policy_test_page_enabled";

// Boolean indicating if the user has permanently dismissed the promotion
// banner on the chrome://policy page. If it's true, it means the user
// has clicked the "dismiss" button and has the banner turned off, if the
// value is false, the user has taken no action to turn off the banner.
const char kHasDismissedPolicyPagePromotionBanner[] =
    "has_dismissed_policy_page_promotion_banner";

// A boolean pref indicating whether the new the page with "Cache-Control:
// no-store" header is allowed to be stored in back/forward cache.
const char kAllowBackForwardCacheForCacheControlNoStorePageEnabled[] =
    "policy.allow_back_forward_cache_for_cache_control_no_store_page_enabled";

const char kLocalTestPoliciesForNextStartup[] =
    "local_test_policies_for_next_startup";

// Enables the deprecated :--foo syntax of CSS custom state. The :--foo syntax
// was deprecated and replaced by :state(foo).
const char kCSSCustomStateDeprecatedSyntaxEnabled[] =
    "policy.css_custom_state_deprecated_syntax_enabled";

// A boolean pref indicating whether the new HTML parser for the <select>
// element is enabled. When enabled, the HTML parser allows more tags to be used
// inside <select> instead of removing them.
const char kSelectParserRelaxationEnabled[] =
    "policy.select_parser_relaxation_enabled";

// A boolean pref indicating whether to allow deprecation of the "unload"
// event.
// If false, the deprecation rollout will be ignored.
const char kForcePermissionPolicyUnloadDefaultEnabled[] =
    "policy.force_permission_policy_unload_default_enabled";

// Prevents certain types of downloads based on integer value, which corresponds
// to policy::DownloadRestriction.
// 0 - No special restrictions (default)
// 1 - Block dangerous downloads
// 2 - Block potentially dangerous downloads
// 3 - Block all downloads
// 4 - Block malicious downloads
const char kDownloadRestrictions[] = "download_restrictions";

#if BUILDFLAG(IS_CHROMEOS)
// Allows user browser navigation access to the listed host patterns. Only
// applied when a AlwaysOn VPN is active but not connected.
const char kAlwaysOnVpnPreConnectUrlAllowlist[] =
    "policy.alwayson_vpn_pre_connect_url_allowlist";

// Boolean value for the FloatingWorkspaceEnabled policy
const char kFloatingWorkspaceEnabled[] = "ash.floating_workspace_enabled";
#endif

// A boolean value indicating whether the built-in AI APIs are enabled.
const char kBuiltInAIAPIsEnabled[] = "policy.built_in_ai_apis_enabled";
#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_WIN) || \
    BUILDFLAG(IS_MAC)
// List of urls for which password manager is disabled/blocked.
const char kPasswordManagerBlocklist[] = "policy.password_manager_blocklist";
#endif  // BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_WIN) ||
        // BUILDFLAG(IS_MAC)
}  // namespace policy_prefs
}  // namespace policy
