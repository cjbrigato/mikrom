// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <WebKit/WebKit.h>

#import "base/functional/bind.h"
#import "base/strings/sys_string_conversions.h"
#import "base/test/ios/wait_util.h"
#import "ios/web/js_messaging/java_script_content_world.h"
#import "ios/web/js_messaging/page_script_util.h"
#import "ios/web/js_messaging/web_frame_impl.h"
#import "ios/web/public/js_messaging/web_frames_manager.h"
#import "ios/web/public/test/web_state_test_util.h"
#import "ios/web/public/test/web_test_with_web_state.h"
#import "ios/web/public/web_state.h"
#import "ios/web/test/js_test_util_internal.h"
#import "ios/web/web_state/ui/crw_web_controller.h"
#import "testing/gtest/include/gtest/gtest.h"
#import "testing/gtest_mac.h"

using base::test::ios::kWaitForJSCompletionTimeout;
using base::test::ios::WaitUntilConditionOrTimeout;

namespace {
// Returns the first WebFrame found which is not the main frame in the given
// `web_state`. Does not wait and returns null if such a frame is not found.
web::WebFrame* GetChildWebFrameForWebState(web::WebState* web_state) {
  __block web::WebFramesManager* manager =
      web_state->GetPageWorldWebFramesManager();
  web::WebFrame* iframe = nullptr;
  for (web::WebFrame* frame : manager->GetAllWebFrames()) {
    if (!frame->IsMainFrame()) {
      iframe = frame;
      break;
    }
  }
  return iframe;
}
}  // namespace

namespace web {

// Test fixture to test WebFrameImpl with a real JavaScript context.
typedef WebTestWithWebState WebFrameImplIntTest;

// Tests that the expected result is received from executing a JavaScript
// function via `CallJavaScriptFunction` on the main frame.
TEST_F(WebFrameImplIntTest, CallJavaScriptFunctionOnMainFrame) {
  ASSERT_TRUE(LoadHtml("<p>"));

  WebFrame* main_frame =
      web_state()->GetPageWorldWebFramesManager()->GetMainWebFrame();
  ASSERT_TRUE(main_frame);

  __block bool called = false;
  main_frame->CallJavaScriptFunction(
      "getFrameId", base::Value::List(),
      base::BindOnce(^(const base::Value* value) {
        ASSERT_TRUE(value->is_string());
        EXPECT_EQ(value->GetString(), main_frame->GetFrameId());
        called = true;
      }),
      // Increase feature timeout in order to fail on test specific timeout.
      2 * kWaitForJSCompletionTimeout);

  EXPECT_TRUE(WaitUntilConditionOrTimeout(kWaitForJSCompletionTimeout, ^bool {
    return called;
  }));
}

TEST_F(WebFrameImplIntTest, CallJavaScriptFunctionOnIframe) {
  ASSERT_TRUE(LoadHtml("<p><iframe srcdoc='<p>'/>"));

  __block WebFramesManager* manager =
      web_state()->GetPageWorldWebFramesManager();
  ASSERT_TRUE(WaitUntilConditionOrTimeout(
      base::test::ios::kWaitForJSCompletionTimeout, ^bool {
        return manager->GetAllWebFrames().size() == 2;
      }));

  WebFrame* iframe = GetChildWebFrameForWebState(web_state());
  ASSERT_TRUE(iframe);

  __block bool called = false;
  iframe->CallJavaScriptFunction(
      "getFrameId", base::Value::List(),
      base::BindOnce(^(const base::Value* value) {
        ASSERT_TRUE(value->is_string());
        EXPECT_EQ(value->GetString(), iframe->GetFrameId());
        called = true;
      }),
      // Increase feature timeout in order to fail on test specific timeout.
      2 * kWaitForJSCompletionTimeout);

  EXPECT_TRUE(WaitUntilConditionOrTimeout(kWaitForJSCompletionTimeout, ^bool {
    return called;
  }));
}

TEST_F(WebFrameImplIntTest, CallJavaScriptFunctionTimeout) {
  ASSERT_TRUE(LoadHtml("<p>"));

  // Inject a function which will never return in order to test feature timeout.
  ExecuteJavaScript(@"__gCrWeb.testFunctionNeverReturns = function() {"
                     "  while(true) {}"
                     "};");

  WebFrame* main_frame =
      web_state()->GetPageWorldWebFramesManager()->GetMainWebFrame();
  ASSERT_TRUE(main_frame);

  __block bool called = false;
  main_frame->CallJavaScriptFunction(
      "testFunctionNeverReturns", base::Value::List(),
      base::BindOnce(^(const base::Value* value) {
        EXPECT_FALSE(value);
        called = true;
      }),
      // A small timeout less than kWaitForJSCompletionTimeout. Since this test
      // case tests the timeout, it will take at least this long to execute.
      // This value should be very small to avoid increasing test suite
      // execution time, but long enough to avoid flake.
      base::Milliseconds(5));

  EXPECT_TRUE(WaitUntilConditionOrTimeout(kWaitForJSCompletionTimeout, ^bool {
    base::RunLoop().RunUntilIdle();
    return called;
  }));
}

// Tests that the expected result is received from executing a JavaScript
// function via `CallJavaScriptFunction` on the main frame in the page content
// world.
TEST_F(WebFrameImplIntTest, CallJavaScriptFunctionMainFramePageContentWorld) {
  ASSERT_TRUE(LoadHtml("<p>"));
  ExecuteJavaScript(@"__gCrWeb = {};"
                    @"__gCrWeb['fakeFunction'] = function() {"
                    @"  return '10';"
                    @"}");

  web::WebFrameImpl* main_frame_impl = static_cast<web::WebFrameImpl*>(
      web_state()->GetPageWorldWebFramesManager()->GetMainWebFrame());
  ASSERT_TRUE(main_frame_impl);

  JavaScriptContentWorld world(GetBrowserState(), WKContentWorld.pageWorld);
  __block bool called = false;

  auto block = ^(const base::Value* value) {
    ASSERT_TRUE(value->is_string());
    EXPECT_EQ(value->GetString(), "10");
    called = true;
  };
  EXPECT_TRUE(main_frame_impl->CallJavaScriptFunctionInContentWorld(
      "fakeFunction", base::Value::List(), &world, base::BindOnce(block),
      // Increase feature timeout in order to fail on test specific timeout.
      2 * kWaitForJSCompletionTimeout));

  EXPECT_TRUE(WaitUntilConditionOrTimeout(kWaitForJSCompletionTimeout, ^bool {
    return called;
  }));
}

// Test fixture for testing WebFrameImpl in different content worlds.
class WebFrameImplContentWorldIntTest
    : public WebFrameImplIntTest,
      public testing::WithParamInterface<ContentWorld> {
 protected:
  // Returns the main frame of the test's content world.
  WebFrameImpl* main_frame() {
    return static_cast<web::WebFrameImpl*>(
        web_state()->GetWebFramesManager(GetParam())->GetMainWebFrame());
  }

  // Returns the `WKContentWorld` in which `WebFrameImpl` should execute
  // scripts.
  WKContentWorld* GetWKContentWorld() {
    switch (GetParam()) {
      case ContentWorld::kIsolatedWorld:
        return WKContentWorld.defaultClientWorld;
      case ContentWorld::kPageContentWorld:
        return WKContentWorld.pageWorld;
      case ContentWorld::kAllContentWorlds:
        NOTREACHED();
    }
  }

  // Executes `script` in the WKWebView associated to the current WebState in
  // the test's content world.
  void ExecuteJavaScriptInTestContentWorld(NSString* script) {
    WKWebView* web_view =
        [web::test::GetWebController(web_state()) ensureWebViewCreated];
    test::ExecuteJavaScript(web_view, GetWKContentWorld(), script);
  }
};

// Tests that the expected result is received from executing a script via
// `ExecuteJavaScript` on the main frame in each content world.
TEST_P(WebFrameImplContentWorldIntTest, ExecuteJavaScriptMainFrame) {
  ASSERT_TRUE(LoadHtml("<p>"));
  ExecuteJavaScriptInTestContentWorld(@"__gCrWeb = {};"
                                      @"__gCrWeb['fakeFunction'] = function() {"
                                      @"  return '10';"
                                      @"}");

  web::WebFrameImpl* main_frame_impl = main_frame();
  ASSERT_TRUE(main_frame_impl);

  __block bool called = false;
  auto block = ^(const base::Value* value, NSError* error) {
    ASSERT_FALSE(error);
    ASSERT_TRUE(value->is_string());
    EXPECT_EQ(value->GetString(), "10");
    called = true;
  };
  EXPECT_TRUE(main_frame_impl->ExecuteJavaScript(u"__gCrWeb['fakeFunction']()",
                                                 base::BindOnce(block)));

  EXPECT_TRUE(WaitUntilConditionOrTimeout(kWaitForJSCompletionTimeout, ^bool {
    return called;
  }));
}

// Tests that the expected result is received from executing a JavaScript
// function via `CallJavaScriptFunction` on the main frame in each content
// world.
TEST_P(WebFrameImplContentWorldIntTest,
       CallJavaScriptFunctionMainFrameIsolatedWorld) {
  ASSERT_TRUE(LoadHtml("<p>"));
  ExecuteJavaScriptInTestContentWorld(@"__gCrWeb = {};"
                                      @"__gCrWeb['fakeFunction'] = function() {"
                                      @"  return '10';"
                                      @"}");

  web::WebFrameImpl* main_frame_impl = main_frame();
  ASSERT_TRUE(main_frame_impl);

  __block bool called = false;
  auto block = ^(const base::Value* value) {
    ASSERT_TRUE(value->is_string());
    EXPECT_EQ(value->GetString(), "10");
    called = true;
  };
  EXPECT_TRUE(main_frame_impl->CallJavaScriptFunction(
      "fakeFunction", base::Value::List(), base::BindOnce(block),
      // Increase feature timeout in order to fail on test specific timeout.
      2 * kWaitForJSCompletionTimeout));

  EXPECT_TRUE(WaitUntilConditionOrTimeout(kWaitForJSCompletionTimeout, ^bool {
    return called;
  }));
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/,
                         WebFrameImplContentWorldIntTest,
                         ::testing::Values(ContentWorld::kIsolatedWorld,
                                           ContentWorld::kPageContentWorld));

}  // namespace web
