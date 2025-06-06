// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/policy/content/policy_blocklist_navigation_throttle.h"

#include <memory>
#include <string>
#include <utility>

#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/values.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/policy/content/policy_blocklist_service.h"
#include "components/policy/content/safe_search_service.h"
#include "components/policy/content/safe_sites_navigation_throttle.h"
#include "components/policy/core/browser/url_blocklist_manager.h"
#include "components/policy/core/browser/url_blocklist_policy_handler.h"
#include "components/policy/core/common/features.h"
#include "components/policy/core/common/policy_pref_names.h"
#include "components/safe_search_api/stub_url_checker.h"
#include "components/safe_search_api/url_checker.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/test_navigation_throttle_inserter.h"
#include "content/public/test/test_renderer_host.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace {

using SafeSitesFilterBehavior = policy::SafeSitesFilterBehavior;

constexpr size_t kCacheSize = 2;

}  // namespace

// TODO(crbug.com/40156526): Break out the tests into separate files. The
// SafeSites tests should be parameterized to run the same tests on both types.
class SafeSitesNavigationThrottleTest
    : public content::RenderViewHostTestHarness {
 public:
  SafeSitesNavigationThrottleTest() = default;
  SafeSitesNavigationThrottleTest(const SafeSitesNavigationThrottleTest&) =
      delete;
  SafeSitesNavigationThrottleTest& operator=(
      const SafeSitesNavigationThrottleTest&) = delete;
  ~SafeSitesNavigationThrottleTest() override = default;

  // content::RenderViewHostTestHarness:
  void SetUp() override {
    content::RenderViewHostTestHarness::SetUp();

    // Prevent crashes in BrowserContextDependencyManager caused when tests
    // that run in serial happen to reuse a memory address for a BrowserContext
    // from a previously-run test.
    // TODO(michaelpg): This shouldn't be the test's responsibility. Can we make
    // BrowserContext just do this always, like Profile does?
    BrowserContextDependencyManager::GetInstance()->MarkBrowserContextLive(
        browser_context());

    SafeSearchFactory::GetInstance()
        ->GetForBrowserContext(browser_context())
        ->SetSafeSearchURLCheckerForTest(
            stub_url_checker().BuildURLChecker(kCacheSize));
  }

  void TearDown() override {
    BrowserContextDependencyManager::GetInstance()
        ->DestroyBrowserContextServices(browser_context());
    content::RenderViewHostTestHarness::TearDown();
  }

 protected:
  std::unique_ptr<content::NavigationSimulator> StartNavigation(
      const GURL& first_url) {
    auto navigation_simulator =
        content::NavigationSimulator::CreateRendererInitiated(first_url,
                                                              main_rfh());
    auto throttle_inserter =
        std::make_unique<content::TestNavigationThrottleInserter>(
            web_contents(),
            base::BindRepeating(
                &SafeSitesNavigationThrottleTest::CreateAndAddThrottle,
                base::Unretained(this)));
    navigation_simulator->SetAutoAdvance(false);
    navigation_simulator->Start();
    return navigation_simulator;
  }

  // Tests that redirects from a safe site to a porn site are handled correctly.
  // Also tests the same scenario when the sites are in the cache.
  // If |expected_error_page_content| is not null, the canceled throttle check
  // result's error_page_content will be expected to match it.
  void TestSafeSitesRedirectAndCachedSites(
      const char* expected_error_page_content,
      bool is_proceed_until_response_enabled = false);

  // Tests responses for both a safe site and a porn site both when the sites
  // are in the cache and not. If |expected_error_page_content| is not null, the
  // canceled throttle check result's error_page_content will be expected to
  // match it.
  void TestSafeSitesCachedSites(const char* expected_error_page_content,
                                bool is_proceed_until_response_enabled = false);

  safe_search_api::StubURLChecker& stub_url_checker() {
    return stub_url_checker_;
  }

 private:
  virtual void CreateAndAddThrottle(content::NavigationThrottleRegistry& registry) {
    registry.AddThrottle(std::make_unique<SafeSitesNavigationThrottle>(
        registry, browser_context()));
  }

  safe_search_api::StubURLChecker stub_url_checker_;
};

class SafeSitesNavigationThrottleWithErrorContentTest
    : public SafeSitesNavigationThrottleTest {
 protected:
  static const char kErrorPageContent[];

  // SafeSitesNavigationThrottleTest:
  void CreateAndAddThrottle(content::NavigationThrottleRegistry& registry) override {
    registry.AddThrottle(std::make_unique<SafeSitesNavigationThrottle>(
        registry, browser_context(), kErrorPageContent));
  }
};

const char
    SafeSitesNavigationThrottleWithErrorContentTest::kErrorPageContent[] =
        "<html><body>URL was filtered.</body></html>";

class PolicyBlocklistNavigationThrottleTest
    : public SafeSitesNavigationThrottleTest,
      public testing::WithParamInterface<bool> {
 public:
  PolicyBlocklistNavigationThrottleTest() {
    if (IsProceedUntilResponseEnabled()) {
      scoped_feature_list_.InitAndEnableFeature(
          policy::features::kPolicyBlocklistProceedUntilResponse);
    } else {
      scoped_feature_list_.InitAndDisableFeature(
          policy::features::kPolicyBlocklistProceedUntilResponse);
    }
  }
  void SetUp() override {
    SafeSitesNavigationThrottleTest::SetUp();

    user_prefs::UserPrefs::Set(browser_context(), &pref_service_);
    policy::URLBlocklistManager::RegisterProfilePrefs(pref_service_.registry());
  }

 protected:
  // SafeSitesNavigationThrottleTest:
  void CreateAndAddThrottle(content::NavigationThrottleRegistry& registry) override {
    registry.AddThrottle(std::make_unique<PolicyBlocklistNavigationThrottle>(
        registry, browser_context()));
  }

  void SetBlocklistUrlPattern(const std::string& pattern) {
    base::Value::List value;
    value.Append(pattern);
    pref_service_.SetManagedPref(policy::policy_prefs::kUrlBlocklist,
                                 std::move(value));
    task_environment()->RunUntilIdle();
  }

  void SetAllowlistUrlPattern(const std::string& pattern) {
    base::Value::List value;
    value.Append(pattern);
    pref_service_.SetManagedPref(policy::policy_prefs::kUrlAllowlist,
                                 std::move(value));
    task_environment()->RunUntilIdle();
  }

  void SetSafeSitesFilterBehavior(SafeSitesFilterBehavior filter_behavior) {
    base::Value value(static_cast<int>(filter_behavior));
    pref_service_.SetManagedPref(policy::policy_prefs::kSafeSitesFilterBehavior,
                                 std::move(value));
  }

  bool IsProceedUntilResponseEnabled() { return GetParam(); }

  sync_preferences::TestingPrefServiceSyncable pref_service_;

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

TEST_P(PolicyBlocklistNavigationThrottleTest, Blocklist) {
  base::HistogramTester histogram_tester;

  SetBlocklistUrlPattern("example.com");

  // Block a blocklisted site.
  auto navigation_simulator = StartNavigation(GURL("http://www.example.com/"));
  ASSERT_FALSE(navigation_simulator->IsDeferred());
  EXPECT_EQ(content::NavigationThrottle::BLOCK_REQUEST,
            navigation_simulator->GetLastThrottleCheckResult());

  // Call WebContents::Stop() to reset the main rfh's navigation state. It
  // results in destructing the navigation throttles to flush metrics.
  RenderViewHostTestHarness::web_contents()->Stop();
}

TEST_P(PolicyBlocklistNavigationThrottleTest, Allowlist) {
  base::HistogramTester histogram_tester;

  SetAllowlistUrlPattern("www.example.com");
  SetBlocklistUrlPattern("example.com");

  // Allow a allowlisted exception to a blocklisted domain.
  auto navigation_simulator = StartNavigation(GURL("http://www.example.com/"));
  ASSERT_FALSE(navigation_simulator->IsDeferred());
  EXPECT_EQ(content::NavigationThrottle::PROCEED,
            navigation_simulator->GetLastThrottleCheckResult());

  // Call WebContents::Stop() to reset the main rfh's navigation state. It
  // results in destructing the navigation throttles to flush metrics.
  RenderViewHostTestHarness::web_contents()->Stop();
}

TEST_P(PolicyBlocklistNavigationThrottleTest, SafeSites_Safe) {
  base::HistogramTester histogram_tester;

  SetSafeSitesFilterBehavior(SafeSitesFilterBehavior::kSafeSitesFilterEnabled);
  stub_url_checker().SetUpValidResponse(false /* is_porn */);

  const GURL url = GURL("http://example.com/");
  auto navigation_simulator = StartNavigation(url);
  if (IsProceedUntilResponseEnabled()) {
    // Proceed with running a background check, will defer on the subsequent
    // redirect event.
    EXPECT_FALSE(navigation_simulator->IsDeferred());
    navigation_simulator->Redirect(url);
  }
  // Defer, then allow a safe site.
  EXPECT_TRUE(navigation_simulator->IsDeferred());
  navigation_simulator->Wait();
  EXPECT_EQ(content::NavigationThrottle::PROCEED,
            navigation_simulator->GetLastThrottleCheckResult());

  // Call WebContents::Stop() to reset the main rfh's navigation state. It
  // results in destructing the navigation throttles to flush metrics.
  RenderViewHostTestHarness::web_contents()->Stop();
}

TEST_P(PolicyBlocklistNavigationThrottleTest, SafeSites_Porn) {
  base::HistogramTester histogram_tester;

  SetSafeSitesFilterBehavior(SafeSitesFilterBehavior::kSafeSitesFilterEnabled);
  stub_url_checker().SetUpValidResponse(true /* is_porn */);

  // Defer, then cancel a porn site.
  const GURL url = GURL("http://example.com/");
  auto navigation_simulator = StartNavigation(url);
  if (IsProceedUntilResponseEnabled()) {
    // Proceed with running a background check, will defer on the subsequent
    // redirect event.
    EXPECT_FALSE(navigation_simulator->IsDeferred());
    navigation_simulator->Redirect(url);
  }
  EXPECT_TRUE(navigation_simulator->IsDeferred());
  navigation_simulator->Wait();
  EXPECT_EQ(content::NavigationThrottle::CANCEL,
            navigation_simulator->GetLastThrottleCheckResult());

  // Call WebContents::Stop() to reset the main rfh's navigation state. It
  // results in destructing the navigation throttles to flush metrics.
  RenderViewHostTestHarness::web_contents()->Stop();
}

TEST_P(PolicyBlocklistNavigationThrottleTest, SafeSites_Allowlisted) {
  SetAllowlistUrlPattern("example.com");
  SetSafeSitesFilterBehavior(SafeSitesFilterBehavior::kSafeSitesFilterEnabled);
  stub_url_checker().SetUpValidResponse(true /* is_porn */);

  // Even with SafeSites enabled, a allowlisted site is immediately allowed.
  auto navigation_simulator = StartNavigation(GURL("http://example.com/"));
  ASSERT_FALSE(navigation_simulator->IsDeferred());
  EXPECT_EQ(content::NavigationThrottle::PROCEED,
            navigation_simulator->GetLastThrottleCheckResult());
}

TEST_P(PolicyBlocklistNavigationThrottleTest, SafeSites_Schemes) {
  SetSafeSitesFilterBehavior(SafeSitesFilterBehavior::kSafeSitesFilterEnabled);
  stub_url_checker().SetUpValidResponse(true /* is_porn */);

  // The safe sites filter is only used for http(s) URLs. This test uses
  // browser-initiated navigation, since renderer-initiated navigations to
  // WebUI documents are not allowed.
  auto navigation_simulator =
      content::NavigationSimulator::CreateBrowserInitiated(
          GURL("chrome://settings"), RenderViewHostTestHarness::web_contents());
  navigation_simulator->SetAutoAdvance(false);
  navigation_simulator->Start();
  ASSERT_FALSE(navigation_simulator->IsDeferred());
  EXPECT_EQ(content::NavigationThrottle::PROCEED,
            navigation_simulator->GetLastThrottleCheckResult());
}

TEST_P(PolicyBlocklistNavigationThrottleTest, SafeSites_PolicyChange) {
  stub_url_checker().SetUpValidResponse(true /* is_porn */);

  // The safe sites filter is initially disabled.
  {
    auto navigation_simulator = StartNavigation(GURL("http://example.com/"));
    ASSERT_FALSE(navigation_simulator->IsDeferred());
    EXPECT_EQ(content::NavigationThrottle::PROCEED,
              navigation_simulator->GetLastThrottleCheckResult());
  }

  // Setting the pref enables the filter.
  SetSafeSitesFilterBehavior(SafeSitesFilterBehavior::kSafeSitesFilterEnabled);
  {
    auto navigation_simulator = StartNavigation(GURL("http://example.com/"));
    if (IsProceedUntilResponseEnabled()) {
      // Proceed with running a background check, will defer on the subsequent
      // response event that happens in the ReadyToCommit.
      EXPECT_FALSE(navigation_simulator->IsDeferred());
      navigation_simulator->ReadyToCommit();
    }
    EXPECT_TRUE(navigation_simulator->IsDeferred());
    navigation_simulator->Wait();
    EXPECT_EQ(content::NavigationThrottle::CANCEL,
              navigation_simulator->GetLastThrottleCheckResult());
  }

  // Updating the pref disables the filter.
  SetSafeSitesFilterBehavior(SafeSitesFilterBehavior::kSafeSitesFilterDisabled);
  {
    auto navigation_simulator = StartNavigation(GURL("http://example.com/"));
    ASSERT_FALSE(navigation_simulator->IsDeferred());
    EXPECT_EQ(content::NavigationThrottle::PROCEED,
              navigation_simulator->GetLastThrottleCheckResult());
  }
}

TEST_P(PolicyBlocklistNavigationThrottleTest, SafeSites_Failure) {
  SetSafeSitesFilterBehavior(SafeSitesFilterBehavior::kSafeSitesFilterEnabled);
  stub_url_checker().SetUpFailedResponse();

  // If the Safe Search API request fails, the navigation is allowed.
  auto navigation_simulator = StartNavigation(GURL("http://example.com/"));
  if (IsProceedUntilResponseEnabled()) {
    // Proceed with running a background check, will defer on the subsequent
    // response event that happens in the ReadyToCommit.
    EXPECT_FALSE(navigation_simulator->IsDeferred());
    navigation_simulator->ReadyToCommit();
  }
  EXPECT_TRUE(navigation_simulator->IsDeferred());
  navigation_simulator->Wait();
  EXPECT_EQ(content::NavigationThrottle::PROCEED,
            navigation_simulator->GetLastThrottleCheckResult());
}

// Run all SafeSitesNavigationThrottle tests with and without the
// kPolicyBlocklistProceedUntilResponse feature enabled.
INSTANTIATE_TEST_SUITE_P(All,
                         PolicyBlocklistNavigationThrottleTest,
                         testing::Values(false, true));

void SafeSitesNavigationThrottleTest::TestSafeSitesCachedSites(
    const char* expected_error_page_content,
    bool is_proceed_until_response_enabled) {
  // Check a couple of sites.
  ASSERT_EQ(2u, kCacheSize);
  const GURL safe_site = GURL("http://example.com/");
  const GURL porn_site = GURL("http://example2.com/");

  stub_url_checker().SetUpValidResponse(false /* is_porn */);
  {
    auto navigation_simulator = StartNavigation(safe_site);
    if (is_proceed_until_response_enabled) {
      // Proceed with running a background check, will defer on the subsequent
      // response event that happens in the ReadyToCommit.
      EXPECT_FALSE(navigation_simulator->IsDeferred());
      navigation_simulator->ReadyToCommit();
    }
    EXPECT_TRUE(navigation_simulator->IsDeferred());
    navigation_simulator->Wait();
    EXPECT_EQ(content::NavigationThrottle::PROCEED,
              navigation_simulator->GetLastThrottleCheckResult());
    EXPECT_FALSE(navigation_simulator->GetLastThrottleCheckResult()
                     .error_page_content());
  }

  stub_url_checker().SetUpValidResponse(true /* is_porn */);
  {
    auto navigation_simulator = StartNavigation(porn_site);
    if (is_proceed_until_response_enabled) {
      // Proceed with running a background check, will defer on the subsequent
      // response event that happens in the ReadyToCommit.
      EXPECT_FALSE(navigation_simulator->IsDeferred());
      navigation_simulator->ReadyToCommit();
    }
    EXPECT_TRUE(navigation_simulator->IsDeferred());
    navigation_simulator->Wait();
    EXPECT_EQ(content::NavigationThrottle::CANCEL,
              navigation_simulator->GetLastThrottleCheckResult());
    if (expected_error_page_content) {
      EXPECT_STREQ(expected_error_page_content,
                   navigation_simulator->GetLastThrottleCheckResult()
                       .error_page_content()
                       ->c_str());
    } else {
      EXPECT_FALSE(navigation_simulator->GetLastThrottleCheckResult()
                       .error_page_content());
    }
  }

  stub_url_checker().ClearResponses();
  {
    // This check is synchronous since the site is in the cache.
    auto navigation_simulator = StartNavigation(safe_site);
    ASSERT_FALSE(navigation_simulator->IsDeferred());
    EXPECT_EQ(content::NavigationThrottle::PROCEED,
              navigation_simulator->GetLastThrottleCheckResult());
    EXPECT_FALSE(navigation_simulator->GetLastThrottleCheckResult()
                     .error_page_content());
  }

  {
    // This check is synchronous since the site is in the cache.
    auto navigation_simulator = StartNavigation(porn_site);
    ASSERT_FALSE(navigation_simulator->IsDeferred());
    EXPECT_EQ(content::NavigationThrottle::CANCEL,
              navigation_simulator->GetLastThrottleCheckResult());
    if (expected_error_page_content) {
      EXPECT_STREQ(expected_error_page_content,
                   navigation_simulator->GetLastThrottleCheckResult()
                       .error_page_content()
                       ->c_str());
    } else {
      EXPECT_FALSE(navigation_simulator->GetLastThrottleCheckResult()
                       .error_page_content());
    }
  }
}

TEST_F(SafeSitesNavigationThrottleTest, SafeSites_CachedSites) {
  TestSafeSitesCachedSites(nullptr);
}

TEST_F(SafeSitesNavigationThrottleWithErrorContentTest, SafeSites_CachedSites) {
  TestSafeSitesCachedSites(&kErrorPageContent[0]);
}

TEST_P(PolicyBlocklistNavigationThrottleTest, SafeSites_CachedSites) {
  SetSafeSitesFilterBehavior(SafeSitesFilterBehavior::kSafeSitesFilterEnabled);
  TestSafeSitesCachedSites(nullptr, IsProceedUntilResponseEnabled());
}

void SafeSitesNavigationThrottleTest::TestSafeSitesRedirectAndCachedSites(
    const char* expected_error_page_content,
    bool is_proceed_until_response_enabled) {
  // Check a couple of sites.
  ASSERT_EQ(2u, kCacheSize);
  const GURL safe_site = GURL("http://example.com/");
  const GURL porn_site = GURL("http://example2.com/");

  stub_url_checker().SetUpValidResponse(false /* is_porn */);
  {
    auto navigation_simulator = StartNavigation(safe_site);
    if (is_proceed_until_response_enabled) {
      // Proceed with running a background check, will defer on the subsequent
      // redirect event.
      EXPECT_FALSE(navigation_simulator->IsDeferred());
      navigation_simulator->Redirect(safe_site);
    }
    EXPECT_TRUE(navigation_simulator->IsDeferred());
    navigation_simulator->Wait();
    EXPECT_EQ(content::NavigationThrottle::PROCEED,
              navigation_simulator->GetLastThrottleCheckResult());
    EXPECT_FALSE(navigation_simulator->GetLastThrottleCheckResult()
                     .error_page_content());

    stub_url_checker().SetUpValidResponse(true /* is_porn */);
    navigation_simulator->Redirect(porn_site);
    if (is_proceed_until_response_enabled) {
      // Proceed with running a background check, will defer on the subsequent
      // response event that happens in the ReadyToCommit.
      EXPECT_FALSE(navigation_simulator->IsDeferred());
      navigation_simulator->ReadyToCommit();
    }
    EXPECT_TRUE(navigation_simulator->IsDeferred());
    navigation_simulator->Wait();
    EXPECT_EQ(content::NavigationThrottle::CANCEL,
              navigation_simulator->GetLastThrottleCheckResult());
    if (expected_error_page_content) {
      EXPECT_STREQ(expected_error_page_content,
                   navigation_simulator->GetLastThrottleCheckResult()
                       .error_page_content()
                       ->c_str());
    } else {
      EXPECT_FALSE(navigation_simulator->GetLastThrottleCheckResult()
                       .error_page_content());
    }
  }

  stub_url_checker().ClearResponses();
  {
    // This check is synchronous since the site is in the cache.
    auto navigation_simulator = StartNavigation(safe_site);
    ASSERT_FALSE(navigation_simulator->IsDeferred());
    EXPECT_EQ(content::NavigationThrottle::PROCEED,
              navigation_simulator->GetLastThrottleCheckResult());
    EXPECT_FALSE(navigation_simulator->GetLastThrottleCheckResult()
                     .error_page_content());

    navigation_simulator->Redirect(porn_site);
    ASSERT_FALSE(navigation_simulator->IsDeferred());
    EXPECT_EQ(content::NavigationThrottle::CANCEL,
              navigation_simulator->GetLastThrottleCheckResult());
    if (expected_error_page_content) {
      EXPECT_STREQ(expected_error_page_content,
                   navigation_simulator->GetLastThrottleCheckResult()
                       .error_page_content()
                       ->c_str());
    } else {
      EXPECT_FALSE(navigation_simulator->GetLastThrottleCheckResult()
                       .error_page_content());
    }
  }
}

TEST_F(SafeSitesNavigationThrottleTest, SafeSites_RedirectAndCachedSites) {
  TestSafeSitesRedirectAndCachedSites(nullptr);
}

TEST_F(SafeSitesNavigationThrottleWithErrorContentTest,
       SafeSites_RedirectAndCachedSites) {
  TestSafeSitesRedirectAndCachedSites(&kErrorPageContent[0]);
}

TEST_P(PolicyBlocklistNavigationThrottleTest,
       SafeSites_RedirectAndCachedSites) {
  SetSafeSitesFilterBehavior(SafeSitesFilterBehavior::kSafeSitesFilterEnabled);

  TestSafeSitesRedirectAndCachedSites(nullptr, IsProceedUntilResponseEnabled());
}

#if BUILDFLAG(IS_CHROMEOS)
TEST_P(PolicyBlocklistNavigationThrottleTest, UseVpnPreConnectFiltering) {
  SetBlocklistUrlPattern("block-by-general-pref.com");
  base::Value::List list;
  list.Append("allowed-preconnect.com");
  pref_service_.SetManagedPref(
      policy::policy_prefs::kAlwaysOnVpnPreConnectUrlAllowlist,
      base::Value(std::move(list)));

  PolicyBlocklistService* service =
      PolicyBlocklistFactory::GetForBrowserContext(browser_context());
  service->SetAlwaysOnVpnPreConnectUrlAllowlistEnforced(
      /*enforced=*/true);

  task_environment()->RunUntilIdle();

  // Verify that the pref kAlwaysOnVpnPreConnectUrlAllowlist is enforced
  // while kEnforceAlwaysOnVpnPreConnectUrlAllowlist is true.
  auto navigation_simulator =
      StartNavigation(GURL("http://allowed-preconnect.com/"));
  ASSERT_FALSE(navigation_simulator->IsDeferred());
  EXPECT_EQ(content::NavigationThrottle::PROCEED,
            navigation_simulator->GetLastThrottleCheckResult());
  navigation_simulator =
      StartNavigation(GURL("http://neural-by-general-pref.com/"));
  EXPECT_EQ(content::NavigationThrottle::BLOCK_REQUEST,
            navigation_simulator->GetLastThrottleCheckResult());

  service->SetAlwaysOnVpnPreConnectUrlAllowlistEnforced(
      /*enforced=*/false);

  task_environment()->RunUntilIdle();

  navigation_simulator =
      StartNavigation(GURL("http://block-by-general-pref.com/"));
  ASSERT_FALSE(navigation_simulator->IsDeferred());
  EXPECT_EQ(content::NavigationThrottle::BLOCK_REQUEST,
            navigation_simulator->GetLastThrottleCheckResult());
  navigation_simulator =
      StartNavigation(GURL("http://neural-by-general-pref.com/"));
  EXPECT_EQ(content::NavigationThrottle::PROCEED,
            navigation_simulator->GetLastThrottleCheckResult());
}
#endif
