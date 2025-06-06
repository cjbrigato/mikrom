// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/updater/policy/service.h"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/json/json_string_value_serializer.h"
#include "base/memory/ref_counted.h"
#include "base/process/launch.h"
#include "base/strings/stringprintf.h"
#include "base/test/task_environment.h"
#include "base/test/test_timeouts.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/enterprise_companion/global_constants.h"
#include "chrome/updater/external_constants.h"
#include "chrome/updater/external_constants_builder.h"
#include "chrome/updater/external_constants_override.h"
#include "chrome/updater/policy/dm_policy_manager.h"
#include "chrome/updater/policy/manager.h"
#include "chrome/updater/policy/platform_policy_manager.h"
#include "chrome/updater/protos/omaha_settings.pb.h"
#include "chrome/updater/test/integration_tests_impl.h"
#include "chrome/updater/test/test_scope.h"
#include "chrome/updater/test/unit_test_util.h"
#include "chrome/updater/updater_branding.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(IS_WIN)
#include "base/test/test_reg_util_win.h"
#include "chrome/updater/util/win_util.h"
#include "chrome/updater/win/win_constants.h"
#elif BUILDFLAG(IS_MAC)
#include "chrome/updater/util/mac_util.h"
#endif

namespace updater {

namespace {

#if BUILDFLAG(IS_WIN)
constexpr char kGlobalPolicyKey[] = "";
#else
constexpr char kGlobalPolicyKey[] = "global";
#endif

}  // namespace

using PolicyManagers = std::vector<scoped_refptr<PolicyManagerInterface>>;

void PolicyService::SetManagersForTesting(updater::PolicyManagers managers) {
  policy_managers_.SetManagersForTesting(std::move(managers));
}

// The Policy Manager Interface is implemented by policy managers such as Group
// Policy and Device Management.
class FakePolicyManager : public PolicyManagerInterface {
 public:
  FakePolicyManager(bool has_active_device_policies, const std::string& source)
      : has_active_device_policies_(has_active_device_policies),
        source_(source) {}

  std::string source() const override { return source_; }

  bool HasActiveDevicePolicies() const override {
    return has_active_device_policies_;
  }

  void SetCloudPolicyOverridesPlatformPolicy(
      bool cloud_policy_overrides_platform_policy) {
    cloud_policy_overrides_platform_policy_ =
        cloud_policy_overrides_platform_policy;
  }

  std::optional<bool> CloudPolicyOverridesPlatformPolicy() const override {
    return cloud_policy_overrides_platform_policy_;
  }

  std::optional<base::TimeDelta> GetLastCheckPeriod() const override {
    return std::nullopt;
  }

  std::optional<UpdatesSuppressedTimes> GetUpdatesSuppressedTimes()
      const override {
    if (!suppressed_times_.valid()) {
      return std::nullopt;
    }

    return suppressed_times_;
  }

  void SetUpdatesSuppressedTimes(
      const UpdatesSuppressedTimes& suppressed_times) {
    suppressed_times_ = suppressed_times;
  }

  std::optional<std::string> GetDownloadPreference() const override {
    return download_preference_.empty()
               ? std::nullopt
               : std::make_optional(download_preference_);
  }

  void SetDownloadPreference(const std::string& preference) {
    download_preference_ = preference;
  }

  std::optional<int> GetPackageCacheSizeLimitMBytes() const override {
    return cache_size_limit_;
  }

  void SetPackageCacheSizeLimitMBytes(int size_limit) {
    cache_size_limit_ = std::make_optional(size_limit);
  }

  std::optional<int> GetPackageCacheExpirationTimeDays() const override {
    return cache_expiration_time_;
  }

  void SetPackageCacheExpirationTimeDays(int expiration_time) {
    cache_expiration_time_ = std::make_optional(expiration_time);
  }

  void SetInstallPolicy(const std::string& app_id, int install_policy) {
    install_policies_[app_id] = install_policy;
  }

  std::optional<int> GetEffectivePolicyForAppInstalls(
      const std::string& app_id) const override {
    auto value = install_policies_.find(app_id);
    return value == install_policies_.end() ? std::nullopt
                                            : std::make_optional(value->second);
  }

  std::optional<int> GetEffectivePolicyForAppUpdates(
      const std::string& app_id) const override {
    auto value = update_policies_.find(app_id);
    return value == update_policies_.end() ? std::nullopt
                                           : std::make_optional(value->second);
  }

  void SetUpdatePolicy(const std::string& app_id, int update_policy) {
    update_policies_[app_id] = update_policy;
  }

  std::optional<std::string> GetProxyMode() const override {
    return proxy_mode_.empty() ? std::nullopt : std::make_optional(proxy_mode_);
  }

  void SetProxyMode(const std::string& proxy_mode) { proxy_mode_ = proxy_mode; }

  std::optional<std::string> GetProxyPacUrl() const override {
    return proxy_pac_url_.empty() ? std::nullopt
                                  : std::make_optional(proxy_pac_url_);
  }

  void SetProxyPacUrl(const std::string& proxy_pac_url) {
    proxy_pac_url_ = proxy_pac_url;
  }

  std::optional<std::string> GetProxyServer() const override {
    return proxy_server_.empty() ? std::nullopt
                                 : std::make_optional(proxy_server_);
  }

  void SetProxyServer(const std::string& proxy_server) {
    proxy_server_ = proxy_server;
  }

  std::optional<bool> IsRollbackToTargetVersionAllowed(
      const std::string& app_id) const override {
    return std::nullopt;
  }

  std::optional<std::string> GetTargetChannel(
      const std::string& app_id) const override {
    auto value = channels_.find(app_id);
    return value == channels_.end() ? std::nullopt
                                    : std::make_optional(value->second);
  }

  void SetChannel(const std::string& app_id, std::string channel) {
    channels_[app_id] = std::move(channel);
  }

  std::optional<std::string> GetTargetVersionPrefix(
      const std::string& app_id) const override {
    auto value = target_version_prefixes_.find(app_id);
    return value == target_version_prefixes_.end()
               ? std::nullopt
               : std::make_optional(value->second);
  }

  void SetTargetVersionPrefix(const std::string& app_id,
                              std::string target_version_prefix) {
    target_version_prefixes_[app_id] = std::move(target_version_prefix);
  }

  std::optional<int> GetMajorVersionRolloutPolicy(
      const std::string& app_id) const override {
    auto value = major_version_rollout_policies_.find(app_id);
    return value == major_version_rollout_policies_.end()
               ? std::nullopt
               : std::make_optional(value->second);
  }

  void SetMajorVersionRolloutPolicy(const std::string& app_id, int policy) {
    major_version_rollout_policies_[app_id] = policy;
  }

  std::optional<int> GetMinorVersionRolloutPolicy(
      const std::string& app_id) const override {
    auto value = minor_version_rollout_policies_.find(app_id);
    return value == minor_version_rollout_policies_.end()
               ? std::nullopt
               : std::make_optional(value->second);
  }

  void SetMinorVersionRolloutPolicy(const std::string& app_id, int policy) {
    minor_version_rollout_policies_[app_id] = policy;
  }

  std::optional<std::vector<std::string>> GetForceInstallApps() const override {
    return std::nullopt;
  }

  std::optional<std::vector<std::string>> GetAppsWithPolicy() const override {
    std::set<std::string> apps_with_policy;
    std::ranges::transform(
        install_policies_,
        std::inserter(apps_with_policy, apps_with_policy.end()),
        [](const auto& kv) { return kv.first; });
    std::ranges::transform(
        update_policies_,
        std::inserter(apps_with_policy, apps_with_policy.end()),
        [](const auto& kv) { return kv.first; });
    std::ranges::transform(
        channels_, std::inserter(apps_with_policy, apps_with_policy.end()),
        [](const auto& kv) { return kv.first; });
    return std::vector<std::string>(apps_with_policy.begin(),
                                    apps_with_policy.end());
  }

 private:
  ~FakePolicyManager() override = default;

  bool has_active_device_policies_;
  std::optional<bool> cloud_policy_overrides_platform_policy_;
  std::string source_;
  UpdatesSuppressedTimes suppressed_times_;
  std::optional<int> cache_size_limit_;
  std::optional<int> cache_expiration_time_;
  std::string download_preference_;
  std::string proxy_mode_;
  std::string proxy_server_;
  std::string proxy_pac_url_;
  std::map<std::string, int> install_policies_;
  std::map<std::string, int> update_policies_;
  std::map<std::string, std::string> channels_;
  std::map<std::string, std::string> target_version_prefixes_;
  std::map<std::string, int> major_version_rollout_policies_;
  std::map<std::string, int> minor_version_rollout_policies_;
};

class PolicyServiceTest : public ::testing::Test {
 protected:
  static scoped_refptr<PolicyService> CreatePolicyServiceForTesting(
      PolicyManagers managers) {
    auto policy_service = base::MakeRefCounted<PolicyService>(
        /*external_constants=*/nullptr,
        /*persisted_data=*/nullptr);
    policy_service->SetManagersForTesting(std::move(managers));
    return policy_service;
  }
};

TEST_F(PolicyServiceTest, DefaultPolicyValue) {
  auto policy_service =
      CreatePolicyServiceForTesting({GetDefaultValuesPolicyManager()});
  EXPECT_EQ(policy_service->source(), "Default");

  EXPECT_FALSE(policy_service->CloudPolicyOverridesPlatformPolicy());

  PolicyStatus<std::string> version_prefix =
      policy_service->GetTargetVersionPrefix("");
  EXPECT_FALSE(version_prefix);

  PolicyStatus<base::TimeDelta> last_check =
      policy_service->GetLastCheckPeriod();
  ASSERT_TRUE(last_check);
  EXPECT_EQ(last_check.policy(), base::Minutes(270));

  PolicyStatus<int> app_installs =
      policy_service->GetPolicyForAppInstalls("test1");
  ASSERT_TRUE(app_installs);
  EXPECT_EQ(app_installs.policy(), 1);

  PolicyStatus<int> app_updates =
      policy_service->GetPolicyForAppUpdates("test1");
  ASSERT_TRUE(app_updates);
  EXPECT_EQ(app_updates.policy(), 1);

  PolicyStatus<bool> rollback_allowed =
      policy_service->IsRollbackToTargetVersionAllowed("test1");
  ASSERT_TRUE(rollback_allowed);
  EXPECT_EQ(rollback_allowed.policy(), false);

  PolicyStatus<int> major_version_rollout_policy =
      policy_service->GetMajorVersionRolloutPolicy("test1");
  EXPECT_FALSE(major_version_rollout_policy);

  PolicyStatus<int> minor_version_rollout_policy =
      policy_service->GetMinorVersionRolloutPolicy("test1");
  EXPECT_FALSE(minor_version_rollout_policy);
}

TEST_F(PolicyServiceTest, ValidatePolicyValues) {
  {
    auto manager = base::MakeRefCounted<FakePolicyManager>(true, "manager");
    manager->SetDownloadPreference("unknown-download-preferences");
    manager->SetProxyMode("random-value");

    auto policy_service = CreatePolicyServiceForTesting(
        {std::move(manager), GetDefaultValuesPolicyManager()});
    EXPECT_FALSE(policy_service->CloudPolicyOverridesPlatformPolicy());
    EXPECT_FALSE(policy_service->GetDownloadPreference());
    EXPECT_FALSE(policy_service->GetProxyMode());
  }

  {
    auto manager = base::MakeRefCounted<FakePolicyManager>(true, "manager");
    manager->SetCloudPolicyOverridesPlatformPolicy(false);
    manager->SetDownloadPreference("cacheable");
    manager->SetProxyMode("auto_detect");

    auto policy_service = CreatePolicyServiceForTesting(
        {std::move(manager), GetDefaultValuesPolicyManager()});
    EXPECT_TRUE(policy_service->CloudPolicyOverridesPlatformPolicy());
    EXPECT_FALSE(policy_service->CloudPolicyOverridesPlatformPolicy().policy());
    EXPECT_TRUE(policy_service->GetDownloadPreference());
    EXPECT_EQ(policy_service->GetDownloadPreference().policy(), "cacheable");
    EXPECT_TRUE(policy_service->GetProxyMode());
    EXPECT_EQ(policy_service->GetProxyMode().policy(), "auto_detect");
  }
}

TEST_F(PolicyServiceTest, SinglePolicyManager) {
  auto manager = base::MakeRefCounted<FakePolicyManager>(true, "test_source");
  manager->SetCloudPolicyOverridesPlatformPolicy(true);
  manager->SetChannel("app1", "test_channel");
  manager->SetUpdatePolicy("app2", 3);
  auto policy_service = CreatePolicyServiceForTesting({std::move(manager)});
  EXPECT_EQ(policy_service->source(), "test_source");

  EXPECT_TRUE(policy_service->CloudPolicyOverridesPlatformPolicy());
  EXPECT_TRUE(policy_service->CloudPolicyOverridesPlatformPolicy().policy());
  PolicyStatus<std::string> app1_channel =
      policy_service->GetTargetChannel("app1");
  ASSERT_TRUE(app1_channel);
  EXPECT_EQ(app1_channel.policy(), "test_channel");
  EXPECT_EQ(app1_channel.conflict_policy(), std::nullopt);

  PolicyStatus<std::string> app2_channel =
      policy_service->GetTargetChannel("app2");
  EXPECT_FALSE(app2_channel);
  EXPECT_EQ(app2_channel.conflict_policy(), std::nullopt);

  PolicyStatus<int> app1_update_status =
      policy_service->GetPolicyForAppUpdates("app1");
  EXPECT_FALSE(app1_update_status);
  EXPECT_EQ(app1_update_status.conflict_policy(), std::nullopt);

  PolicyStatus<int> app2_update_status =
      policy_service->GetPolicyForAppUpdates("app2");
  EXPECT_TRUE(app2_update_status);
  EXPECT_EQ(app2_update_status.policy(), 3);
  EXPECT_EQ(app2_update_status.conflict_policy(), std::nullopt);
}

TEST_F(PolicyServiceTest, MultiplePolicyManagers) {
  PolicyManagers managers;

  auto manager = base::MakeRefCounted<FakePolicyManager>(true, "group_policy");
  manager->SetCloudPolicyOverridesPlatformPolicy(false);
  UpdatesSuppressedTimes updates_suppressed_times;
  updates_suppressed_times.start_hour_ = 5;
  updates_suppressed_times.start_minute_ = 10;
  updates_suppressed_times.duration_minute_ = 30;
  manager->SetUpdatesSuppressedTimes(updates_suppressed_times);
  manager->SetPackageCacheSizeLimitMBytes(1000);
  manager->SetInstallPolicy("app1", 0);
  manager->SetChannel("app1", "channel_gp");
  manager->SetUpdatePolicy("app2", 1);
  manager->SetUpdatePolicy(enterprise_companion::kCompanionAppId, 0);
  managers.push_back(std::move(manager));

  manager = base::MakeRefCounted<FakePolicyManager>(true, "device_management");
  manager->SetUpdatesSuppressedTimes(updates_suppressed_times);
  manager->SetPackageCacheExpirationTimeDays(60);
  manager->SetInstallPolicy("app1", 1);
  manager->SetChannel("app1", "channel_dm");
  manager->SetUpdatePolicy("app1", 3);
  managers.push_back(std::move(manager));

  manager = base::MakeRefCounted<FakePolicyManager>(true, "imaginary");
  manager->SetProxyMode("direct");
  manager->SetProxyPacUrl("url://proxyurl");
  manager->SetProxyServer("test-server");
  manager->SetDownloadPreference("cacheable");
  updates_suppressed_times.start_hour_ = 1;
  updates_suppressed_times.start_minute_ = 1;
  updates_suppressed_times.duration_minute_ = 20;
  manager->SetInstallPolicy(enterprise_companion::kCompanionAppId, 0);
  manager->SetUpdatesSuppressedTimes(updates_suppressed_times);
  manager->SetChannel("app1", "channel_imaginary");
  manager->SetTargetVersionPrefix("app1", "103.3.");
  manager->SetUpdatePolicy("app1", 2);
  manager->SetInstallPolicy("app2", 2);
  managers.push_back(std::move(manager));

  // The default policy manager.
  managers.push_back(GetDefaultValuesPolicyManager());

  auto policy_service = CreatePolicyServiceForTesting(std::move(managers));
  EXPECT_EQ(policy_service->source(),
            "group_policy;device_management;imaginary;Default");

  EXPECT_TRUE(policy_service->CloudPolicyOverridesPlatformPolicy());
  EXPECT_FALSE(policy_service->CloudPolicyOverridesPlatformPolicy().policy());
  EXPECT_EQ(policy_service->GetPackageCacheSizeLimitMBytes()
                .effective_policy()
                .value()
                .policy,
            1000);
  EXPECT_EQ(policy_service->GetPackageCacheExpirationTimeDays()
                .effective_policy()
                .value()
                .policy,
            60);
  EXPECT_EQ(policy_service->GetProxyMode().effective_policy().value().policy,
            "direct");
  EXPECT_EQ(policy_service->GetProxyPacUrl().effective_policy().value().policy,
            "url://proxyurl");
  EXPECT_EQ(policy_service->GetProxyServer().effective_policy().value().policy,
            "test-server");

  PolicyStatus<UpdatesSuppressedTimes> suppressed_time_status =
      policy_service->GetUpdatesSuppressedTimes();
  ASSERT_TRUE(suppressed_time_status);
  EXPECT_TRUE(suppressed_time_status.conflict_policy());
  EXPECT_EQ(suppressed_time_status.effective_policy().value().source,
            "group_policy");
  EXPECT_EQ(suppressed_time_status.policy().start_hour_, 5);
  EXPECT_EQ(suppressed_time_status.policy().start_minute_, 10);
  EXPECT_EQ(suppressed_time_status.policy().duration_minute_, 30);

  PolicyStatus<std::string> channel_status =
      policy_service->GetTargetChannel("app1");
  ASSERT_TRUE(channel_status);
  const PolicyStatus<std::string>::Entry& channel_policy =
      channel_status.effective_policy().value();
  EXPECT_EQ(channel_policy.source, "group_policy");
  EXPECT_EQ(channel_policy.policy, "channel_gp");
  EXPECT_TRUE(channel_status.conflict_policy());
  const PolicyStatus<std::string>::Entry& channel_conflict_policy =
      channel_status.conflict_policy().value();
  EXPECT_EQ(channel_conflict_policy.source, "device_management");
  EXPECT_EQ(channel_conflict_policy.policy, "channel_dm");
  EXPECT_EQ(channel_status.policy(), "channel_gp");

  PolicyStatus<int> companion_app_install_status =
      policy_service->GetPolicyForAppInstalls(
          enterprise_companion::kCompanionAppId);
  EXPECT_FALSE(companion_app_install_status)
      << "Compaion app install cannot be disabled.";

  PolicyStatus<int> companion_app_update_status =
      policy_service->GetPolicyForAppUpdates(
          enterprise_companion::kCompanionAppId);
  EXPECT_FALSE(companion_app_update_status)
      << "Compaion app update cannot be disabled.";

  PolicyStatus<int> app1_install_status =
      policy_service->GetPolicyForAppInstalls("app1");
  ASSERT_TRUE(app1_install_status);
  const PolicyStatus<int>::Entry& app1_install_policy =
      app1_install_status.effective_policy().value();
  EXPECT_EQ(app1_install_policy.source, "group_policy");
  EXPECT_EQ(app1_install_policy.policy, 0);
  EXPECT_TRUE(app1_install_status.conflict_policy());
  const PolicyStatus<int>::Entry& app1_install_conflict_policy =
      app1_install_status.conflict_policy().value();
  EXPECT_EQ(app1_install_conflict_policy.policy, 1);
  EXPECT_EQ(app1_install_conflict_policy.source, "device_management");
  EXPECT_EQ(app1_install_status.policy(), 0);

  PolicyStatus<int> app1_update_status =
      policy_service->GetPolicyForAppUpdates("app1");
  ASSERT_TRUE(app1_update_status);
  const PolicyStatus<int>::Entry& app1_update_policy =
      app1_update_status.effective_policy().value();
  EXPECT_EQ(app1_update_policy.source, "device_management");
  EXPECT_EQ(app1_update_policy.policy, 3);
  EXPECT_TRUE(app1_update_status.conflict_policy());
  const PolicyStatus<int>::Entry& app1_update_conflict_policy =
      app1_update_status.conflict_policy().value();
  EXPECT_TRUE(app1_update_status.conflict_policy());
  EXPECT_EQ(app1_update_conflict_policy.policy, 2);
  EXPECT_EQ(app1_update_conflict_policy.source, "imaginary");
  EXPECT_EQ(app1_update_status.policy(), 3);

  PolicyStatus<int> app2_install_status =
      policy_service->GetPolicyForAppInstalls("app2");
  ASSERT_TRUE(app2_install_status);
  const PolicyStatus<int>::Entry& app2_install_policy =
      app2_install_status.effective_policy().value();
  EXPECT_EQ(app2_install_policy.source, "imaginary");
  EXPECT_EQ(app2_install_policy.policy, 2);
  EXPECT_TRUE(app2_install_status.conflict_policy());
  const PolicyStatus<int>::Entry& app2_install_conflict_policy =
      app2_install_status.conflict_policy().value();
  EXPECT_EQ(app2_install_conflict_policy.policy, 1);
  EXPECT_EQ(app2_install_conflict_policy.source, "Default");
  EXPECT_EQ(app2_install_status.policy(), 2);

  PolicyStatus<int> app2_update_status =
      policy_service->GetPolicyForAppUpdates("app2");
  ASSERT_TRUE(app2_update_status);
  const PolicyStatus<int>::Entry& app2_update_policy =
      app2_update_status.effective_policy().value();
  EXPECT_EQ(app2_update_policy.source, "group_policy");
  EXPECT_EQ(app2_update_policy.policy, 1);
  EXPECT_EQ(app2_update_status.policy(), 1);
  EXPECT_EQ(app2_update_status.conflict_policy(), std::nullopt);

  PolicyStatus<std::string> download_preference_status =
      policy_service->GetDownloadPreference();
  ASSERT_TRUE(download_preference_status);
  const PolicyStatus<std::string>::Entry& download_preference_policy =
      download_preference_status.effective_policy().value();
  EXPECT_EQ(download_preference_policy.source, "imaginary");
  EXPECT_EQ(download_preference_policy.policy, "cacheable");
  EXPECT_EQ(download_preference_status.policy(), "cacheable");
  EXPECT_EQ(download_preference_status.conflict_policy(), std::nullopt);

  EXPECT_EQ(policy_service->GetAllPoliciesAsString(),
            base::StringPrintf(
                "{\n"
                "  CloudPolicyOverridesPlatformPolicy = 0 (group_policy)\n"
                "  LastCheckPeriod = 270 (Default)\n"
                "  UpdatesSuppressed = "
                "{StartHour: 5, StartMinute: 10, Duration: 30} (group_policy)\n"
                "  DownloadPreference = cacheable (imaginary)\n"
                "  PackageCacheSizeLimit = 1000 MB (group_policy)\n"
                "  PackageCacheExpires = 60 days (device_management)\n"
                "  ProxyMode = direct (imaginary)\n"
                "  ProxyPacURL = url://proxyurl (imaginary)\n"
                "  ProxyServer = test-server (imaginary)\n"
                "  \"app1\": {\n"
                "    Install = 0 (group_policy)\n"
                "    Update = 3 (device_management)\n"
                "    TargetChannel = channel_gp (group_policy)\n"
                "    TargetVersionPrefix = 103.3. (imaginary)\n"
                "    RollbackToTargetVersionAllowed = 0 (Default)\n"
                "  }\n"
                "  \"app2\": {\n"
                "    Install = 2 (imaginary)\n"
                "    Update = 1 (group_policy)\n"
                "    RollbackToTargetVersionAllowed = 0 (Default)\n"
                "  }\n"
                "  \"%s\": {\n"
                "    \n"
                "  }\n"
                "}\n",
                enterprise_companion::kCompanionAppId));
  EXPECT_EQ(
      policy_service->GetAllPolicies(),
      base::Value(
          base::Value::Dict()
              .Set("CloudPolicyOverridesPlatformPolicy",
                   base::Value::Dict()
                       .Set("value", false)
                       .Set("source", "group_policy"))
              .Set("LastCheckPeriod", base::Value::Dict()
                                          .Set("value", 270)
                                          .Set("source", "Default"))
              .Set("UpdatesSuppressed", base::Value::Dict()
                                            .Set("StartHour", 5)
                                            .Set("StartMinute", 10)
                                            .Set("Duration", 30)
                                            .Set("source", "group_policy"))
              .Set("DownloadPreference", base::Value::Dict()
                                             .Set("value", "cacheable")
                                             .Set("source", "imaginary"))
              .Set("PackageCacheSizeLimit", base::Value::Dict()
                                                .Set("value", 1000)
                                                .Set("source", "group_policy"))
              .Set("PackageCacheExpires",
                   base::Value::Dict()
                       .Set("value", 60)
                       .Set("source", "device_management"))
              .Set("ProxyMode", base::Value::Dict()
                                    .Set("value", "direct")
                                    .Set("source", "imaginary"))
              .Set("ProxyPacURL",
                   base::Value::Dict()
                       .Set("value", "url://proxyurl")
                       .Set("source", "imaginary"))
              .Set("ProxyServer",
                   base::Value::Dict()
                       .Set("value", "test-server")
                       .Set("source", "imaginary"))
              .Set("app1",
                   base::Value::Dict()
                       .Set("Install", base::Value::Dict()
                                           .Set("value", 0)
                                           .Set("source", "group_policy"))
                       .Set("Update", base::Value::Dict()
                                          .Set("value", 3)
                                          .Set("source", "device_management"))
                       .Set("RollbackToTargetVersionAllowed",
                            base::Value::Dict()
                                .Set("value", false)
                                .Set("source", "Default"))
                       .Set("TargetChannel", base::Value::Dict()
                                                 .Set("value", "channel_gp")
                                                 .Set("source", "group_policy"))
                       .Set("TargetVersionPrefix",
                            base::Value::Dict()
                                .Set("value", "103.3.")
                                .Set("source", "imaginary"))
                       .Set("RollbackToTargetVersionAllowed",
                            base::Value::Dict()
                                .Set("value", false)
                                .Set("source", "Default")))
              .Set("app2",
                   base::Value::Dict()
                       .Set("Install", base::Value::Dict()
                                           .Set("value", 2)
                                           .Set("source", "imaginary"))
                       .Set("Update", base::Value::Dict()
                                          .Set("value", 1)
                                          .Set("source", "group_policy"))
                       .Set("RollbackToTargetVersionAllowed",
                            base::Value::Dict()
                                .Set("value", false)
                                .Set("source", "Default"))
                       .Set("RollbackToTargetVersionAllowed",
                            base::Value::Dict()
                                .Set("value", false)
                                .Set("source", "Default")))
              .Set(enterprise_companion::kCompanionAppId,
                   base::Value::Dict())));
}

TEST_F(PolicyServiceTest, MultiplePolicyManagers_WithUnmanagedOnes) {
  PolicyManagers managers;

  auto manager =
      base::MakeRefCounted<FakePolicyManager>(true, "device_management");
  UpdatesSuppressedTimes updates_suppressed_times;
  updates_suppressed_times.start_hour_ = 5;
  updates_suppressed_times.start_minute_ = 10;
  updates_suppressed_times.duration_minute_ = 30;
  manager->SetUpdatesSuppressedTimes(updates_suppressed_times);
  manager->SetChannel("app1", "channel_dm");
  manager->SetUpdatePolicy("app1", 3);
  managers.push_back(std::move(manager));

  manager = base::MakeRefCounted<FakePolicyManager>(true, "imaginary");
  updates_suppressed_times.start_hour_ = 1;
  updates_suppressed_times.start_minute_ = 1;
  updates_suppressed_times.duration_minute_ = 20;
  manager->SetUpdatesSuppressedTimes(updates_suppressed_times);
  manager->SetChannel("app1", "channel_imaginary");
  manager->SetUpdatePolicy("app1", 2);
  manager->SetDownloadPreference("cacheable");
  managers.push_back(std::move(manager));

  managers.push_back(GetDefaultValuesPolicyManager());

  manager = base::MakeRefCounted<FakePolicyManager>(false, "group_policy");
  updates_suppressed_times.start_hour_ = 5;
  updates_suppressed_times.start_minute_ = 10;
  updates_suppressed_times.duration_minute_ = 30;
  manager->SetUpdatesSuppressedTimes(updates_suppressed_times);
  manager->SetChannel("app1", "channel_gp");
  manager->SetUpdatePolicy("app2", 1);
  managers.push_back(std::move(manager));

  auto policy_service = CreatePolicyServiceForTesting(std::move(managers));
  EXPECT_EQ(policy_service->source(), "device_management;imaginary;Default");

  PolicyStatus<UpdatesSuppressedTimes> suppressed_time_status =
      policy_service->GetUpdatesSuppressedTimes();
  ASSERT_TRUE(suppressed_time_status);
  EXPECT_EQ(suppressed_time_status.effective_policy().value().source,
            "device_management");
  EXPECT_EQ(suppressed_time_status.policy().start_hour_, 5);
  EXPECT_EQ(suppressed_time_status.policy().start_minute_, 10);
  EXPECT_EQ(suppressed_time_status.policy().duration_minute_, 30);

  PolicyStatus<std::string> channel_status =
      policy_service->GetTargetChannel("app1");
  ASSERT_TRUE(channel_status);
  const PolicyStatus<std::string>::Entry& channel_status_policy =
      channel_status.effective_policy().value();
  EXPECT_EQ(channel_status_policy.source, "device_management");
  EXPECT_EQ(channel_status_policy.policy, "channel_dm");
  EXPECT_TRUE(channel_status.conflict_policy());
  const PolicyStatus<std::string>::Entry& channel_status_conflict_policy =
      channel_status.conflict_policy().value();
  EXPECT_EQ(channel_status_conflict_policy.policy, "channel_imaginary");
  EXPECT_EQ(channel_status_conflict_policy.source, "imaginary");
  EXPECT_EQ(channel_status.policy(), "channel_dm");

  PolicyStatus<int> app1_update_status =
      policy_service->GetPolicyForAppUpdates("app1");
  ASSERT_TRUE(app1_update_status);
  const PolicyStatus<int>::Entry& app1_update_status_policy =
      app1_update_status.effective_policy().value();
  EXPECT_EQ(app1_update_status_policy.source, "device_management");
  EXPECT_EQ(app1_update_status_policy.policy, 3);
  EXPECT_TRUE(app1_update_status.conflict_policy());
  const PolicyStatus<int>::Entry& app1_update_status_conflict_policy =
      app1_update_status.conflict_policy().value();
  EXPECT_EQ(app1_update_status_conflict_policy.source, "imaginary");
  EXPECT_EQ(app1_update_status_conflict_policy.policy, 2);
  EXPECT_EQ(app1_update_status.policy(), 3);

  PolicyStatus<int> app2_update_status =
      policy_service->GetPolicyForAppUpdates("app2");
  ASSERT_TRUE(app2_update_status);
  EXPECT_EQ(app2_update_status.conflict_policy(), std::nullopt);
  const PolicyStatus<int>::Entry& app2_update_status_policy =
      app2_update_status.effective_policy().value();
  EXPECT_EQ(app2_update_status_policy.source, "Default");
  EXPECT_EQ(app2_update_status_policy.policy, 1);
  EXPECT_EQ(app2_update_status.policy(), 1);

  PolicyStatus<std::string> download_preference_status =
      policy_service->GetDownloadPreference();
  ASSERT_TRUE(download_preference_status);
  EXPECT_EQ(download_preference_status.effective_policy().value().source,
            "imaginary");
  EXPECT_EQ(download_preference_status.policy(), "cacheable");
  EXPECT_EQ(download_preference_status.conflict_policy(), std::nullopt);

  EXPECT_FALSE(policy_service->GetPackageCacheSizeLimitMBytes());

  EXPECT_EQ(
      policy_service->GetAllPoliciesAsString(),
      "{\n"
      "  LastCheckPeriod = 270 (Default)\n"
      "  UpdatesSuppressed = "
      "{StartHour: 5, StartMinute: 10, Duration: 30} (device_management)\n"
      "  DownloadPreference = cacheable (imaginary)\n"
      "  \"app1\": {\n"
      "    Install = 1 (Default)\n"
      "    Update = 3 (device_management)\n"
      "    TargetChannel = channel_dm (device_management)\n"
      "    RollbackToTargetVersionAllowed = 0 (Default)\n"
      "  }\n"
      "  \"app2\": {\n"
      "    Install = 1 (Default)\n"
      "    Update = 1 (Default)\n"
      "    RollbackToTargetVersionAllowed = 0 (Default)\n"
      "  }\n"
      "}\n");
}

struct PolicyServiceAreUpdatesSuppressedNowTestCase {
  const UpdatesSuppressedTimes updates_suppressed_times;
  const std::string now_string;
  const bool expect_updates_suppressed;
};

class PolicyServiceAreUpdatesSuppressedNowTest
    : public ::testing::WithParamInterface<
          PolicyServiceAreUpdatesSuppressedNowTestCase>,
      public PolicyServiceTest {};

INSTANTIATE_TEST_SUITE_P(
    PolicyServiceAreUpdatesSuppressedNowTestCases,
    PolicyServiceAreUpdatesSuppressedNowTest,
    ::testing::ValuesIn(
        std::vector<PolicyServiceAreUpdatesSuppressedNowTestCase>{
            // Suppress starting 12:00 for 959 minutes.
            {{12, 00, 959}, "Sat, 01 July 2023 01:15:00", true},

            // Suppress starting 12:00 for 959 minutes.
            {{12, 00, 959}, "Sat, 01 July 2023 04:15:00", false},

            // Suppress starting 00:00 for 959 minutes.
            {{00, 00, 959}, "Sat, 01 July 2023 04:15:00", true},

            // Suppress starting 00:00 for 959 minutes.
            {{00, 00, 959}, "Sat, 01 July 2023 16:15:00", false},

            // Suppress starting 18:00 for 12 hours.
            {{18, 00, 12 * 60}, "Sat, 01 July 2023 05:15:00", true},

            // Suppress starting 18:00 for 12 hours.
            {{18, 00, 12 * 60}, "Sat, 01 July 2023 06:15:00", false},
        }));

TEST_P(PolicyServiceAreUpdatesSuppressedNowTest, TestCases) {
  auto manager = base::MakeRefCounted<FakePolicyManager>(true, "group_policy");
  manager->SetUpdatesSuppressedTimes(GetParam().updates_suppressed_times);

  base::Time now;
  ASSERT_TRUE(base::Time::FromString(GetParam().now_string.c_str(), &now));
  EXPECT_EQ(GetParam().expect_updates_suppressed,
            CreatePolicyServiceForTesting({std::move(manager)})
                ->AreUpdatesSuppressedNow(now));
}

TEST_F(PolicyServiceTest, PolicyServiceProxyConfiguration_Get) {
  // Test proxy mode "auto_detect".
  auto manager = base::MakeRefCounted<FakePolicyManager>(true, "manager");
  manager->SetProxyMode("auto_detect");
  manager->SetProxyPacUrl("pac://server");
  manager->SetProxyServer("proxy_server");
  auto policy_service = CreatePolicyServiceForTesting(
      {std::move(manager), GetDefaultValuesPolicyManager()});
  std::optional<PolicyServiceProxyConfiguration> proxy_configuration =
      PolicyServiceProxyConfiguration::Get(policy_service);
  ASSERT_TRUE(proxy_configuration);
  ASSERT_TRUE(proxy_configuration->proxy_auto_detect);
  ASSERT_FALSE(proxy_configuration->proxy_pac_url);
  ASSERT_FALSE(proxy_configuration->proxy_url);

  // Test proxy mode "fixed_servers".
  manager = base::MakeRefCounted<FakePolicyManager>(true, "manager");
  manager->SetProxyMode("fixed_servers");
  manager->SetProxyPacUrl("pac://server");
  manager->SetProxyServer("proxy_server");
  policy_service = CreatePolicyServiceForTesting(
      {std::move(manager), GetDefaultValuesPolicyManager()});
  proxy_configuration = PolicyServiceProxyConfiguration::Get(policy_service);
  ASSERT_TRUE(proxy_configuration);
  ASSERT_FALSE(proxy_configuration->proxy_auto_detect);
  ASSERT_FALSE(proxy_configuration->proxy_pac_url);
  ASSERT_TRUE(proxy_configuration->proxy_url);
  ASSERT_EQ(*proxy_configuration->proxy_url, "proxy_server");

  // Test proxy mode "pac_script".
  manager = base::MakeRefCounted<FakePolicyManager>(true, "manager");
  manager->SetProxyMode("pac_script");
  manager->SetProxyPacUrl("pac://server");
  manager->SetProxyServer("proxy_server");
  policy_service = CreatePolicyServiceForTesting(
      {std::move(manager), GetDefaultValuesPolicyManager()});
  proxy_configuration = PolicyServiceProxyConfiguration::Get(policy_service);
  ASSERT_TRUE(proxy_configuration);
  ASSERT_FALSE(proxy_configuration->proxy_auto_detect);
  ASSERT_TRUE(proxy_configuration->proxy_pac_url);
  ASSERT_EQ(proxy_configuration->proxy_pac_url, "pac://server");
  ASSERT_FALSE(proxy_configuration->proxy_url);

  // Test unknown proxy mode.
  manager = base::MakeRefCounted<FakePolicyManager>(true, "manager");
  manager->SetProxyMode("unknown");
  manager->SetProxyPacUrl("pac://server");
  manager->SetProxyServer("proxy_server");
  policy_service = CreatePolicyServiceForTesting(
      {std::move(manager), GetDefaultValuesPolicyManager()});
  ASSERT_FALSE(PolicyServiceProxyConfiguration::Get(policy_service));
}

class PolicyManagersTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(DeleteOverridesFile());

#if BUILDFLAG(IS_WIN)
    ASSERT_NO_FATAL_FAILURE(
        registry_overrides_.OverrideRegistry(HKEY_LOCAL_MACHINE));
#endif
  }

  void TearDown() override { ASSERT_NO_FATAL_FAILURE(DeleteOverridesFile()); }

  void DeleteOverridesFile() {
#if BUILDFLAG(IS_MAC)
    if (!IsSystemInstall(GetUpdaterScopeForTesting())) {
      GTEST_SKIP() << "test skipped for user install.";
    }

    if (base::PathExists(*overrides_file_path_)) {
      RunCommand(std::vector<std::string>(
          {"/usr/bin/sudo", "/bin/rm", overrides_file_path_->value()}));
    }
#else
    ASSERT_TRUE(base::DeleteFile(*overrides_file_path_))
        << *overrides_file_path_;
#endif
  }

#if BUILDFLAG(IS_MAC)
  void RunCommand(const std::vector<std::string> argv,
                  bool check_result = false) const {
    base::Process process = base::LaunchProcess(argv, {});
    if (!process.IsValid()) {
      VLOG(2) << "Failed to launch command.";
      return;
    }
    int exit_code = -1;
    EXPECT_TRUE(process.WaitForExitWithTimeout(TestTimeouts::action_timeout(),
                                               &exit_code));
    if (check_result) {
      EXPECT_EQ(exit_code, 0);
    }
  }
#endif

  void SetPlatformPolicies(const base::Value::Dict& policies) const {
#if BUILDFLAG(IS_MAC)
    const base::FilePath policy_file_path =
        GetLibraryFolderPath(UpdaterScope::kSystem)
            ->AppendUTF8("Managed Preferences")
            .AppendUTF8(LEGACY_GOOGLE_UPDATE_APPID ".plist");

    if (!base::PathExists(policy_file_path)) {
      RunCommand(std::vector<std::string>({"/usr/bin/sudo", "/usr/bin/plutil",
                                           "-create", "binary1",
                                           policy_file_path.value()}));
    }

    std::string policy_json_string;
    JSONStringValueSerializer serializer(&policy_json_string);
    serializer.Serialize(policies);
    RunCommand(std::vector<std::string>(
        {"/usr/bin/sudo", "/usr/bin/plutil", "-replace", "updatePolicies",
         "-json", policy_json_string, policy_file_path.value()}));

    // Refresh policies and force flushing preferences cache.
    const CFStringRef domain = CFSTR(LEGACY_GOOGLE_UPDATE_APPID);
    ASSERT_TRUE(CFPreferencesSynchronize(domain, kCFPreferencesAnyUser,
                                         kCFPreferencesCurrentHost));
    RunCommand(std::vector<std::string>(
                   {"/usr/bin/sudo", "/usr/bin/killall", "cfprefsd"}),
               /*check_result=*/false);
#else
    test::SetPlatformPolicies(policies);
#endif
  }

 private:
  const std::optional<base::FilePath> overrides_file_path_ =
      GetOverrideFilePath(GetUpdaterScopeForTesting());

#if BUILDFLAG(IS_WIN)
  registry_util::RegistryOverrideManager registry_overrides_;
  base::test::TaskEnvironment environment_;
#endif
};

TEST_F(PolicyManagersTest, NullExternalConstants) {
  PolicyService::PolicyManagers managers({});
  ASSERT_EQ(managers.managers().size(), size_t{1});
  EXPECT_EQ(managers.managers()[0]->source(), "Default");
}

TEST_F(PolicyManagersTest, MachineUnmanaged) {
  ASSERT_TRUE(ExternalConstantsBuilder().SetMachineManaged(false).Overwrite());
  PolicyService::PolicyManagers managers(CreateExternalConstants());
  managers.ResetDeviceManagementManager({});

  ASSERT_EQ(managers.managers().size(),
            size_t{1 + kPlatformPolicyManagerDefined});
  EXPECT_EQ(managers.managers()[0]->source(), "Default");
  if (kPlatformPolicyManagerDefined) {
    EXPECT_EQ(managers.managers()[0 + kPlatformPolicyManagerDefined]->source(),
              kSourcePlatformPolicyManager);
  }
}

TEST_F(PolicyManagersTest, ValidDeviceManagementManager) {
  ASSERT_TRUE(ExternalConstantsBuilder().SetMachineManaged(false).Overwrite());
  auto omaha_settings =
      std::make_unique<::wireless_android_enterprise_devicemanagement::
                           OmahaSettingsClientProto>();
  auto dm_policy = base::MakeRefCounted<DMPolicyManager>(*omaha_settings, true);
  PolicyService::PolicyManagers managers(CreateExternalConstants());
  managers.ResetDeviceManagementManager(dm_policy);

  ASSERT_EQ(managers.managers().size(),
            size_t{2 + kPlatformPolicyManagerDefined});
  EXPECT_EQ(managers.managers()[0]->source(), "Device Management");
  EXPECT_EQ(managers.managers()[1]->source(), "Default");
  if (kPlatformPolicyManagerDefined) {
    EXPECT_EQ(managers.managers()[1 + kPlatformPolicyManagerDefined]->source(),
              kSourcePlatformPolicyManager);
  }
}

TEST_F(PolicyManagersTest, ValidDictPlatformPolicies) {
#if BUILDFLAG(IS_MAC)
  if (!IsSystemInstall(GetUpdaterScopeForTesting())) {
    GTEST_SKIP() << "test skipped for user install.";
  }
#endif

  base::Value::Dict dict_policies;
  dict_policies.Set("a", 1);

  ASSERT_TRUE(ExternalConstantsBuilder()
                  .SetMachineManaged(true)
                  .SetDictPolicies(dict_policies)
                  .Overwrite());

  base::Value::Dict policies;
  policies.Set(kGlobalPolicyKey,
               base::Value::Dict().Set("CloudPolicyOverridesPlatformPolicy",
                                       kPolicyEnabled));
  ASSERT_NO_FATAL_FAILURE(SetPlatformPolicies(policies));

  PolicyService::PolicyManagers managers(CreateExternalConstants());
  managers.ResetDeviceManagementManager({});

  ASSERT_EQ(managers.managers().size(),
            size_t{2 + kPlatformPolicyManagerDefined});
  EXPECT_EQ(managers.managers()[0]->source(), "DictValuePolicy");
  if (kPlatformPolicyManagerDefined) {
    EXPECT_EQ(managers.managers()[0 + kPlatformPolicyManagerDefined]->source(),
              kSourcePlatformPolicyManager);
  }
  EXPECT_EQ(managers.managers()[1 + kPlatformPolicyManagerDefined]->source(),
            "Default");
}

TEST_F(PolicyManagersTest, ValidDeviceManagementPlatformPolicyNoCloudOverride) {
#if BUILDFLAG(IS_MAC)
  if (!IsSystemInstall(GetUpdaterScopeForTesting())) {
    GTEST_SKIP() << "test skipped for user install.";
  }
#endif

  ASSERT_TRUE(ExternalConstantsBuilder().SetMachineManaged(true).Overwrite());

  base::Value::Dict policies;
  policies.Set(kGlobalPolicyKey,
               base::Value::Dict().Set("CloudPolicyOverridesPlatformPolicy",
                                       kPolicyDisabled));
  ASSERT_NO_FATAL_FAILURE(SetPlatformPolicies(policies));

  auto omaha_settings =
      std::make_unique<::wireless_android_enterprise_devicemanagement::
                           OmahaSettingsClientProto>();
  auto dm_policy = base::MakeRefCounted<DMPolicyManager>(*omaha_settings, true);
  PolicyService::PolicyManagers managers(CreateExternalConstants());
  managers.ResetDeviceManagementManager(dm_policy);
  ASSERT_EQ(managers.managers().size(),
            size_t{2 + kPlatformPolicyManagerDefined});
  if (kPlatformPolicyManagerDefined) {
    EXPECT_EQ(managers.managers()[0]->source(),
              kCloudPolicyOverridesPlatformPolicyDefaultValue
                  ? "Device Management"
                  : kSourcePlatformPolicyManager);
    EXPECT_EQ(managers.managers()[1]->source(),
              kCloudPolicyOverridesPlatformPolicyDefaultValue
                  ? kSourcePlatformPolicyManager
                  : "Device Management");
  } else {
    EXPECT_EQ(managers.managers()[0]->source(), "Device Management");
  }

  EXPECT_EQ(managers.managers()[1 + kPlatformPolicyManagerDefined]->source(),
            "Default");
}

TEST_F(PolicyManagersTest, ValidDeviceManagementPlatformPolicyCloudOverride) {
#if BUILDFLAG(IS_MAC)
  if (!IsSystemInstall(GetUpdaterScopeForTesting())) {
    GTEST_SKIP() << "test skipped for user install.";
  }
#endif

  ASSERT_TRUE(ExternalConstantsBuilder().SetMachineManaged(true).Overwrite());

  base::Value::Dict policies;
  policies.Set(kGlobalPolicyKey,
               base::Value::Dict().Set("CloudPolicyOverridesPlatformPolicy",
                                       kPolicyEnabled));
  ASSERT_NO_FATAL_FAILURE(SetPlatformPolicies(policies));

  auto omaha_settings =
      std::make_unique<::wireless_android_enterprise_devicemanagement::
                           OmahaSettingsClientProto>();
  auto dm_policy = base::MakeRefCounted<DMPolicyManager>(*omaha_settings, true);
  PolicyService::PolicyManagers managers(CreateExternalConstants());
  managers.ResetDeviceManagementManager(dm_policy);

  ASSERT_EQ(managers.managers().size(),
            size_t{2 + kPlatformPolicyManagerDefined});
  EXPECT_EQ(managers.managers()[0]->source(), "Device Management");
  if (kPlatformPolicyManagerDefined) {
    EXPECT_EQ(managers.managers()[0 + kPlatformPolicyManagerDefined]->source(),
              kSourcePlatformPolicyManager);
  }
  EXPECT_EQ(managers.managers()[1 + kPlatformPolicyManagerDefined]->source(),
            "Default");
}

TEST_F(PolicyManagersTest,
       ValidDictDeviceManagementPlatformPolicyCloudOverride) {
#if BUILDFLAG(IS_MAC)
  if (!IsSystemInstall(GetUpdaterScopeForTesting())) {
    GTEST_SKIP() << "test skipped for user install.";
  }
#endif

  base::Value::Dict dict_policies;
  dict_policies.Set("a", 1);

  ASSERT_TRUE(ExternalConstantsBuilder()
                  .SetMachineManaged(true)
                  .SetDictPolicies(dict_policies)
                  .Overwrite());

  base::Value::Dict policies;
  policies.Set(kGlobalPolicyKey,
               base::Value::Dict().Set("CloudPolicyOverridesPlatformPolicy",
                                       kPolicyEnabled));
  ASSERT_NO_FATAL_FAILURE(SetPlatformPolicies(policies));

  auto omaha_settings =
      std::make_unique<::wireless_android_enterprise_devicemanagement::
                           OmahaSettingsClientProto>();
  auto dm_policy = base::MakeRefCounted<DMPolicyManager>(*omaha_settings, true);
  PolicyService::PolicyManagers managers(CreateExternalConstants());
  managers.ResetDeviceManagementManager(dm_policy);
  ASSERT_EQ(managers.managers().size(),
            size_t{3 + kPlatformPolicyManagerDefined});
  EXPECT_EQ(managers.managers()[0]->source(), "DictValuePolicy");
  EXPECT_EQ(managers.managers()[1]->source(), "Device Management");
  if (kPlatformPolicyManagerDefined) {
    EXPECT_EQ(managers.managers()[1 + kPlatformPolicyManagerDefined]->source(),
              kSourcePlatformPolicyManager);
  }

  EXPECT_EQ(managers.managers()[2 + kPlatformPolicyManagerDefined]->source(),
            "Default");
}

}  // namespace updater
