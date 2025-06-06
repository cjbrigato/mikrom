// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/privacy_sandbox/mock_privacy_sandbox_service.h"
#include "chrome/browser/privacy_sandbox/notice/notice_service_factory.h"
#include "chrome/browser/privacy_sandbox/privacy_sandbox_service.h"
#include "chrome/browser/privacy_sandbox/privacy_sandbox_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/privacy_sandbox/privacy_sandbox_prompt.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/test/test_browser_dialog.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/privacy_sandbox/privacy_sandbox_dialog_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/privacy_sandbox/privacy_sandbox_features.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "ui/views/widget/any_widget_observer.h"
#include "ui/views/widget/widget.h"

using privacy_sandbox::notice::mojom::PrivacySandboxNotice;
using privacy_sandbox::notice::mojom::PrivacySandboxNoticeEvent;

//-----------------------------------------------------------------------------
// Notice Framework Dialog View Interactive UI Tests.
//-----------------------------------------------------------------------------
class PrivacySandboxNoticeFrameworkDialogViewInteractiveUiTest
    : public InProcessBrowserTest {
 public:
  PrivacySandboxNoticeFrameworkDialogViewInteractiveUiTest() {
    feature_list_.InitWithFeaturesAndParameters(
        /*enabled_features=*/{{privacy_sandbox::kPrivacySandboxNoticeFramework,
                               {}}},
        {});
  }

 private:
  base::test::ScopedFeatureList feature_list_;
};

// TODO(crbug.com/408016824): Enable once GetRequiredList is implemented.
IN_PROC_BROWSER_TEST_F(PrivacySandboxNoticeFrameworkDialogViewInteractiveUiTest,
                       DISABLED_MultipleDialogsClosed) {
  views::NamedWidgetShownWaiter waiter1(
      views::test::AnyWidgetTestPasskey{},
      PrivacySandboxDialogView::kViewClassName);
  auto* view_manager =
      PrivacySandboxNoticeServiceFactory::GetForProfile(browser()->profile())
          ->GetDesktopViewManager();
  view_manager->HandleChromeOwnedPageNavigation(browser());

  auto* dialog1 = waiter1.WaitIfNeededAndGet();
  auto* view1 = static_cast<PrivacySandboxDialogView*>(
      dialog1->widget_delegate()->GetContentsView());
  EXPECT_THAT(view1->GetPrivacySandboxNotice(),
              PrivacySandboxNotice::kTopicsConsentNotice);

  views::NamedWidgetShownWaiter waiter2(
      views::test::AnyWidgetTestPasskey{},
      PrivacySandboxDialogView::kViewClassName);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(chrome::kChromeUINewTabPageURL),
      WindowOpenDisposition::NEW_WINDOW,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  view_manager->HandleChromeOwnedPageNavigation(browser());
  auto* dialog2 = waiter2.WaitIfNeededAndGet();
  auto* view2 = static_cast<PrivacySandboxDialogView*>(
      dialog2->widget_delegate()->GetContentsView());
  EXPECT_THAT(view2->GetPrivacySandboxNotice(),
              PrivacySandboxNotice::kTopicsConsentNotice);

  // Check two distinct dialogs were opened.
  EXPECT_FALSE(dialog1->IsClosed());
  EXPECT_FALSE(dialog2->IsClosed());
  EXPECT_NE(dialog1, dialog2);

  // Completing a consent step shouldn't close the dialogs, but all open notices
  // should advance to the next notice.
  view_manager->OnEventOccurred(PrivacySandboxNotice::kTopicsConsentNotice,
                                PrivacySandboxNoticeEvent::kOptIn);
  EXPECT_FALSE(dialog1->IsClosed());
  EXPECT_FALSE(dialog2->IsClosed());
  EXPECT_THAT(view1->GetPrivacySandboxNotice(),
              PrivacySandboxNotice::kProtectedAudienceMeasurementNotice);
  EXPECT_THAT(view2->GetPrivacySandboxNotice(),
              PrivacySandboxNotice::kProtectedAudienceMeasurementNotice);

  // When completing the notice step, close all dialogs.
  view_manager->OnEventOccurred(
      PrivacySandboxNotice::kProtectedAudienceMeasurementNotice,
      PrivacySandboxNoticeEvent::kAck);
  EXPECT_TRUE(dialog1->IsClosed());
  EXPECT_TRUE(dialog2->IsClosed());
}

//-----------------------------------------------------------------------------
// Pre-migration Privacy Sandbox Dialog View Interactive UI Tests.
//-----------------------------------------------------------------------------
class PrivacySandboxDialogViewInteractiveUiTestM1
    : public InProcessBrowserTest {
 public:
  PrivacySandboxDialogViewInteractiveUiTestM1() {
    feature_list_.InitWithFeaturesAndParameters(
        /*enabled_features=*/{{privacy_sandbox::kDisablePrivacySandboxPrompts,
                               {}},
                              {privacy_sandbox::kPrivacySandboxSettings4,
                               {{"consent-required", "true"}}}},
        {});
  }

 private:
  base::test::ScopedFeatureList feature_list_;
};

IN_PROC_BROWSER_TEST_F(PrivacySandboxDialogViewInteractiveUiTestM1,
                       MultipleDialogsClosed) {
  // Check that when the service receives indication that the flow has been
  // completed, that all open dialogs are closed.
  views::NamedWidgetShownWaiter waiter1(
      views::test::AnyWidgetTestPasskey{},
      PrivacySandboxDialogView::kViewClassName);
  PrivacySandboxDialog::Show(browser(),
                             PrivacySandboxService::PromptType::kM1Consent);
  auto* dialog1 = waiter1.WaitIfNeededAndGet();

  views::NamedWidgetShownWaiter waiter2(
      views::test::AnyWidgetTestPasskey{},
      PrivacySandboxDialogView::kViewClassName);
  auto* new_rfh = ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(chrome::kChromeUINewTabPageURL),
      WindowOpenDisposition::NEW_WINDOW,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  auto* new_browser = chrome::FindBrowserWithTab(
      content::WebContents::FromRenderFrameHost(new_rfh));
  PrivacySandboxDialog::Show(new_browser,
                             PrivacySandboxService::PromptType::kM1Consent);
  auto* dialog2 = waiter2.WaitIfNeededAndGet();

  // Check two distinct dialogs were opened.
  EXPECT_FALSE(dialog1->IsClosed());
  EXPECT_FALSE(dialog2->IsClosed());
  EXPECT_NE(dialog1, dialog2);

  // Completing a consent step shouldn't close the dialogs.
  auto* privacy_sandbox_service =
      PrivacySandboxServiceFactory::GetForProfile(browser()->profile());
  privacy_sandbox_service->PromptActionOccurred(
      PrivacySandboxService::PromptAction::kConsentShown,
      PrivacySandboxService::SurfaceType::kDesktop);
  privacy_sandbox_service->PromptActionOccurred(
      PrivacySandboxService::PromptAction::kConsentAccepted,
      PrivacySandboxService::SurfaceType::kDesktop);
  privacy_sandbox_service->PromptActionOccurred(
      PrivacySandboxService::PromptAction::kConsentDeclined,
      PrivacySandboxService::SurfaceType::kDesktop);

  EXPECT_FALSE(dialog1->IsClosed());
  EXPECT_FALSE(dialog2->IsClosed());

  // While completing the notice step, should close all dialogs
  privacy_sandbox_service->PromptActionOccurred(
      PrivacySandboxService::PromptAction::kNoticeAcknowledge,
      PrivacySandboxService::SurfaceType::kDesktop);
  EXPECT_TRUE(dialog1->IsClosed());
  EXPECT_TRUE(dialog2->IsClosed());
}
