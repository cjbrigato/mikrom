// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/system/sys_info.h"
#include "base/test/scoped_feature_list.h"
#include "build/build_config.h"
#include "chrome/browser/chrome_content_browser_client.h"
#include "chrome/common/chrome_features.h"
#include "components/site_isolation/features.h"
#include "components/site_isolation/preloaded_isolated_origins.h"
#include "components/site_isolation/site_isolation_policy.h"
#include "content/public/browser/child_process_security_policy.h"
#include "content/public/browser/site_isolation_policy.h"
#include "content/public/common/content_features.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_utils.h"
#include "extensions/buildflags/buildflags.h"
#include "google_apis/gaia/gaia_urls.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(ENABLE_EXTENSIONS_CORE)
#include "extensions/common/extension_urls.h"
#endif

namespace {

// Some command-line switches override field trials - the tests need to be
// skipped in this case.
bool ShouldSkipBecauseOfConflictingCommandLineSwitches() {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kSitePerProcess)) {
    return true;
  }

  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDisableSiteIsolation)) {
    return true;
  }

  return false;
}

}  // namespace

class ChromeSiteIsolationPolicyTest : public testing::Test {
 public:
  ChromeSiteIsolationPolicyTest() = default;

  ChromeSiteIsolationPolicyTest(const ChromeSiteIsolationPolicyTest&) = delete;
  ChromeSiteIsolationPolicyTest& operator=(
      const ChromeSiteIsolationPolicyTest&) = delete;

  void SetUp() override {
    // This way the test always sees the same amount of physical memory
    // (kLowMemoryDeviceThresholdMB = 512MB), regardless of how much memory is
    // available in the testing environment.
    base::CommandLine::ForCurrentProcess()->AppendSwitch(
        switches::kEnableLowEndDeviceMode);
    EXPECT_EQ(512, base::SysInfo::AmountOfPhysicalMemoryMB());

    mode_feature_.InitWithFeatures(
        {features::kSitePerProcess, features::kOriginKeyedProcessesByDefault},
        {});
    site_isolation::SiteIsolationPolicy::
        SetDisallowMemoryThresholdCachingForTesting(true);
  }

  void TearDown() override {
    site_isolation::SiteIsolationPolicy::
        SetDisallowMemoryThresholdCachingForTesting(false);
  }

#if BUILDFLAG(IS_ANDROID)
  // Note that this only sets the memory threshold for strict site isolation.
  void SetMemoryThreshold(const std::string& threshold) {
    threshold_feature_.InitAndEnableFeatureWithParameters(
        site_isolation::features::kSiteIsolationMemoryThresholdsAndroid,
        {{site_isolation::features::
              kStrictSiteIsolationMemoryThresholdParamName,
          threshold}});
  }
#endif  // BUILDFLAG(IS_ANDROID)

  // Note that this only sets the memory threshold for
  // kOriginKeyedProcessesByDefault isolation.
  void SetOriginMemoryThreshold(const std::string& threshold) {
    origin_threshold_feature_.InitAndEnableFeatureWithParameters(
        site_isolation::features::kOriginIsolationMemoryThreshold,
        {{site_isolation::features::kOriginIsolationMemoryThresholdParamName,
          threshold}});
  }

 private:
  content::BrowserTaskEnvironment task_environment_;
  base::test::ScopedFeatureList mode_feature_;
  base::test::ScopedFeatureList threshold_feature_;
  base::test::ScopedFeatureList origin_threshold_feature_;
};

#if BUILDFLAG(IS_ANDROID)
// kSiteIsolationMemoryThresholdsAndroid only affects Android.
// kOriginIsolationMemoryThreshold only affects Desktop.
TEST_F(ChromeSiteIsolationPolicyTest, NoAndroidIsolationBelowMemoryThreshold) {
  if (ShouldSkipBecauseOfConflictingCommandLineSwitches()) {
    return;
  }

  SetMemoryThreshold("768");
  EXPECT_FALSE(
      content::SiteIsolationPolicy::UseDedicatedProcessesForAllSites());
}

TEST_F(ChromeSiteIsolationPolicyTest, AndroidIsolationAboveMemoryThreshold) {
  if (ShouldSkipBecauseOfConflictingCommandLineSwitches()) {
    return;
  }

  SetMemoryThreshold("128");
  EXPECT_TRUE(content::SiteIsolationPolicy::UseDedicatedProcessesForAllSites());
}
#endif  // BUILDFLAG(IS_ANDROID)

TEST_F(ChromeSiteIsolationPolicyTest, NoOriginIsolationBelowMemoryThreshold) {
  if (ShouldSkipBecauseOfConflictingCommandLineSwitches()) {
    GTEST_SKIP();
  }

  SetOriginMemoryThreshold("768");
  EXPECT_FALSE(
      content::SiteIsolationPolicy::AreOriginKeyedProcessesEnabledByDefault());
}

TEST_F(ChromeSiteIsolationPolicyTest, OriginIsolationAboveMemoryThreshold) {
  if (ShouldSkipBecauseOfConflictingCommandLineSwitches()) {
    GTEST_SKIP();
  }

  SetOriginMemoryThreshold("128");
  // Android browser client allows it, but
  // content::SiteIsolationPolicy::UseDedicatedProcessesForAllSites disables it
  // unless --site-per-process is specified. So, for an above-threshold case,
  // UseDedicatedProcessesForAllSites() controls the value of
  // AreOriginKeyedProcessesEnabledByDefault().
  EXPECT_EQ(
      content::SiteIsolationPolicy::UseDedicatedProcessesForAllSites(),
      content::SiteIsolationPolicy::AreOriginKeyedProcessesEnabledByDefault());
}

TEST_F(ChromeSiteIsolationPolicyTest, IsolatedOriginsContainChromeOrigins) {
  if (ShouldSkipBecauseOfConflictingCommandLineSwitches()) {
    return;
  }

  content::SiteIsolationPolicy::ApplyGlobalIsolatedOrigins();

  // On Android official builds, we expect to isolate an additional set of
  // built-in origins.
  std::vector<url::Origin> expected_embedder_origins =
      site_isolation::GetBrowserSpecificBuiltInIsolatedOrigins();

  if (ChromeContentBrowserClient::DoesGaiaOriginRequireDedicatedProcess()) {
    expected_embedder_origins.push_back(GaiaUrls::GetInstance()->gaia_origin());
  }

#if BUILDFLAG(ENABLE_EXTENSIONS_CORE)
  expected_embedder_origins.push_back(
      url::Origin::Create(extension_urls::GetWebstoreLaunchURL()));
  expected_embedder_origins.push_back(
      url::Origin::Create(extension_urls::GetNewWebstoreLaunchURL()));
#endif
  auto* cpsp = content::ChildProcessSecurityPolicy::GetInstance();
  std::vector<url::Origin> isolated_origins = cpsp->GetIsolatedOrigins();
  EXPECT_EQ(expected_embedder_origins.size(), isolated_origins.size());

  // Verify that the expected embedder origins are present even though site
  // isolation has been disabled and the trial origins should not be present.
  EXPECT_THAT(expected_embedder_origins,
              ::testing::UnorderedElementsAreArray(isolated_origins));
}
