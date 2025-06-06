// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media/webrtc/display_media_access_handler.h"

#include "base/run_loop.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/media/webrtc/desktop_media_picker_manager.h"
#include "chrome/browser/media/webrtc/media_capture_devices_dispatcher.h"
#include "chrome/browser/media/webrtc/webrtc_browsertest_base.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

namespace {

class NonTabWebView : public views::WidgetDelegate, public views::WebView {
 public:
  NonTabWebView(content::BrowserContext* browser_context, const GURL& url) {
    auto* widget = views::Widget::CreateWindowWithParent(
        this, /*parent=*/gfx::NativeView());
    widget->Show();

    SetBrowserContext(browser_context);
    LoadInitialURL(url);
    content::WaitForLoadStop(GetWebContents());
  }

  // views::WidgetDelegate:
  views::View* GetContentsView() override { return this; }

  // views::WebView:
  void RequestMediaAccessPermission(
      content::WebContents* web_contents,
      const content::MediaStreamRequest& request,
      content::MediaResponseCallback callback) override {
    MediaCaptureDevicesDispatcher::GetInstance()->ProcessMediaAccessRequest(
        web_contents, request, std::move(callback), nullptr /* extension */);
  }
  bool CheckMediaAccessPermission(content::RenderFrameHost* render_frame_host,
                                  const url::Origin& security_origin,
                                  blink::mojom::MediaStreamType type) override {
    return MediaCaptureDevicesDispatcher::GetInstance()
        ->CheckMediaAccessPermission(render_frame_host, security_origin, type);
  }
};

}  // namespace

class DisplayMediaAccessHandlerTest
    : public testing::WithParamInterface<bool>,
      public WebRtcTestBase,
      public DesktopMediaPickerManager::DialogObserver {
 public:
  DisplayMediaAccessHandlerTest() = default;
  ~DisplayMediaAccessHandlerTest() override = default;

  DisplayMediaAccessHandlerTest(const DisplayMediaAccessHandlerTest&) = delete;
  DisplayMediaAccessHandlerTest& operator=(
      const DisplayMediaAccessHandlerTest&) = delete;

  void SetSystemAudioSetting(bool enabled) {
    content::WebContents* web_contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    HostContentSettingsMap* content_settings =
        HostContentSettingsMapFactory::GetForProfile(
            web_contents->GetBrowserContext());
    content_settings->SetDefaultContentSetting(
        ContentSettingsType::DISPLAY_MEDIA_SYSTEM_AUDIO,
        enabled ? CONTENT_SETTING_ALLOW : CONTENT_SETTING_BLOCK);
  }

  // DesktopMediaPickerManager::DialogObserver implementation:
  void OnDialogOpened(const DesktopMediaPicker::Params&) override {
    dialog_opened_ = true;
    if (run_loop_) {
      run_loop_->Quit();
    }
  }

  void OnDialogClosed() override {}

  std::unique_ptr<base::RunLoop> run_loop_;

  bool dialog_opened_ = false;
};

// Verify that the display media picker will show up by default.
IN_PROC_BROWSER_TEST_F(DisplayMediaAccessHandlerTest, ShowPickerByDefault) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // Navigate to an empty page.
  GURL url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(ui_test_utils::NavigateToURL(browser(), url));

  DesktopMediaPickerManager* picker_manager = DesktopMediaPickerManager::Get();
  picker_manager->AddObserver(this);

  SetSystemAudioSetting(false);

  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  // If the video stream is requested, the picker should still show up.
  run_loop_ = std::make_unique<base::RunLoop>();
  EXPECT_EQ(true, content::EvalJs(web_contents->GetPrimaryMainFrame(),
                                  R"((async () => {
    navigator.mediaDevices.getDisplayMedia({
        audio: true, systemAudio: 'include'});
    return true;
  })())"));
  run_loop_->Run();
  EXPECT_EQ(dialog_opened_, true);
}

// Verify that the request will be rejected when the video stream is not
// requested by default.
IN_PROC_BROWSER_TEST_F(DisplayMediaAccessHandlerTest, RejectNoVideoByDefault) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // Navigate to an empty page.
  GURL url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(ui_test_utils::NavigateToURL(browser(), url));

  DesktopMediaPickerManager* picker_manager = DesktopMediaPickerManager::Get();
  picker_manager->AddObserver(this);

  SetSystemAudioSetting(false);

  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_THAT(content::EvalJs(web_contents->GetPrimaryMainFrame(),
                              R"((async () => {
    return navigator.mediaDevices.getDisplayMedia({
        audio: true, systemAudio: 'include', video: false});
  })())")
                  .error,
              testing::HasSubstr("Not supported"));
  EXPECT_EQ(dialog_opened_, false);
}

// Verify that when `ContentSettingsType::DISPLAY_MEDIA_SYSTEM_AUDIO` is set,
// the display media selection dialog can be bypassed and the system audio track
// will be available by default.
IN_PROC_BROWSER_TEST_F(DisplayMediaAccessHandlerTest, ForceSystemAudio) {
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://version")));

  DesktopMediaPickerManager* picker_manager = DesktopMediaPickerManager::Get();
  picker_manager->AddObserver(this);

  SetSystemAudioSetting(true);

  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  // It is expected to get a system audio track and no video track.
  EXPECT_EQ(true, content::EvalJs(web_contents->GetPrimaryMainFrame(),
                                  R"((async () => {
    const mediaStream = await navigator.mediaDevices.getDisplayMedia({
        audio: true, systemAudio: 'include', video: false});
    return mediaStream.getAudioTracks().length == 1 &&
            mediaStream.getVideoTracks().length == 0;
  })())"));
  EXPECT_EQ(dialog_opened_, false);
}

// Verify that `ContentSettingsType::DISPLAY_MEDIA_SYSTEM_AUDIO` does not work
// when the request is not from chrome://.
IN_PROC_BROWSER_TEST_F(DisplayMediaAccessHandlerTest,
                       ForceSystemAudioButWrongScheme) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // Navigate to an empty page.
  GURL url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(ui_test_utils::NavigateToURL(browser(), url));

  DesktopMediaPickerManager* picker_manager = DesktopMediaPickerManager::Get();
  picker_manager->AddObserver(this);

  SetSystemAudioSetting(true);

  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_THAT(content::EvalJs(web_contents->GetPrimaryMainFrame(),
                              R"((async () => {
    return navigator.mediaDevices.getDisplayMedia({
        audio: true, systemAudio: 'include', video: false});
  })())")
                  .error,
              testing::HasSubstr("Not supported"));
  EXPECT_EQ(dialog_opened_, false);
}

// Verify that `ContentSettingsType::DISPLAY_MEDIA_SYSTEM_AUDIO` does not work
// when the system audio is excluded and the request should be rejected.
IN_PROC_BROWSER_TEST_F(DisplayMediaAccessHandlerTest,
                       ForceSystemAudioButExcluded) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // Navigate to an empty page.
  GURL url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(ui_test_utils::NavigateToURL(browser(), url));

  DesktopMediaPickerManager* picker_manager = DesktopMediaPickerManager::Get();
  picker_manager->AddObserver(this);

  SetSystemAudioSetting(true);

  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_THAT(content::EvalJs(web_contents->GetPrimaryMainFrame(),
                              R"((async () => {
    return navigator.mediaDevices.getDisplayMedia({
        audio: true, systemAudio: 'exclude', video: false});
  })())")
                  .error,
              testing::HasSubstr("Not supported"));
  EXPECT_EQ(dialog_opened_, false);
}

// Verify that `ContentSettingsType::DISPLAY_MEDIA_SYSTEM_AUDIO` does not work
// when the video stream is requested and should fallback to the original
// behavior.
IN_PROC_BROWSER_TEST_F(DisplayMediaAccessHandlerTest,
                       ForceSystemAudioWithVideoStream) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // Navigate to an empty page.
  GURL url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(ui_test_utils::NavigateToURL(browser(), url));

  DesktopMediaPickerManager* picker_manager = DesktopMediaPickerManager::Get();
  picker_manager->AddObserver(this);

  SetSystemAudioSetting(true);

  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  // If the video stream is requested, the picker should still show up.
  run_loop_ = std::make_unique<base::RunLoop>();
  EXPECT_EQ(true, content::EvalJs(web_contents->GetPrimaryMainFrame(),
                                  R"((async () => {
    navigator.mediaDevices.getDisplayMedia({
        audio: true, systemAudio: 'include', video: true});
    return true;
  })())"));
  run_loop_->Run();
  EXPECT_EQ(dialog_opened_, true);
}

// Verify that showing media picker for a non-tab WebContents does not crash.
IN_PROC_BROWSER_TEST_F(DisplayMediaAccessHandlerTest, NonTabWebContents) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // Creates a non-tab WebContents.
  auto* webview = new NonTabWebView(
      browser()->profile(), embedded_test_server()->GetURL("/title1.html"));
  content::WebContents* web_contents = webview->GetWebContents();

  // Media picker dialog should show as a standalone window and no crash.
  DesktopMediaPickerManager* picker_manager = DesktopMediaPickerManager::Get();
  picker_manager->AddObserver(this);

  run_loop_ = std::make_unique<base::RunLoop>();
  EXPECT_EQ(true, content::EvalJs(web_contents->GetPrimaryMainFrame(),
                                  R"((async () => {
    navigator.mediaDevices.getDisplayMedia({audio: true, video: true});
    return true;
  })())"));
  run_loop_->Run();
  EXPECT_EQ(dialog_opened_, true);
}
