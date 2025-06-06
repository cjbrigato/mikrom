// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>

#include "base/test/bind.h"
#include "base/test/metrics/histogram_tester.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_browser_main.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/ui/accelerator_utils.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/startup/default_browser_prompt/default_browser_prompt.h"
#include "chrome/browser/ui/startup/default_browser_prompt/default_browser_prompt_manager.h"
#include "chrome/browser/ui/startup/infobar_utils.h"
#include "chrome/browser/ui/toolbar/app_menu_model.h"
#include "chrome/browser/ui/views/frame/app_menu_button.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/infobars/confirm_infobar.h"
#include "chrome/browser/ui/webui/test_support/webui_interactive_test_mixin.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/branded_strings.h"
#include "chrome/test/base/ui_test_utils.h"
#include "chrome/test/interaction/interactive_browser_test.h"
#include "chrome/test/interaction/tracked_element_webcontents.h"
#include "components/infobars/content/content_infobar_manager.h"
#include "components/infobars/core/infobar.h"
#include "components/infobars/core/infobar_manager.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_main_parts.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/switches.h"

#if !BUILDFLAG(IS_LINUX)
#include "chrome/browser/ui/chrome_pages.h"
#endif

namespace {
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kSecondTabContents);
}  // namespace

class DefaultBrowserPromptInteractiveTest
    : public WebUiInteractiveTestMixin<InteractiveBrowserTest> {
 public:
  void SetUp() override {
    shell_integration::DefaultBrowserWorker::DisableSetAsDefaultForTesting();
    InteractiveBrowserTest::SetUp();
  }

  InteractiveTestApi::MultiStep DoesAppMenuItemExist(bool exists) {
    return Steps(
        PressButton(kToolbarAppMenuButtonElementId),
        exists ? EnsurePresent(AppMenuModel::kSetBrowserAsDefaultMenuItem)
               : EnsureNotPresent(AppMenuModel::kSetBrowserAsDefaultMenuItem),
        WithView(kToolbarAppMenuButtonElementId,
                 [](AppMenuButton* app_menu_button) {
                   app_menu_button->CloseMenu();
                 }));
  }

  InteractiveTestApi::MultiStep RemovesAllBrowserDefaultPromptsWhen(
      InteractiveTestApi::MultiStep steps,
      bool preserve_app_menu_item = false) {
    return Steps(
        WaitForShow(ConfirmInfoBar::kInfoBarElementId),
        WaitForShow(kToolbarAppMenuButtonElementId), DoesAppMenuItemExist(true),
        AddInstrumentedTab(kSecondTabContents,
                           GURL(chrome::kChromeUINewTabURL)),
        WaitForShow(ConfirmInfoBar::kInfoBarElementId), std::move(steps),
        WaitForHide(ConfirmInfoBar::kInfoBarElementId),
        SelectTab(kTabStripElementId, 0),
        WaitForHide(ConfirmInfoBar::kInfoBarElementId),
        DoesAppMenuItemExist(preserve_app_menu_item));
  }
};

IN_PROC_BROWSER_TEST_F(DefaultBrowserPromptInteractiveTest,
                       ShowsDefaultBrowserPromptOnNewTab) {
  DefaultBrowserPromptManager::GetInstance()->MaybeShowPrompt();
  RunTestSequence(
      WaitForShow(ConfirmInfoBar::kInfoBarElementId),
      AddInstrumentedTab(kSecondTabContents, GURL(chrome::kChromeUINewTabURL)),
      WaitForShow(ConfirmInfoBar::kInfoBarElementId));
}

IN_PROC_BROWSER_TEST_F(DefaultBrowserPromptInteractiveTest,
                       HeightStaysConstant) {
  int height;
  DefaultBrowserPromptManager::GetInstance()->MaybeShowPrompt();
  RunTestSequence(
      WaitForShow(ConfirmInfoBar::kInfoBarElementId),
      WithView(ConfirmInfoBar::kInfoBarElementId,
               [&height](ConfirmInfoBar* info_bar) {
                 height = info_bar->target_height_for_testing();
               }),
      AddInstrumentedTab(kSecondTabContents, GURL(chrome::kChromeUINewTabURL)),
      SelectTab(kTabStripElementId, 0),
      WaitForShow(ConfirmInfoBar::kInfoBarElementId),
      CheckView(ConfirmInfoBar::kInfoBarElementId,
                [&height](ConfirmInfoBar* info_bar) {
                  return height == info_bar->target_height_for_testing();
                }));
}

IN_PROC_BROWSER_TEST_F(DefaultBrowserPromptInteractiveTest,
                       DoesNotShowDefaultBrowserPromptOnIncognitoTab) {
  ui::Accelerator incognito_accelerator;
  chrome::AcceleratorProviderForBrowser(browser())->GetAcceleratorForCommandId(
      IDC_NEW_INCOGNITO_WINDOW, &incognito_accelerator);

  DefaultBrowserPromptManager::GetInstance()->MaybeShowPrompt();
  RunTestSequence(
      WaitForShow(ConfirmInfoBar::kInfoBarElementId),
      SendAccelerator(kBrowserViewElementId, incognito_accelerator),
      InAnyContext(
          WaitForShow(kBrowserViewElementId).SetTransitionOnlyOnEvent(true)),
      InSameContext(EnsureNotPresent(ConfirmInfoBar::kInfoBarElementId)));
}

IN_PROC_BROWSER_TEST_F(DefaultBrowserPromptInteractiveTest,
                       DoesNotShowAppMenuItemOnIncognitoTab) {
  ui::Accelerator incognito_accelerator;
  chrome::AcceleratorProviderForBrowser(browser())->GetAcceleratorForCommandId(
      IDC_NEW_INCOGNITO_WINDOW, &incognito_accelerator);

  DefaultBrowserPromptManager::GetInstance()->MaybeShowPrompt();
  RunTestSequence(
      PressButton(kToolbarAppMenuButtonElementId),

      WaitForShow(AppMenuModel::kSetBrowserAsDefaultMenuItem),
      SendAccelerator(kBrowserViewElementId, incognito_accelerator),
      InAnyContext(
          WaitForShow(kBrowserViewElementId).SetTransitionOnlyOnEvent(true)),
      InSameContext(
          EnsureNotPresent(AppMenuModel::kSetBrowserAsDefaultMenuItem)));
}

IN_PROC_BROWSER_TEST_F(DefaultBrowserPromptInteractiveTest,
                       HandlesAcceptWithDisabledAnimation) {
  // When animations are disabled, the info bar is destroyed sooner which can
  // cause UAF if not handled properly. This test ensures it is handled
  // properly.
  const gfx::AnimationTestApi::RenderModeResetter disable_rich_animations_ =
      gfx::AnimationTestApi::SetRichAnimationRenderMode(
          gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  DefaultBrowserPromptManager::GetInstance()->MaybeShowPrompt();
  RunTestSequence(WaitForShow(ConfirmInfoBar::kInfoBarElementId),
                  PressButton(ConfirmInfoBar::kOkButtonElementId),
                  WaitForHide(ConfirmInfoBar::kInfoBarElementId));
}

IN_PROC_BROWSER_TEST_F(DefaultBrowserPromptInteractiveTest,
                       HandlesDismissWithDisabledAnimation) {
  // When animations are disabled, the info bar is destroyed sooner which can
  // cause UAF if not handled properly. This test ensures it is handled
  // properly.
  const gfx::AnimationTestApi::RenderModeResetter disable_rich_animations_ =
      gfx::AnimationTestApi::SetRichAnimationRenderMode(
          gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  DefaultBrowserPromptManager::GetInstance()->MaybeShowPrompt();
  RunTestSequence(WaitForShow(ConfirmInfoBar::kInfoBarElementId),
                  PressButton(ConfirmInfoBar::kDismissButtonElementId),
                  WaitForHide(ConfirmInfoBar::kInfoBarElementId));
}

IN_PROC_BROWSER_TEST_F(DefaultBrowserPromptInteractiveTest, LogsMetrics) {
  base::HistogramTester histogram_tester;
  DefaultBrowserPromptManager::GetInstance()->MaybeShowPrompt();
  RunTestSequence(WaitForShow(ConfirmInfoBar::kInfoBarElementId),
                  // This flush is needed to prevent TSan builders from flaking.

                  PressButton(ConfirmInfoBar::kOkButtonElementId),
                  WaitForHide(ConfirmInfoBar::kInfoBarElementId));

  histogram_tester.ExpectTotalCount(
      "DefaultBrowser.InfoBar.TimesShownBeforeAccept", 1);
}

IN_PROC_BROWSER_TEST_F(DefaultBrowserPromptInteractiveTest,
                       DoesNotShowDismissedPromptOnNewWindows) {
  // Regression test for a bug where the DefaultBrowserPromptManager didn't
  // stop subscribing to TabStripModelObserver updates when new windows were
  // created.
  DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kTabMovedToNewWindowId);
  DefaultBrowserPromptManager::GetInstance()->MaybeShowPrompt();
  RunTestSequence(
      // Open two tabs
      WaitForShow(ConfirmInfoBar::kInfoBarElementId),
      AddInstrumentedTab(kSecondTabContents, GURL(chrome::kChromeUINewTabURL)),
      WaitForShow(ConfirmInfoBar::kInfoBarElementId),
      // Dismiss prompt on one tab
      PressButton(ConfirmInfoBar::kDismissButtonElementId),
      // Wait for hide
      WaitForHide(ConfirmInfoBar::kInfoBarElementId),
      // Move tab to new window
      InstrumentNextTab(kTabMovedToNewWindowId, AnyBrowser()),
      Do([&]() { chrome::MoveTabsToNewWindow(browser(), {1}); }),
      InAnyContext(WaitForWebContentsReady(kTabMovedToNewWindowId)),
      // Since the infobar isn't rendered synchronously, but the infobar is
      // created inside the manager, check the size of infobars for the moved
      // WebContents.
      InSameContext(CheckElement(
          kTabMovedToNewWindowId,
          [](ui::TrackedElement* el) {
            auto* const manager =
                infobars::ContentInfoBarManager::FromWebContents(
                    el->AsA<TrackedElementWebContents>()
                        ->owner()
                        ->web_contents());
            return manager->infobars().size();
          },
          0U)));
}

IN_PROC_BROWSER_TEST_F(DefaultBrowserPromptInteractiveTest,
                       RemovesAllBrowserDefaultPromptsOnAccept) {
  DefaultBrowserPromptManager::GetInstance()->MaybeShowPrompt();
  RunTestSequence(RemovesAllBrowserDefaultPromptsWhen(
      Steps(PressButton(ConfirmInfoBar::kOkButtonElementId))));
}

IN_PROC_BROWSER_TEST_F(DefaultBrowserPromptInteractiveTest,
                       RemovesAllBrowserDefaultPromptsOnDismiss) {
  DefaultBrowserPromptManager::GetInstance()->MaybeShowPrompt();
  RunTestSequence(RemovesAllBrowserDefaultPromptsWhen(
      Steps(PressButton(ConfirmInfoBar::kDismissButtonElementId)), true));
}

IN_PROC_BROWSER_TEST_F(DefaultBrowserPromptInteractiveTest,
                       RemovesAllBrowserDefaultPromptsOnAppMenuItemSelected) {
  DefaultBrowserPromptManager::GetInstance()->MaybeShowPrompt();
  RunTestSequence(RemovesAllBrowserDefaultPromptsWhen(
      Steps(PressButton(kToolbarAppMenuButtonElementId),
            SelectMenuItem(AppMenuModel::kSetBrowserAsDefaultMenuItem))));
}

IN_PROC_BROWSER_TEST_F(DefaultBrowserPromptInteractiveTest, HidesInFullscreen) {
  DefaultBrowserPromptManager::GetInstance()->MaybeShowPrompt();
  RunTestSequence(WaitForShow(ConfirmInfoBar::kInfoBarElementId), Do([&]() {
                    ui_test_utils::ToggleFullscreenModeAndWait(browser());
                  }),
                  WaitForHide(ConfirmInfoBar::kInfoBarElementId));
}

// Linux test environment doesn't allow setting default via the
// chrome://settings/defaultBrowser page.
#if !BUILDFLAG(IS_LINUX)
IN_PROC_BROWSER_TEST_F(DefaultBrowserPromptInteractiveTest,
                       RemovesAllBrowserDefaultPromptsOnSettingsChange) {
  DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kFirstTabContents);
  const WebContentsInteractionTestUtil::DeepQuery kDefaultBrowserButton = {
      "settings-ui", "settings-main", "settings-basic-page",
      "settings-default-browser-page", "cr-button"};

  DefaultBrowserPromptManager::GetInstance()->MaybeShowPrompt();
  RunTestSequence(RemovesAllBrowserDefaultPromptsWhen(
      Steps(SelectTab(kTabStripElementId, 0), InstrumentTab(kFirstTabContents),
            NavigateWebContents(
                kFirstTabContents,
                GURL(chrome::GetSettingsUrl(chrome::kDefaultBrowserSubPage))),
            ClickElement(kFirstTabContents, kDefaultBrowserButton),
            SelectTab(kTabStripElementId, 1))));
}
#endif

class DefaultBrowserPromptHeadlessBrowserTest
    : public DefaultBrowserPromptInteractiveTest {
 public:
  void SetUp() override {
    base::CommandLine::ForCurrentProcess()->AppendSwitch(::switches::kHeadless);
    DefaultBrowserPromptInteractiveTest::SetUp();
  }
};

IN_PROC_BROWSER_TEST_F(DefaultBrowserPromptHeadlessBrowserTest, DoesNotCrash) {
  DefaultBrowserPromptManager::GetInstance()->MaybeShowPrompt();
  RunTestSequence(WaitForHide(ConfirmInfoBar::kInfoBarElementId));
}
