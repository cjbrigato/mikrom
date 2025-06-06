// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/sessions/sync_sessions_router_tab_helper.h"

#include <memory>
#include <set>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/sync/sessions/sync_sessions_web_contents_router.h"
#include "chrome/browser/sync/sessions/sync_sessions_web_contents_router_factory.h"
#include "chrome/browser/ui/sync/browser_synced_tab_delegate.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/testing_profile.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/mock_navigation_handle.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace sync_sessions {

class FakeLocalSessionEventHandler : public LocalSessionEventHandler {
 public:
  void OnSessionRestoreComplete() override {}

  void OnLocalTabModified(SyncedTabDelegate* modified_tab) override {
    was_notified_ = true;
  }

  void OnLocalTabClosed() override { was_notified_ = true; }

  bool was_notified_since_last_call() {
    bool was_notified = was_notified_;
    was_notified_ = false;
    return was_notified;
  }

 private:
  bool was_notified_ = false;
};

class SyncSessionsRouterTabHelperTest : public ChromeRenderViewHostTestHarness {
 public:
  SyncSessionsRouterTabHelperTest() = default;
  ~SyncSessionsRouterTabHelperTest() override = default;

  void SetUp() override {
    ChromeRenderViewHostTestHarness::SetUp();
    content::RenderFrameHostTester::For(main_rfh())
        ->InitializeRenderFrameIfNeeded();
    router_ =
        SyncSessionsWebContentsRouterFactory::GetInstance()->GetForProfile(
            profile());
    sync_sessions_router_ =
        std::make_unique<sync_sessions::SyncSessionsRouterTabHelper>(
            web_contents(), router(),
            ChromeTranslateClient::FromWebContents(web_contents()),
            favicon::ContentFaviconDriver::FromWebContents(web_contents()));
    router_->StartRoutingTo(handler());

    BrowserSyncedTabDelegate::CreateForWebContents(web_contents());
    NavigateAndCommit(GURL("about:blank"));
  }

  void TearDown() override {
    sync_sessions_router_.reset();
    router_ = nullptr;
    ChromeRenderViewHostTestHarness::TearDown();
  }

  SyncSessionsWebContentsRouter* router() { return router_; }
  FakeLocalSessionEventHandler* handler() { return &handler_; }
  SyncSessionsRouterTabHelper* router_tab_helper() {
    return sync_sessions_router_.get();
  }

 private:
  raw_ptr<SyncSessionsWebContentsRouter> router_ = nullptr;
  FakeLocalSessionEventHandler handler_;
  std::unique_ptr<SyncSessionsRouterTabHelper> sync_sessions_router_;
};

TEST_F(SyncSessionsRouterTabHelperTest, SubframeNavigationsIgnored) {
  SyncSessionsRouterTabHelper* helper = router_tab_helper();

  ASSERT_TRUE(handler()->was_notified_since_last_call());

  content::RenderFrameHost* child_rfh =
      content::RenderFrameHostTester::For(main_rfh())->AppendChild("subframe");
  GURL child_url("http://foobar.com");

  content::MockNavigationHandle test_handle(child_url, child_rfh);
  helper->DidFinishNavigation(&test_handle);
  EXPECT_FALSE(handler()->was_notified_since_last_call());

  helper->DidFinishLoad(child_rfh, GURL());
  EXPECT_FALSE(handler()->was_notified_since_last_call());
}

}  // namespace sync_sessions
