// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/component_updater/afp_blocked_domain_list_component_installer.h"

#include "afp_blocked_domain_list_component_installer.h"
#include "base/check.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/raw_ptr.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/version.h"
#include "chrome/common/chrome_features.h"
#include "chrome/test/base/testing_browser_process.h"
#include "components/component_updater/mock_component_updater_service.h"
#include "components/fingerprinting_protection_filter/browser/fingerprinting_protection_ruleset_publisher.h"
#include "components/fingerprinting_protection_filter/common/fingerprinting_protection_filter_constants.h"
#include "components/fingerprinting_protection_filter/common/fingerprinting_protection_filter_features.h"
#include "components/prefs/testing_pref_service.h"
#include "components/subresource_filter/content/shared/browser/ruleset_publisher.h"
#include "components/subresource_filter/content/shared/browser/ruleset_service.h"
#include "components/subresource_filter/core/browser/ruleset_version.h"
#include "components/subresource_filter/core/browser/subresource_filter_constants.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/platform_test.h"

namespace {

using ::testing::_;

constexpr char kTestRulesetVersion[] = "1.2.3.4";
constexpr int kInvalidRulesetFormat = 0;

class TestRulesetService : public ::subresource_filter::RulesetService {
 public:
  TestRulesetService(
      PrefService* local_state,
      scoped_refptr<base::SequencedTaskRunner> task_runner,
      const base::FilePath& base_dir,
      scoped_refptr<base::SequencedTaskRunner> blocking_task_runner)
      : subresource_filter::RulesetService(
            fingerprinting_protection_filter::
                kFingerprintingProtectionRulesetConfig,
            local_state,
            task_runner,
            base_dir,
            blocking_task_runner,
            fingerprinting_protection_filter::
                FingerprintingProtectionRulesetPublisher::Factory()) {}

  TestRulesetService(const TestRulesetService&) = delete;
  TestRulesetService& operator=(const TestRulesetService&) = delete;

  using UnindexedRulesetInfo = ::subresource_filter::UnindexedRulesetInfo;
  void IndexAndStoreAndPublishRulesetIfNeeded(
      const UnindexedRulesetInfo& unindexed_ruleset_info) override {
    unindexed_ruleset_info_ = unindexed_ruleset_info;
  }

  const base::FilePath& ruleset_path() const {
    return unindexed_ruleset_info_.ruleset_path;
  }

  const base::FilePath& license_path() const {
    return unindexed_ruleset_info_.license_path;
  }

  const std::string& content_version() const {
    return unindexed_ruleset_info_.content_version;
  }

 private:
  UnindexedRulesetInfo unindexed_ruleset_info_;
};

}  // namespace

namespace component_updater {

class AntiFingerprintingBlockedDomainListComponentInstallerTest
    : public PlatformTest {
 public:
  AntiFingerprintingBlockedDomainListComponentInstallerTest() = default;

  AntiFingerprintingBlockedDomainListComponentInstallerTest(
      const AntiFingerprintingBlockedDomainListComponentInstallerTest&) =
      delete;
  AntiFingerprintingBlockedDomainListComponentInstallerTest& operator=(
      const AntiFingerprintingBlockedDomainListComponentInstallerTest&) =
      delete;

  void SetUp() override {
    PlatformTest::SetUp();
    CHECK(component_install_dir_.CreateUniqueTempDir());
    CHECK(ruleset_install_dir_.CreateUniqueTempDir());

    subresource_filter::IndexedRulesetVersion::RegisterPrefs(
        pref_service_.registry(),
        fingerprinting_protection_filter::kFingerprintingProtectionRulesetConfig
            .filter_tag);

    auto test_ruleset_service = std::make_unique<TestRulesetService>(
        &pref_service_, base::SingleThreadTaskRunner::GetCurrentDefault(),
        ruleset_install_dir_.GetPath(),
        base::SingleThreadTaskRunner::GetCurrentDefault());
    test_ruleset_service_ = test_ruleset_service.get();

    TestingBrowserProcess::GetGlobal()
        ->SetFingerprintingProtectionRulesetService(
            std::move(test_ruleset_service));
    policy_ = std::make_unique<
        AntiFingerprintingBlockedDomainListComponentInstallerPolicy>();
  }

  void TearDown() override {
    TestingBrowserProcess::GetGlobal()
        ->SetFingerprintingProtectionRulesetService(nullptr);
    task_env_.RunUntilIdle();
    PlatformTest::TearDown();
  }

  TestRulesetService* service() { return test_ruleset_service_; }

  const base::FilePath& install_dir() {
    return component_install_dir_.GetPath();
  }

  content::BrowserTaskEnvironment& task_env() { return task_env_; }

  auto* policy() { return policy_.get(); }

  void WriteStringToFile(const std::string& data, const base::FilePath& path) {
    ASSERT_TRUE(base::WriteFile(path, data));
  }

  void CreateTestRuleset(const std::string& ruleset_contents) {
    base::FilePath ruleset_data_path = install_dir().Append(
        fingerprinting_protection_filter::kUnindexedRulesetDataFileName);
    ASSERT_NO_FATAL_FAILURE(
        WriteStringToFile(ruleset_contents, ruleset_data_path));
  }

  void LoadTestRuleset(int ruleset_format) {
    base::Value::Dict manifest;
    manifest.Set(AntiFingerprintingBlockedDomainListComponentInstallerPolicy::
                     kManifestRulesetFormatKey,
                 ruleset_format);
    ASSERT_TRUE(policy_->VerifyInstallation(manifest, install_dir()));
    const base::Version expected_version(kTestRulesetVersion);
    policy_->ComponentReady(expected_version, install_dir(),
                            std::move(manifest));
    task_env().RunUntilIdle();
  }

  void ErrorLoadTestRuleset(int ruleset_format,
                            const base::FilePath& install_dir) {
    base::Value::Dict manifest;
    manifest.Set(AntiFingerprintingBlockedDomainListComponentInstallerPolicy::
                     kManifestRulesetFormatKey,
                 ruleset_format);
    ASSERT_FALSE(policy_->VerifyInstallation(manifest, install_dir));
  }

 private:
  base::ScopedTempDir component_install_dir_;
  base::ScopedTempDir ruleset_install_dir_;

  content::BrowserTaskEnvironment task_env_;

  std::unique_ptr<AntiFingerprintingBlockedDomainListComponentInstallerPolicy>
      policy_;
  TestingPrefServiceSimple pref_service_;

  raw_ptr<TestRulesetService, DanglingUntriaged> test_ruleset_service_ =
      nullptr;
};

TEST_F(AntiFingerprintingBlockedDomainListComponentInstallerTest,
       ComponentRegistration_FeatureEnabled) {
  base::test::ScopedFeatureList scoped_enable;
  scoped_enable.InitAndEnableFeature(
      fingerprinting_protection_filter::features::
          kEnableFingerprintingProtectionFilter);

  auto service =
      std::make_unique<component_updater::MockComponentUpdateService>();

  EXPECT_CALL(*service, RegisterComponent(_));
  RegisterAntiFingerprintingBlockedDomainListComponent(service.get());

  task_env().RunUntilIdle();
}

TEST_F(AntiFingerprintingBlockedDomainListComponentInstallerTest,
       ComponentRegistration_FeatureDisabled) {
  base::test::ScopedFeatureList scoped_disable;
  scoped_disable.InitAndDisableFeature(
      fingerprinting_protection_filter::features::
          kEnableFingerprintingProtectionFilter);

  auto service =
      std::make_unique<component_updater::MockComponentUpdateService>();

  EXPECT_CALL(*service, RegisterComponent(_)).Times(0);
  RegisterAntiFingerprintingBlockedDomainListComponent(service.get());

  task_env().RunUntilIdle();
}

TEST_F(AntiFingerprintingBlockedDomainListComponentInstallerTest,
       LoadRuleset_Empty) {
  base::HistogramTester histogram_tester;
  ASSERT_TRUE(service());
  ASSERT_NO_FATAL_FAILURE(CreateTestRuleset(std::string()));
  ASSERT_NO_FATAL_FAILURE(LoadTestRuleset(
      AntiFingerprintingBlockedDomainListComponentInstallerPolicy::
          kCurrentRulesetFormat));
  histogram_tester.ExpectBucketCount(
      "FingerprintingProtection.BlockedDomainListComponent."
      "InstallationResult",
      static_cast<int>(InstallationResult::kSuccess), 1);
  EXPECT_EQ(kTestRulesetVersion, service()->content_version());
  std::string actual_ruleset_contents;
  ASSERT_TRUE(base::ReadFileToString(service()->ruleset_path(),
                                     &actual_ruleset_contents));
  EXPECT_TRUE(actual_ruleset_contents.empty()) << actual_ruleset_contents;
}

TEST_F(AntiFingerprintingBlockedDomainListComponentInstallerTest,
       LoadRuleset_WithData) {
  base::HistogramTester histogram_tester;
  ASSERT_TRUE(service());
  const std::string expected_ruleset_contents = "foobar";
  ASSERT_NO_FATAL_FAILURE(CreateTestRuleset(expected_ruleset_contents));
  ASSERT_NO_FATAL_FAILURE(LoadTestRuleset(
      AntiFingerprintingBlockedDomainListComponentInstallerPolicy::
          kCurrentRulesetFormat));
  histogram_tester.ExpectBucketCount(
      "FingerprintingProtection.BlockedDomainListComponent."
      "InstallationResult",
      static_cast<int>(InstallationResult::kSuccess), 1);
  EXPECT_EQ(kTestRulesetVersion, service()->content_version());
  std::string actual_ruleset_contents;
  ASSERT_TRUE(base::ReadFileToString(service()->ruleset_path(),
                                     &actual_ruleset_contents));
  EXPECT_EQ(expected_ruleset_contents, actual_ruleset_contents);
}
TEST_F(AntiFingerprintingBlockedDomainListComponentInstallerTest,
       LoadRuleset_WrongFormat) {
  base::HistogramTester histogram_tester;
  ASSERT_TRUE(service());
  const std::string expected_ruleset_contents = "foobar";
  ASSERT_NO_FATAL_FAILURE(CreateTestRuleset(expected_ruleset_contents));
  ASSERT_NO_FATAL_FAILURE(
      ErrorLoadTestRuleset(kInvalidRulesetFormat, base::FilePath()));

  histogram_tester.ExpectBucketCount(
      "FingerprintingProtection.BlockedDomainListComponent."
      "InstallationResult",
      static_cast<int>(InstallationResult::kRulesetFormatError), 1);
  EXPECT_EQ(policy()->GetInstallerAttributes(),
            update_client::InstallerAttributes(
                {{kExperimentalVersionAttributeName, ""}}));
}

TEST_F(AntiFingerprintingBlockedDomainListComponentInstallerTest,
       LoadRuleset_NoRulesetFile) {
  base::HistogramTester histogram_tester;
  ASSERT_TRUE(service());
  const std::string expected_ruleset_contents = "foobar";
  // skip creating file
  ASSERT_NO_FATAL_FAILURE(ErrorLoadTestRuleset(
      AntiFingerprintingBlockedDomainListComponentInstallerPolicy::
          kCurrentRulesetFormat,
      base::FilePath()));

  histogram_tester.ExpectBucketCount(
      "FingerprintingProtection.BlockedDomainListComponent."
      "InstallationResult",
      static_cast<int>(InstallationResult::kMissingBlocklistFileError), 1);
  EXPECT_EQ(policy()->GetInstallerAttributes(),
            update_client::InstallerAttributes(
                {{kExperimentalVersionAttributeName, ""}}));
}

TEST_F(AntiFingerprintingBlockedDomainListComponentInstallerTest,
       InstallerAttributes_FeatureDisabled) {
  base::test::ScopedFeatureList scoped_features;
  scoped_features.InitWithFeatures(
      {}, {fingerprinting_protection_filter::features::
               kEnableFingerprintingProtectionFilter,
           fingerprinting_protection_filter::features::
               kEnableFingerprintingProtectionFilterInIncognito});

  EXPECT_EQ(policy()->GetInstallerAttributes(),
            update_client::InstallerAttributes(
                {{kExperimentalVersionAttributeName, ""}}));
}

TEST_F(AntiFingerprintingBlockedDomainListComponentInstallerTest,
       InstallerAttributes_FeatureEnabledInNonIncognito) {
  base::test::ScopedFeatureList scoped_features;
  scoped_features.InitWithFeaturesAndParameters(
      {{fingerprinting_protection_filter::features::
            kEnableFingerprintingProtectionFilter,
        {{"experimental_version", "test1"}}}},
      {fingerprinting_protection_filter::features::
           kEnableFingerprintingProtectionFilterInIncognito});

  EXPECT_EQ(policy()->GetInstallerAttributes(),
            update_client::InstallerAttributes(
                {{kExperimentalVersionAttributeName, "test1"}}));
}

TEST_F(AntiFingerprintingBlockedDomainListComponentInstallerTest,
       InstallerAttributes_FeatureEnabledFilterInIncognito) {
  base::test::ScopedFeatureList scoped_features;
  scoped_features.InitWithFeaturesAndParameters(
      {{fingerprinting_protection_filter::features::
            kEnableFingerprintingProtectionFilterInIncognito,
        {{"experimental_version", "test2"}}}},
      {fingerprinting_protection_filter::features::
           kEnableFingerprintingProtectionFilter});

  EXPECT_EQ(policy()->GetInstallerAttributes(),
            update_client::InstallerAttributes(
                {{kExperimentalVersionAttributeName, "test2"}}));
}

}  // namespace component_updater
