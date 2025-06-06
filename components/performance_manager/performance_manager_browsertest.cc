// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/performance_manager/public/performance_manager.h"

#include <utility>

#include "base/run_loop.h"
#include "base/test/bind.h"
#include "components/performance_manager/graph/frame_node_impl.h"
#include "components/performance_manager/performance_manager_tab_helper.h"
#include "components/performance_manager/public/graph/frame_node.h"
#include "components/performance_manager/public/graph/graph.h"
#include "components/performance_manager/public/graph/page_node.h"
#include "components/performance_manager/public/render_frame_host_proxy.h"
#include "components/performance_manager/test_support/performance_manager_browsertest_harness.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/fenced_frame_test_util.h"
#include "content/public/test/url_loader_interceptor.h"
#include "content/shell/browser/shell.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/features.h"
#include "url/origin.h"

namespace performance_manager {

using PerformanceManagerBrowserTest = PerformanceManagerBrowserTestHarness;

// A full browser test is required for this because we need RenderFrameHosts
// to be created.
IN_PROC_BROWSER_TEST_F(PerformanceManagerBrowserTest,
                       GetFrameNodeForRenderFrameHost) {
  // NavigateToURL blocks until the load has completed. We assert that the
  // contents has been reused as we don't have general WebContents creation
  // hooks in our BrowserTest fixture, and if a new contents was created it
  // would be missing the PM tab helper.
  auto* old_contents = shell()->web_contents();
  static const char kUrl[] = "about:blank";
  ASSERT_TRUE(NavigateToURL(shell(), GURL(kUrl)));
  content::WebContents* contents = shell()->web_contents();
  ASSERT_EQ(contents, old_contents);
  ASSERT_EQ(contents->GetLastCommittedURL().possibly_invalid_spec(), kUrl);
  content::RenderFrameHost* rfh = contents->GetPrimaryMainFrame();
  ASSERT_TRUE(rfh->IsRenderFrameLive());

  base::WeakPtr<FrameNode> frame_node =
      PerformanceManager::GetFrameNodeForRenderFrameHost(rfh);

  const RenderFrameHostProxy& rfh_proxy = frame_node->GetRenderFrameHostProxy();
  EXPECT_EQ(rfh, rfh_proxy.Get());

  // This closes the window, and destroys the underlying WebContents.
  shell()->Close();
  contents = nullptr;

  // After deleting |contents| the corresponding FrameNode WeakPtr should be
  // invalid.
  EXPECT_FALSE(frame_node);
}

IN_PROC_BROWSER_TEST_F(PerformanceManagerBrowserTest, OpenerTrackingWorks) {
  // Load a page that will load a popup.
  GURL url(embedded_test_server()->GetURL("a.com", "/a_popup_a.html"));
  content::ShellAddedObserver shell_added_observer;
  ASSERT_TRUE(NavigateToURL(shell(), url));

  // Wait for the popup window to appear, and then wait for it to load.
  auto* popup = shell_added_observer.GetShell();
  ASSERT_TRUE(popup);
  WaitForLoad(popup->web_contents());

  auto* contents = shell()->web_contents();

  // Make sure everything is connected as expected in the graph.
  auto page = PerformanceManager::GetPrimaryPageNodeForWebContents(contents);
  EXPECT_TRUE(page);
  auto* frame = page->GetMainFrameNode();
  EXPECT_EQ(1u, frame->GetOpenedPageNodes().size());
  auto* embedded_page = *(frame->GetOpenedPageNodes().begin());
  EXPECT_EQ(frame, embedded_page->GetOpenerFrameNode());
}

namespace {

class WebRTCUsageChangeWaiter : public PageNodeObserver {
 public:
  WebRTCUsageChangeWaiter() = default;

  void WaitForWebRTCUsageChange() { run_loop_.Run(); }

  // PageNodeObserver:
  void OnPageUsesWebRTCChanged(const PageNode* page_node) override {
    run_loop_.Quit();
  }

 private:
  base::RunLoop run_loop_;
};

}  // namespace

// Integration test for WebRTC usage tracking on PageNode and FrameNode.
IN_PROC_BROWSER_TEST_F(PerformanceManagerBrowserTest, UsesWebRTC) {
  Graph* graph = PerformanceManager::GetGraph();

  WebRTCUsageChangeWaiter waiter;
  graph->AddPageNodeObserver(&waiter);

  GURL url(embedded_test_server()->GetURL("a.com", "/webrtc_basic.html"));
  content::ShellAddedObserver shell_added_observer;
  ASSERT_TRUE(NavigateToURL(shell(), url));

  auto* contents = shell()->web_contents();
  auto page = PerformanceManager::GetPrimaryPageNodeForWebContents(contents);

  waiter.WaitForWebRTCUsageChange();

  EXPECT_TRUE(page->UsesWebRTC());
  EXPECT_TRUE(page->GetMainFrameNode()->UsesWebRTC());

  graph->RemovePageNodeObserver(&waiter);
}

namespace {

class FreezingOriginTrialOptOutWaiter : public PageNodeObserver {
 public:
  FreezingOriginTrialOptOutWaiter() = default;

  void WaitForFreezingOriginTrialOptOut() { run_loop_.Run(); }

  // PageNodeObserver:
  void OnPageHasFreezingOriginTrialOptOutChanged(
      const PageNode* page_node) override {
    run_loop_.Quit();
  }

 private:
  base::RunLoop run_loop_;
};

}  // namespace

// Integration test for freezing origin trial opt-out tracking on PageNode and
// FrameNode.
//
// This test uses `content::URLLoaderInterceptor` instead of
// `net::test::EmbeddedTestServer` to control the port on which the test data is
// served, which is required because an Origin Trial token is tied to a specific
// origin (including port).
IN_PROC_BROWSER_TEST_F(PerformanceManagerBrowserTest,
                       HasFreezingOriginTrialOptOut) {
  Graph* graph = PerformanceManager::GetGraph();

  FreezingOriginTrialOptOutWaiter waiter;
  graph->AddPageNodeObserver(&waiter);

  const GURL kHost("https://example.test");
  auto interceptor =
      content::URLLoaderInterceptor::ServeFilesFromDirectoryAtOrigin(
          "components/test/data/performance_manager",
          GURL("https://example.test"));
  ASSERT_TRUE(
      NavigateToURL(shell(), kHost.Resolve("page_freeze_ot_opt_out.html")));

  auto* contents = shell()->web_contents();
  auto page = PerformanceManager::GetPrimaryPageNodeForWebContents(contents);

  waiter.WaitForFreezingOriginTrialOptOut();

  EXPECT_TRUE(page->HasFreezingOriginTrialOptOut());
  EXPECT_TRUE(page->GetMainFrameNode()->HasFreezingOriginTrialOptOut());

  graph->RemovePageNodeObserver(&waiter);
}

namespace {

MATCHER_P(IsOpaqueDerivedFrom,
          base_origin,
          "is opaque derived from " + base_origin.GetDebugString()) {
  return arg.has_value() && arg->opaque() &&
         arg->GetTupleOrPrecursorTupleIfOpaque() ==
             base_origin.GetTupleOrPrecursorTupleIfOpaque();
}

}  // namespace

IN_PROC_BROWSER_TEST_F(PerformanceManagerBrowserTest, OriginAboutBlankFrame) {
  // Load a page with:
  // - A regular frame navigated to about:blank.
  // - A sandboxed frame navigated to about:blank.
  GURL main_frame_url(
      embedded_test_server()->GetURL("a.com", "/about_blank_iframes.html"));
  const url::Origin main_frame_origin = url::Origin::Create(main_frame_url);
  ASSERT_TRUE(NavigateToURL(shell(), main_frame_url));

  auto* contents = shell()->web_contents();
  auto page = PerformanceManager::GetPrimaryPageNodeForWebContents(contents);

  // Make sure everything is connected as expected in the graph.
  EXPECT_TRUE(page);

  // Verify that the regular frame has the same origin as its parent whereas
  // the sandboxed frame has an opaque origin derived from it, as assumed by
  // Resource Attribution.
  std::vector<std::optional<url::Origin>> child_frame_origins;
  for (const FrameNode* node : page->GetMainFrameNode()->GetChildFrameNodes()) {
    child_frame_origins.push_back(node->GetOrigin());
  }
  EXPECT_THAT(child_frame_origins,
              testing::UnorderedElementsAre(
                  main_frame_origin, IsOpaqueDerivedFrom(main_frame_origin)));
}

class PerformanceManagerFencedFrameBrowserTest
    : public PerformanceManagerBrowserTest {
 public:
  PerformanceManagerFencedFrameBrowserTest() = default;
  ~PerformanceManagerFencedFrameBrowserTest() override = default;

  content::test::FencedFrameTestHelper& fenced_frame_test_helper() {
    return fenced_frame_helper_;
  }

  content::WebContents* GetWebContents() { return shell()->web_contents(); }

 private:
  content::test::FencedFrameTestHelper fenced_frame_helper_;
};

IN_PROC_BROWSER_TEST_F(PerformanceManagerFencedFrameBrowserTest,
                       NoParentFrameNode) {
  auto initial_url = embedded_test_server()->GetURL("/empty.html");
  ASSERT_TRUE(NavigateToURL(shell(), initial_url));

  PerformanceManagerTabHelper* tab_helper =
      PerformanceManagerTabHelper::FromWebContents(GetWebContents());
  ASSERT_TRUE(tab_helper);

  // Main frame, which is going to be the fenced frame's outer document.
  content::RenderFrameHost* main_frame_host =
      GetWebContents()->GetPrimaryMainFrame();
  FrameNodeImpl* main_frame_node = tab_helper->GetFrameNode(main_frame_host);

  // Load a fenced frame.
  GURL fenced_frame_url =
      embedded_test_server()->GetURL("/fenced_frames/title1.html");
  content::RenderFrameHost* fenced_frame_host =
      fenced_frame_test_helper().CreateFencedFrame(main_frame_host,
                                                   fenced_frame_url);
  FrameNodeImpl* fenced_frame_node =
      tab_helper->GetFrameNode(fenced_frame_host);

  // Make sure |fenced_frame_node| does not have a parent frame node.
  EXPECT_EQ(fenced_frame_node->parent_frame_node(), nullptr);

  // The outer document of the fenced frame is available.
  EXPECT_EQ(fenced_frame_node->parent_or_outer_document_or_embedder(),
            main_frame_node);
}

}  // namespace performance_manager
