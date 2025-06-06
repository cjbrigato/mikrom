// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/optimization_guide/core/feature_registry/feature_registration.h"

#include "base/feature_list.h"
#include "components/optimization_guide/core/feature_registry/enterprise_policy_registry.h"
#include "components/optimization_guide/core/feature_registry/mqls_feature_registry.h"
#include "components/optimization_guide/core/feature_registry/settings_ui_registry.h"
#include "components/optimization_guide/core/model_execution/feature_keys.h"
#include "components/optimization_guide/proto/features/common_quality_data.pb.h"
#include "components/optimization_guide/proto/features/tab_organization.pb.h"
#include "components/optimization_guide/proto/model_quality_service.pb.h"
#include "components/prefs/pref_registry_simple.h"
#include "enterprise_policy_registry.h"
#include "mqls_feature_registry.h"

namespace optimization_guide {

namespace prefs {

const char kTabOrganizationEnterprisePolicyAllowed[] =
    "optimization_guide.model_execution.tab_organization_enterprise_policy_"
    "allowed";

const char kComposeEnterprisePolicyAllowed[] =
    "optimization_guide.model_execution.compose_enterprise_policy_allowed";

const char kWallpaperSearchEnterprisePolicyAllowed[] =
    "optimization_guide.model_execution.wallpaper_search_enterprise_policy_"
    "allowed";

const char kHistorySearchEnterprisePolicyAllowed[] =
    "optimization_guide.model_execution.history_search_"
    "enterprise_policy_allowed";

const char kProductSpecificationsEnterprisePolicyAllowed[] =
    "optimization_guide.model_execution.tab_compare_settings_enterprise_policy";

const char kAutofillPredictionImprovementsEnterprisePolicyAllowed[] =
    "optimization_guide.model_execution.autofill_prediction_improvements_"
    "enterprise_policy_allowed";

const char kPasswordChangeSubmissionEnterprisePolicyAllowed[] =
    "optimization_guide.model_execution.password_change_submission_"
    "enterprise_policy_allowed";

const char kNotificationContentDetectionEnterprisePolicyAllowed[] =
    "optimization_guide.model_execution.notification_content_detection_"
    "enterprise_policy_allowed";
}  // namespace prefs

namespace features {
BASE_FEATURE(kComposeMqlsLogging,
             "ComposeMqlsLogging",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kTabOrganizationMqlsLogging,
             "TabOrganizationMqlsLogging",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kWallpaperSearchMqlsLogging,
             "WallpaperSearchMqlsLogging",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kHistorySearchMqlsLogging,
             "HistorySearchMqlsLogging",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kProductSpecificationsMqlsLogging,
             "ProductSpecificationsMqlsLogging",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kFormsClassificationsMqlsLogging,
             "FormsClassificationsMqlsLogging",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kPasswordChangeSubmissionMqlsLogging,
             "PasswordChangeSubmissionMqlsLogging",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kNotificationContentDetectionMqlsLogging,
             "NotificationContentDetectionMqlsLogging",
             base::FEATURE_DISABLED_BY_DEFAULT);

}  // namespace features

namespace {

// Helper function that creates a `UserFeedbackCallback` for unspecified
// feedback.
UserFeedbackCallback FeedbackUnspecified() {
  return base::BindRepeating([](proto::LogAiDataRequest&) {
    return proto::UserFeedback::USER_FEEDBACK_UNSPECIFIED;
  });
}

void RegisterCompose() {
  const char* kComposeName = "Compose";
  EnterprisePolicyPref enterprise_policy =
      EnterprisePolicyRegistry::GetInstance().Register(
          prefs::kComposeEnterprisePolicyAllowed);

  UserFeedbackCallback logging_callback =
      base::BindRepeating([](proto::LogAiDataRequest& request_proto) {
        return request_proto.compose().quality().user_feedback();
      });
  auto mqls_metadata = std::make_unique<MqlsFeatureMetadata>(
      kComposeName, proto::LogAiDataRequest::FeatureCase::kCompose,
      enterprise_policy, &features::kComposeMqlsLogging, logging_callback);
  MqlsFeatureRegistry::GetInstance().Register(std::move(mqls_metadata));

  auto ui_metadata = std::make_unique<SettingsUiMetadata>(
      kComposeName, UserVisibleFeatureKey::kCompose, enterprise_policy);
  SettingsUiRegistry::GetInstance().Register(std::move(ui_metadata));
}

void RegisterTabOrganization() {
  const char* kTabOrganizationName = "TabOrganization";
  EnterprisePolicyPref enterprise_policy =
      EnterprisePolicyRegistry::GetInstance().Register(
          prefs::kTabOrganizationEnterprisePolicyAllowed);

  UserFeedbackCallback logging_callback =
      base::BindRepeating([](proto::LogAiDataRequest& request_proto) {
        // If there is no tab organization, we don't have any user_feedback mark
        // it as unspecified.
        const proto::TabOrganizationQuality& quality =
            request_proto.tab_organization().quality();
        if (quality.organizations().empty()) {
          return proto::UserFeedback::USER_FEEDBACK_UNSPECIFIED;
        }
        if (quality.user_feedback()) {
          return quality.user_feedback();
        }
        // TODO(b/331852814): Remove this else case along with the multi tab
        // organization flag.
        return quality.organizations()[0].user_feedback();
      });
  auto mqls_metadata = std::make_unique<MqlsFeatureMetadata>(
      kTabOrganizationName,
      proto::LogAiDataRequest::FeatureCase::kTabOrganization, enterprise_policy,
      &features::kTabOrganizationMqlsLogging, logging_callback);
  MqlsFeatureRegistry::GetInstance().Register(std::move(mqls_metadata));

  auto ui_metadata = std::make_unique<SettingsUiMetadata>(
      kTabOrganizationName, UserVisibleFeatureKey::kTabOrganization,
      enterprise_policy);
  SettingsUiRegistry::GetInstance().Register(std::move(ui_metadata));
}

void RegisterWallpaperSearch() {
  const char* kWallpaperSearchName = "WallpaperSearch";
  EnterprisePolicyPref enterprise_policy =
      EnterprisePolicyRegistry::GetInstance().Register(
          prefs::kWallpaperSearchEnterprisePolicyAllowed);

  UserFeedbackCallback logging_callback =
      base::BindRepeating([](proto::LogAiDataRequest& request_proto) {
        return request_proto.wallpaper_search().quality().user_feedback();
      });
  auto mqls_metadata = std::make_unique<MqlsFeatureMetadata>(
      kWallpaperSearchName,
      proto::LogAiDataRequest::FeatureCase::kWallpaperSearch, enterprise_policy,
      &features::kWallpaperSearchMqlsLogging, logging_callback);
  MqlsFeatureRegistry::GetInstance().Register(std::move(mqls_metadata));

  auto ui_metadata = std::make_unique<SettingsUiMetadata>(
      kWallpaperSearchName, UserVisibleFeatureKey::kWallpaperSearch,
      enterprise_policy);
  SettingsUiRegistry::GetInstance().Register(std::move(ui_metadata));
}

void RegisterHistorySearch() {
  EnterprisePolicyPref enterprise_policy =
      EnterprisePolicyRegistry::GetInstance().Register(
          prefs::kHistorySearchEnterprisePolicyAllowed);

  UserFeedbackCallback logging_callback_query =
      base::BindRepeating([](proto::LogAiDataRequest& request_proto) {
        return request_proto.history_query().quality().user_feedback();
      });
  auto mqls_metadata_query = std::make_unique<MqlsFeatureMetadata>(
      "HistoryQuery", proto::LogAiDataRequest::FeatureCase::kHistoryQuery,
      enterprise_policy, &features::kHistorySearchMqlsLogging,
      logging_callback_query);
  MqlsFeatureRegistry::GetInstance().Register(std::move(mqls_metadata_query));

  auto mqls_metadata_answer = std::make_unique<MqlsFeatureMetadata>(
      "HistoryAnswer", proto::LogAiDataRequest::FeatureCase::kHistoryAnswer,
      enterprise_policy, &features::kHistorySearchMqlsLogging,
      FeedbackUnspecified());
  MqlsFeatureRegistry::GetInstance().Register(std::move(mqls_metadata_answer));

  auto ui_metadata = std::make_unique<SettingsUiMetadata>(
      "HistorySearch", UserVisibleFeatureKey::kHistorySearch,
      enterprise_policy);
  SettingsUiRegistry::GetInstance().Register(std::move(ui_metadata));
}

void RegisterPasswordChangeSubmission() {
  const char* kPasswordChangeSubmissionName = "PasswordChangeSubmission";
  EnterprisePolicyPref enterprise_policy =
      EnterprisePolicyRegistry::GetInstance().Register(
          prefs::kPasswordChangeSubmissionEnterprisePolicyAllowed);

  auto ui_metadata = std::make_unique<SettingsUiMetadata>(
      "PasswordChangeSubmission",
      UserVisibleFeatureKey::kPasswordChangeSubmission,
      std::move(enterprise_policy));
  SettingsUiRegistry::GetInstance().Register(std::move(ui_metadata));

  auto mqls_metadata = std::make_unique<MqlsFeatureMetadata>(
      kPasswordChangeSubmissionName,
      proto::LogAiDataRequest::FeatureCase::kPasswordChangeSubmission,
      enterprise_policy, &features::kPasswordChangeSubmissionMqlsLogging,
      FeedbackUnspecified());
  MqlsFeatureRegistry::GetInstance().Register(std::move(mqls_metadata));
}

void RegisterProductSpecifications() {
  EnterprisePolicyPref enterprise_policy =
      EnterprisePolicyRegistry::GetInstance().Register(
          prefs::kProductSpecificationsEnterprisePolicyAllowed);
  UserFeedbackCallback logging_callback =
      base::BindRepeating([](proto::LogAiDataRequest& request_proto) {
        return request_proto.product_specifications().quality().user_feedback();
      });
  auto metadata = std::make_unique<MqlsFeatureMetadata>(
      "ProductSpecifications",
      proto::LogAiDataRequest::FeatureCase::kProductSpecifications,
      enterprise_policy, &features::kProductSpecificationsMqlsLogging,
      logging_callback);
  MqlsFeatureRegistry::GetInstance().Register(std::move(metadata));
}

void RegisterAutofillPredictions() {
  MqlsFeatureRegistry::GetInstance().Register(
      std::make_unique<MqlsFeatureMetadata>(
          "FormsClassifications",
          proto::LogAiDataRequest::FeatureCase::kFormsClassifications,
          EnterprisePolicyRegistry::GetInstance().Register(
              prefs::kAutofillPredictionImprovementsEnterprisePolicyAllowed),
          &features::kFormsClassificationsMqlsLogging, FeedbackUnspecified()));
}

void RegisterNotificationContentDetection() {
  EnterprisePolicyPref enterprise_policy =
      EnterprisePolicyRegistry::GetInstance().Register(
          prefs::kNotificationContentDetectionEnterprisePolicyAllowed);
  UserFeedbackCallback logging_callback =
      base::BindRepeating([](proto::LogAiDataRequest& request_proto) {
        return request_proto.notification_content_detection()
            .quality()
            .user_feedback();
      });
  auto metadata = std::make_unique<MqlsFeatureMetadata>(
      "NotificationContentDetection",
      proto::LogAiDataRequest::FeatureCase::kNotificationContentDetection,
      enterprise_policy, &features::kNotificationContentDetectionMqlsLogging,
      logging_callback);
  MqlsFeatureRegistry::GetInstance().Register(std::move(metadata));
}

}  // anonymous namespace

void RegisterGenAiFeatures(PrefRegistrySimple* pref_registry) {
  static bool features_registered = false;
  // When adding a value here, also update:
  // - tools/metrics/histograms/metadata/optimization_guide/histogram.xml:
  // <variants name="LogAiDataRequestFeature">
  if (!features_registered) {
    // The registries are static and so should only be populated once for the
    // program (rather than once per profile).
    RegisterCompose();
    RegisterTabOrganization();
    RegisterWallpaperSearch();
    RegisterHistorySearch();
    RegisterProductSpecifications();
    RegisterAutofillPredictions();
    RegisterPasswordChangeSubmission();
    RegisterNotificationContentDetection();
    features_registered = true;
  }
  EnterprisePolicyRegistry::GetInstance().RegisterProfilePrefs(pref_registry);
}

}  // namespace optimization_guide
