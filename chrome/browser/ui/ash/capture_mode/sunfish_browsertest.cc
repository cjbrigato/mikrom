// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "ash/capture_mode/capture_mode_controller.h"
#include "ash/capture_mode/capture_mode_test_util.h"
#include "ash/capture_mode/capture_mode_types.h"
#include "ash/capture_mode/search_results_panel.h"
#include "ash/constants/ash_features.h"
#include "ash/constants/ash_switches.h"
#include "ash/public/cpp/ash_web_view.h"
#include "ash/public/cpp/capture_mode/capture_mode_api.h"
#include "base/functional/callback_helpers.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/ui/ash/capture_mode/chrome_capture_mode_delegate.h"
#include "chrome/browser/ui/ash/capture_mode/search_results_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/lens/lens_overlay_permission_utils.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/common/referrer.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_navigation_observer.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/page_transition_types.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace {

constexpr char kSearchResultClickedHistogram[] =
    "Ash.CaptureModeController.SearchResultClicked.ClamshellMode";

std::unique_ptr<views::Widget> CreateWidget() {
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  return std::make_unique<views::Widget>(std::move(params));
}

// Given an embedded script `body`, wraps it in a basic HTML structure, then
// returns a GURL accessible by the local test server.
GURL CreateDataUrlWithBody(const std::string& body) {
  return GURL(base::StringPrintf(R"(data:text/html,
      <html>
        <body>
          <style>
            * {
              margin: 0;
              padding: 0;
            }
          </style>
          %s
        </body>
      </html>
    )",
                                 body.c_str()));
}

}  // namespace

namespace ash {

class SunfishBrowserTest : public InProcessBrowserTest {
 public:
  SunfishBrowserTest() = default;
  SunfishBrowserTest(const SunfishBrowserTest&) = delete;
  SunfishBrowserTest& operator=(const SunfishBrowserTest&) = delete;
  ~SunfishBrowserTest() override = default;

 private:
  base::test::ScopedFeatureList scoped_feature_list_{features::kSunfishFeature};
};

// Tests the basic functionalities of `SearchResultsView`.
IN_PROC_BROWSER_TEST_F(SunfishBrowserTest, SearchResultsView) {
  auto widget = CreateWidget();
  ChromeCaptureModeDelegate* delegate = ChromeCaptureModeDelegate::Get();
  auto* contents_view =
      widget->SetContentsView(delegate->CreateSearchResultsView());
  auto* search_results_view =
      views::AsViewClass<SearchResultsView>(contents_view);
  ASSERT_TRUE(search_results_view);
}

// Tests that links are opened in new tabs.
IN_PROC_BROWSER_TEST_F(SunfishBrowserTest, OpenLinksInNewTabs) {
  base::HistogramTester histogram_tester;

  histogram_tester.ExpectTotalCount(kSearchResultClickedHistogram, 0);

  auto* controller = CaptureModeController::Get();
  controller->StartSunfishSession();
  VerifyActiveBehavior(BehaviorType::kSunfish);

  // Simulate showing the panel while the session is active.
  controller->ShowSearchResultsPanel();
  controller->NavigateSearchResultsPanel(GURL("kTestUrl1"));
  ASSERT_TRUE(controller->IsActive());
  auto* search_results_view =
      controller->GetSearchResultsPanel()->search_results_view();

  // Browser tests start out with 1 browser tab by default.
  EXPECT_EQ(1, browser()->tab_strip_model()->count());

  // Simulate clicking on a new URL in the web view.
  auto* child_view =
      search_results_view->GetViewByID(kAshWebViewChildWebViewId);
  ASSERT_TRUE(child_view);
  auto* web_view = views::AsViewClass<views::WebView>(child_view);
  ASSERT_TRUE(web_view);
  auto* web_contents = web_view->web_contents();
  web_contents->GetController().LoadURL(
      CreateDataUrlWithBody(R"(
      <script>
        // Wait until window has finished loading.
        window.addEventListener("load", () => {

          // Perform simple click on an anchor within the same target.
          const anchor = document.createElement("a");
          anchor.href = "https://google.com/";
          anchor.click();

          // Wait for first click event to be flushed.
          setTimeout(() => {

            // Perform simple click on an anchor with "_blank" target.
            const anchor = document.createElement("a");
            anchor.href = "https://assistant.google.com/";
            anchor.target = "_blank";
            anchor.click();
          }, 0);
        });
      </script>
    )"),
      content::Referrer(), ui::PAGE_TRANSITION_TYPED, std::string());
  content::TestNavigationObserver observer(web_contents);
  observer.Wait();
  EXPECT_TRUE(observer.last_navigation_succeeded());

  // Test it opens a new tab and ends the session.
  EXPECT_EQ(2, browser()->tab_strip_model()->count());
  EXPECT_FALSE(controller->IsActive());
  histogram_tester.ExpectTotalCount(kSearchResultClickedHistogram, 1);
}

// Tests that links are correctly opened in off the record profiles.
IN_PROC_BROWSER_TEST_F(SunfishBrowserTest, OpensLinksOffTheRecord) {
  base::HistogramTester histogram_tester;
  histogram_tester.ExpectTotalCount(kSearchResultClickedHistogram, 0);

  auto* controller = CaptureModeController::Get();
  controller->StartSunfishSession();
  VerifyActiveBehavior(BehaviorType::kSunfish);

  // Simulate showing the panel while the session is active.
  controller->ShowSearchResultsPanel();
  controller->NavigateSearchResultsPanel(GURL("kTestUrl1"));
  ASSERT_TRUE(controller->IsActive());
  auto* search_results_view =
      controller->GetSearchResultsPanel()->search_results_view();

  // Simulate clicking on a new URL in the web view.
  views::View* child_view =
      search_results_view->GetViewByID(kAshWebViewChildWebViewId);
  ASSERT_TRUE(child_view);
  auto* web_view = views::AsViewClass<views::WebView>(child_view);
  ASSERT_TRUE(web_view);
  content::WebContents* web_contents = web_view->web_contents();
  ASSERT_TRUE(web_contents);
  content::OpenURLParams params(
      GURL("https://assistant.google.com"), content::Referrer(),
      WindowOpenDisposition::OFF_THE_RECORD, ui::PAGE_TRANSITION_LINK,
      /*is_renderer_initiated=*/false);
  content::WebContents* new_contents =
      web_contents->OpenURL(params,
                            /*navigation_handle_callback=*/base::DoNothing());

  Profile* profile =
      Profile::FromBrowserContext(new_contents->GetBrowserContext());
  EXPECT_TRUE(profile->IsOffTheRecord());
  histogram_tester.ExpectTotalCount(kSearchResultClickedHistogram, 1);
}

IN_PROC_BROWSER_TEST_F(SunfishBrowserTest, SendSearchRequests) {
  // Send a region search, simulated inside a regular capture session.
  ChromeCaptureModeDelegate* delegate = ChromeCaptureModeDelegate::Get();
  delegate->SendLensWebRegionSearch(gfx::Image(),
                                    /*is_standalone_session=*/false,
                                    base::BindRepeating([](GURL url) {}),
                                    base::DoNothing(), base::DoNothing());

  // Send a region search, simulated inside a standalone Sunfish session.
  delegate->SendLensWebRegionSearch(gfx::Image(),
                                    /*is_standalone_session=*/true,
                                    base::BindRepeating([](GURL url) {}),
                                    base::DoNothing(), base::DoNothing());
}

// Tests that the policy can be read and set browser-side.
IN_PROC_BROWSER_TEST_F(SunfishBrowserTest, BrowserPolicy) {
  ChromeCaptureModeDelegate* delegate = ChromeCaptureModeDelegate::Get();
  EXPECT_TRUE(delegate->IsSearchAllowedByPolicy());
  EXPECT_TRUE(CanShowSunfishUi());

  browser()->profile()->GetPrefs()->SetInteger(
      lens::prefs::kLensOverlaySettings,
      static_cast<int>(lens::prefs::LensOverlaySettingsPolicyValue::kDisabled));
  EXPECT_FALSE(delegate->IsSearchAllowedByPolicy());
  EXPECT_FALSE(CanShowSunfishUi());
}

}  // namespace ash
