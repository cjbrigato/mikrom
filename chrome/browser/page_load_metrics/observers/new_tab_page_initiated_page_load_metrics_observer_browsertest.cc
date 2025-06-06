// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/page_load_metrics/observers/new_tab_page_initiated_page_load_metrics_observer.h"

#include "base/test/metrics/histogram_tester.h"
#include "build/build_config.h"
#include "chrome/browser/preloading/chrome_preloading.h"
#include "chrome/browser/preloading/prerender/prerender_manager.h"
#include "chrome/browser/preloading/prerender/prerender_utils.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/chrome_test_utils.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/preloading_test_util.h"
#include "content/public/test/prerender_test_util.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"

class NewTabPageInitiatedPageLoadMetricsBrowserTest
    : public InProcessBrowserTest {
 public:
  NewTabPageInitiatedPageLoadMetricsBrowserTest()
      : prerender_helper_(
            base::BindRepeating(&NewTabPageInitiatedPageLoadMetricsBrowserTest::
                                    GetActiveWebContents,
                                base::Unretained(this))) {}

  content::WebContents* GetActiveWebContents() {
    return chrome_test_utils::GetActiveWebContents(this);
  }

  content::test::PrerenderTestHelper& prerender_helper() {
    return prerender_helper_;
  }

 private:
  void SetUp() override {
    prerender_helper_.RegisterServerRequestMonitor(embedded_test_server());
    InProcessBrowserTest::SetUp();
  }

  void SetUpOnMainThread() override {
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  content::test::PrerenderTestHelper prerender_helper_;
};

// This test confirms that non-NewTabPage triggers don't record metrics for
// NewTabPage. This test uses Omnibox trigger as a non-NewTabPage trigger,.
IN_PROC_BROWSER_TEST_F(NewTabPageInitiatedPageLoadMetricsBrowserTest,
                       NonNewTabPagePrerenderTriggeredByEmbedderAndActivate) {
  base::HistogramTester histogram_tester;

  // Navigate to an initial page.
  GURL url = embedded_test_server()->GetURL("/empty.html");
  ASSERT_TRUE(content::NavigateToURL(GetActiveWebContents(), url));

  GURL prerender_url = embedded_test_server()->GetURL("/simple.html");

  // Start Omnibox triggered prerendering.
  auto* preloading_data = content::PreloadingData::GetOrCreateForWebContents(
      GetActiveWebContents());
  content::PreloadingURLMatchCallback same_url_matcher =
      content::PreloadingData::GetSameURLMatcher(prerender_url);
  content::PreloadingAttempt* preloading_attempt =
      preloading_data->AddPreloadingAttempt(
          chrome_preloading_predictor::kOmniboxDirectURLInput,
          content::PreloadingType::kPrerender, same_url_matcher,
          GetActiveWebContents()->GetPrimaryMainFrame()->GetPageUkmSourceId());
  PrerenderManager::CreateForWebContents(GetActiveWebContents());
  base::WeakPtr<content::PrerenderHandle> prerender_handle =
      PrerenderManager::FromWebContents(GetActiveWebContents())
          ->StartPrerenderDirectUrlInput(prerender_url, *preloading_attempt);
  EXPECT_TRUE(prerender_handle);
  content::test::PrerenderTestHelper::WaitForPrerenderLoadCompletion(
      *GetActiveWebContents(), prerender_url);

  // Activate.
  content::TestActivationManager activation_manager(GetActiveWebContents(),
                                                    prerender_url);
  // Simulate an Omnibox triggered navigation.
  prerender_helper().NavigatePrimaryPageAsync(
      prerender_url,
      ui::PageTransitionFromInt(ui::PAGE_TRANSITION_TYPED |
                                ui::PAGE_TRANSITION_FROM_ADDRESS_BAR));
  activation_manager.WaitForNavigationFinished();
  EXPECT_TRUE(activation_manager.was_activated());
  histogram_tester.ExpectUniqueSample(
      "Prerender.Experimental.PrerenderHostFinalStatus.Embedder_DirectURLInput",
      /*kFinalStatusActivated*/ 0, 1);

  // Navigate away to flush the metrics and check.
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL)));
  histogram_tester.ExpectTotalCount(
      "NewTabPage.PrerenderNavigationToActivation", 0);
}
