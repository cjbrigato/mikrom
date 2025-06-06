// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/containers/contains.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/scoped_feature_list.h"
#include "build/build_config.h"
#include "build/buildflag.h"
#include "chrome/browser/chromeos/app_mode/kiosk_browser_session.h"
#include "chrome/browser/prefs/incognito_mode_prefs.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/sessions/exit_type_service.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/sessions/session_restore_test_utils.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/profiles/profile_picker.h"
#include "chrome/browser/ui/profiles/profile_ui_test_utils.h"
#include "chrome/browser/ui/startup/startup_browser_creator.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/keep_alive_registry/keep_alive_types.h"
#include "components/keep_alive_registry/scoped_keep_alive.h"
#include "components/signin/public/base/signin_switches.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/aura/window.h"
#include "url/url_constants.h"

namespace {

Browser* FindOneOtherBrowserForProfile(Profile* profile,
                                       Browser* not_this_browser) {
  for (Browser* browser : *BrowserList::GetInstance()) {
    if (browser != not_this_browser && browser->profile() == profile) {
      return browser;
    }
  }
  return nullptr;
}

void WaitForLoadStopForBrowser(Browser* browser) {
  TabStripModel* tab_strip_model = browser->tab_strip_model();
  for (int i = 0; i < tab_strip_model->count(); ++i) {
    content::WebContents* contents = tab_strip_model->GetWebContentsAt(i);
    EXPECT_TRUE(content::WaitForLoadStop(contents));
  }
}

}  // namespace

using BrowserProcessPlatformPartAshBrowsertest = InProcessBrowserTest;

// We should not apply startup URLs if Chrome has previously exited from a
// crash.
IN_PROC_BROWSER_TEST_F(BrowserProcessPlatformPartAshBrowsertest,
                       UrlsNotRestoredAfterCrash) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // Keep the browser process running while browsers are closed.
  auto* profile = browser()->profile();
  ScopedKeepAlive keep_alive(KeepAliveOrigin::BROWSER,
                             KeepAliveRestartOption::DISABLED);
  ScopedProfileKeepAlive profile_keep_alive(
      profile, ProfileKeepAliveOrigin::kBrowserWindow);
  ASSERT_EQ(1u, chrome::GetBrowserCount(profile));
  CloseBrowserSynchronously(browser());
  ASSERT_EQ(0u, chrome::GetBrowserCount(profile));

  // Set the exit type to crashed.
  g_browser_process->local_state()->SetInteger(
      prefs::kBrowserProfilePickerAvailabilityOnStartup,
      static_cast<int>(ProfilePicker::AvailabilityOnStartup::kDisabled));
  ExitTypeService::GetInstanceForProfile(profile)
      ->SetLastSessionExitTypeForTest(ExitType::kCrashed);
  StartupBrowserCreator::ClearLaunchedProfilesForTesting();

  // Set the startup URLs pref.
  const GURL restore_url_1 = embedded_test_server()->GetURL("/title1.html");
  const GURL restore_url_2 = embedded_test_server()->GetURL("/title2.html");
  const GURL restore_url_3 = embedded_test_server()->GetURL("/title3.html");
  SessionStartupPref startup_pref(SessionStartupPref::URLS);
  std::vector<GURL> urls_to_open;
  urls_to_open.push_back(restore_url_1);
  urls_to_open.push_back(restore_url_2);
  urls_to_open.push_back(restore_url_3);
  startup_pref.urls = urls_to_open;
  SessionStartupPref::SetStartupPref(profile, startup_pref);

  // Open a new window.
  ui_test_utils::OpenNewEmptyWindowAndWaitUntilActivated(
      profile, /*should_trigger_session_restore=*/true);

  // Startup URLs should not have been applied to the browser window.
  ASSERT_EQ(1u, chrome::GetBrowserCount(profile));
  auto* new_browser = chrome::FindLastActiveWithProfile(profile);
  EXPECT_NO_FATAL_FAILURE(WaitForLoadStopForBrowser(new_browser));
  auto* tab_strip_model = new_browser->tab_strip_model();
  EXPECT_EQ(1, tab_strip_model->GetTabCount());
  EXPECT_EQ(GURL(url::kAboutBlankURL),
            tab_strip_model->GetWebContentsAt(0)->GetVisibleURL());
}

// If startup pref is set to URLS, the first browser window opened should open
// a single window with these startup URLs in its tabstrip.
IN_PROC_BROWSER_TEST_F(BrowserProcessPlatformPartAshBrowsertest,
                       StartupPrefSetURLs) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL original_url = embedded_test_server()->GetURL("/simple.html");

  // Open `original_url` in a tab.
  TabStripModel* tab_strip_model = browser()->tab_strip_model();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), original_url));
  ASSERT_EQ(1, tab_strip_model->count());
  EXPECT_EQ(original_url,
            tab_strip_model->GetWebContentsAt(0)->GetLastCommittedURL());

  // Keep the browser process running while browsers are closed.
  auto* profile = browser()->profile();
  ScopedKeepAlive keep_alive(KeepAliveOrigin::BROWSER,
                             KeepAliveRestartOption::DISABLED);
  ScopedProfileKeepAlive profile_keep_alive(
      profile, ProfileKeepAliveOrigin::kBrowserWindow);
  ASSERT_EQ(1u, chrome::GetBrowserCount(profile));
  CloseBrowserSynchronously(browser());

  // Set the startup URLS pref.
  const GURL restore_url_1 = embedded_test_server()->GetURL("/title1.html");
  const GURL restore_url_2 = embedded_test_server()->GetURL("/title2.html");
  const GURL restore_url_3 = embedded_test_server()->GetURL("/title3.html");
  SessionStartupPref startup_pref(SessionStartupPref::URLS);
  std::vector<GURL> urls_to_open;
  urls_to_open.push_back(restore_url_1);
  urls_to_open.push_back(restore_url_2);
  urls_to_open.push_back(restore_url_3);
  startup_pref.urls = urls_to_open;
  SessionStartupPref::SetStartupPref(profile, startup_pref);

  // Request a new browser window.
  ui_test_utils::OpenNewEmptyWindowAndWaitUntilActivated(profile);

  ASSERT_EQ(1u, chrome::GetBrowserCount(profile));

  auto* pref_urls_opened_browser = chrome::FindLastActiveWithProfile(profile);
  ASSERT_TRUE(pref_urls_opened_browser);

  // Check pref_urls_opened_browser.
  EXPECT_NO_FATAL_FAILURE(WaitForLoadStopForBrowser(pref_urls_opened_browser));
  tab_strip_model = pref_urls_opened_browser->tab_strip_model();
  EXPECT_EQ(3, tab_strip_model->GetTabCount());
  EXPECT_EQ(restore_url_1,
            tab_strip_model->GetWebContentsAt(0)->GetVisibleURL());
  EXPECT_EQ(restore_url_2,
            tab_strip_model->GetWebContentsAt(1)->GetVisibleURL());
  EXPECT_EQ(restore_url_3,
            tab_strip_model->GetWebContentsAt(2)->GetVisibleURL());

  // If there are existing open browsers opening a new browser should not
  // trigger a restore or open another window with startup URLs.
  ui_test_utils::OpenNewEmptyWindowAndWaitUntilActivated(
      profile, /*should_trigger_session_restore=*/true);
  ASSERT_EQ(2u, chrome::GetBrowserCount(profile));
  auto* new_browser = chrome::FindLastActiveWithProfile(profile);
  EXPECT_NO_FATAL_FAILURE(WaitForLoadStopForBrowser(new_browser));
  tab_strip_model = new_browser->tab_strip_model();
  EXPECT_EQ(1, tab_strip_model->GetTabCount());
  EXPECT_EQ(GURL(chrome::kChromeUINewTabURL),
            tab_strip_model->GetWebContentsAt(0)->GetVisibleURL());
}

// If startup pref is set as LAST_AND_URLS, startup urls should be opened in a
// new browser window separated from the last session restored browser.
IN_PROC_BROWSER_TEST_F(BrowserProcessPlatformPartAshBrowsertest,
                       StartupPrefSetAsLastAndURLs) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL original_url = embedded_test_server()->GetURL("/simple.html");

  // Open `original_url` in a tab.
  TabStripModel* tab_strip_model = browser()->tab_strip_model();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), original_url));
  ASSERT_EQ(1, tab_strip_model->count());
  EXPECT_EQ(original_url,
            tab_strip_model->GetWebContentsAt(0)->GetLastCommittedURL());

  // Keep the browser process running while browsers are closed.
  auto* profile = browser()->profile();
  ScopedKeepAlive keep_alive(KeepAliveOrigin::BROWSER,
                             KeepAliveRestartOption::DISABLED);
  ScopedProfileKeepAlive profile_keep_alive(
      profile, ProfileKeepAliveOrigin::kBrowserWindow);
  ASSERT_EQ(1u, chrome::GetBrowserCount(profile));
  CloseBrowserSynchronously(browser());

  // Set the startup LAST_AND_URLS pref.
  const GURL restore_url_1 = embedded_test_server()->GetURL("/title1.html");
  const GURL restore_url_2 = embedded_test_server()->GetURL("/title2.html");
  const GURL restore_url_3 = embedded_test_server()->GetURL("/title3.html");
  SessionStartupPref startup_pref(SessionStartupPref::LAST_AND_URLS);
  std::vector<GURL> urls_to_open;
  urls_to_open.push_back(restore_url_1);
  urls_to_open.push_back(restore_url_2);
  urls_to_open.push_back(restore_url_3);
  startup_pref.urls = urls_to_open;
  SessionStartupPref::SetStartupPref(profile, startup_pref);

  // Request a new browser window.
  ui_test_utils::BrowserChangeObserver new_browser_observer(
      nullptr, ui_test_utils::BrowserChangeObserver::ChangeType::kAdded);
  chrome::NewEmptyWindow(profile);

  // This startup pref should restore a single window.
  base::RunLoop run_loop;
  testing::SessionsRestoredWaiter restore_waiter(run_loop.QuitClosure(), 1);
  run_loop.Run();

  auto* pref_urls_opened_browser = new_browser_observer.Wait();
  ASSERT_TRUE(pref_urls_opened_browser);
  EXPECT_EQ(pref_urls_opened_browser->profile(), profile);
  ui_test_utils::WaitUntilBrowserBecomeActive(pref_urls_opened_browser);

  ASSERT_EQ(2u, chrome::GetBrowserCount(profile));

  auto* last_session_opened_browser =
      FindOneOtherBrowserForProfile(profile, pref_urls_opened_browser);
  ASSERT_TRUE(last_session_opened_browser);

  // Check the last_session_opened_browser.
  EXPECT_NO_FATAL_FAILURE(
      WaitForLoadStopForBrowser(last_session_opened_browser));
  tab_strip_model = last_session_opened_browser->tab_strip_model();
  ASSERT_EQ(1, tab_strip_model->count());
  EXPECT_EQ(original_url,
            tab_strip_model->GetWebContentsAt(0)->GetVisibleURL());

  // Check the pref_urls_opened_browser.
  EXPECT_NO_FATAL_FAILURE(WaitForLoadStopForBrowser(pref_urls_opened_browser));
  tab_strip_model = pref_urls_opened_browser->tab_strip_model();
  EXPECT_EQ(3, tab_strip_model->GetTabCount());
  EXPECT_EQ(restore_url_1,
            tab_strip_model->GetWebContentsAt(0)->GetVisibleURL());
  EXPECT_EQ(restore_url_2,
            tab_strip_model->GetWebContentsAt(1)->GetVisibleURL());
  EXPECT_EQ(restore_url_3,
            tab_strip_model->GetWebContentsAt(2)->GetVisibleURL());

  // If there are existing open browsers opening a new browser should not
  // trigger a restore or open another window with last URLs.
  auto* new_browser = ui_test_utils::OpenNewEmptyWindowAndWaitUntilActivated(
      profile, /*should_trigger_session_restore=*/true);
  ASSERT_EQ(3u, chrome::GetBrowserCount(profile));
  EXPECT_EQ(new_browser, chrome::FindLastActiveWithProfile(profile));
  EXPECT_NO_FATAL_FAILURE(WaitForLoadStopForBrowser(new_browser));
  tab_strip_model = new_browser->tab_strip_model();
  EXPECT_EQ(1, tab_strip_model->GetTabCount());
  EXPECT_EQ(GURL(chrome::kChromeUINewTabURL),
            tab_strip_model->GetWebContentsAt(0)->GetVisibleURL());
}
