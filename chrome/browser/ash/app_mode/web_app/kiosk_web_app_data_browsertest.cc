// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/app_mode/web_app/kiosk_web_app_data.h"

#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/path_service.h"
#include "base/test/repeating_test_future.h"
#include "base/test/test_future.h"
#include "base/values.h"
#include "chrome/browser/ash/app_mode/kiosk_app_data_base.h"
#include "chrome/browser/ash/app_mode/kiosk_app_data_delegate.h"
#include "chrome/browser/ash/app_mode/web_app/kiosk_web_app_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/web_applications/test/web_app_icon_test_utils.h"
#include "chrome/browser/web_applications/web_app_install_info.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/testing_browser_process.h"
#include "components/account_id/account_id.h"
#include "content/public/test/browser_test.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ash {

namespace {

const char kAppId[] = "123";
const char kAppUrl[] = "https://example.com/";
const char kAppKey[] = "apps";
const char kAppTitle[] = "Title";
const char16_t kAppTitle16[] = u"Title";
const char kAppTitle2[] = "Title2";
const char kTitleKey[] = "name";
const char kIconKey[] = "icon";
const char kLaunchUrlKey[] = "launch_url";
const char kIconPath[] = "chrome/test/data/load_image/image.png";
const char kIconBadPath[] = "chrome/test/data/load_image/image.html";
const char kIconUrl[] = "/load_image/image.png";
const char kIconUrl2[] = "/load_image/fail_image.png";
const char kIconExampleUrl1[] = "https://example.com/icon1.png";
const char kIconExampleUrl2[] = "https://example.com/icon2.png";
const char kLastIconUrlKey[] = "last_icon_url";
const char kLaunchUrl[] = "https://example.com/launch";
const char kStartUrl[] = "https://example.com/start";

base::FilePath GetFullPathToImage(bool valid) {
  base::FilePath test_data_dir;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &test_data_dir));
  return test_data_dir.Append(valid ? kIconPath : kIconBadPath);
}

void PopulateIcon(web_app::WebAppInstallInfo* web_app_info,
                  const std::string& icon_url_str) {
  web_app::IconsMap icons_map;
  const GURL icon_url(icon_url_str);
  std::vector<SkBitmap> bmp = {web_app::CreateSquareIcon(32, SK_ColorWHITE)};
  icons_map.emplace(icon_url, bmp);

  web_app::PopulateProductIcons(web_app_info, &icons_map);
}

const std::string* GetLastIconUrlForAppId() {
  return g_browser_process->local_state()
      ->GetDict(KioskWebAppManager::kWebKioskDictionaryName)
      .FindDict(KioskAppDataBase::kKeyApps)
      ->FindDict(kAppId)
      ->FindString(kLastIconUrlKey);
}

}  // namespace

class KioskWebAppDataTest : public InProcessBrowserTest,
                            public KioskAppDataDelegate {
 public:
  void WaitForAppDataChange(int count) {
    for (int i = 0; i < count; i++) {
      waiter_.Take();
    }
  }

  void SetCached(bool installed, bool icon_valid = true) {
    const std::string app_key = std::string(kAppKey) + '.' + kAppId;
    auto app_dict =
        base::Value::Dict()
            .SetByDottedPath(app_key + '.' + std::string(kTitleKey), kAppTitle)
            .SetByDottedPath(app_key + '.' + std::string(kIconKey),
                             GetFullPathToImage(icon_valid).value());
    if (installed) {
      app_dict.SetByDottedPath(app_key + '.' + std::string(kLaunchUrlKey),
                               kLaunchUrl);
    }
    g_browser_process->local_state()->Set(
        KioskWebAppManager::kWebKioskDictionaryName,
        base::Value(std::move(app_dict)));
  }

 protected:
  KioskWebAppData CreateTestData(const std::string& title = std::string(),
                                 const GURL& icon_url = GURL()) {
    return {*this, kAppId, EmptyAccountId(), GURL(kAppUrl), title, icon_url};
  }

 private:
  // KioskAppDataDelegate:
  base::FilePath GetKioskAppIconCacheDir() override {
    base::FilePath user_data_dir;
    bool has_dir =
        base::PathService::Get(chrome::DIR_USER_DATA, &user_data_dir);
    DCHECK(has_dir);
    return user_data_dir;
  }

  void OnKioskAppDataChanged(const std::string& /*app_id*/) override {
    waiter_.AddValue(true);
  }

  void OnKioskAppDataLoadFailure(const std::string& /*app_id*/) override {}

  void OnExternalCacheDamaged(const std::string& /*app_id*/) override {}

  base::test::RepeatingTestFuture<bool> waiter_;
};

IN_PROC_BROWSER_TEST_F(KioskWebAppDataTest, NoIconCached) {
  KioskWebAppData app_data = CreateTestData();
  EXPECT_FALSE(app_data.LoadFromCache());
  // The app will stay in the INIT state if there is nothing to be loaded from
  // cache.
  EXPECT_EQ(app_data.status(), KioskWebAppData::Status::kInit);
  EXPECT_EQ(app_data.name(), kAppUrl);
  EXPECT_TRUE(app_data.icon().isNull());
}

IN_PROC_BROWSER_TEST_F(KioskWebAppDataTest, LoadCachedIcon) {
  SetCached(/*installed=*/false);
  KioskWebAppData app_data = CreateTestData();
  EXPECT_TRUE(app_data.LoadFromCache());
  app_data.LoadIcon();
  WaitForAppDataChange(2);

  EXPECT_EQ(app_data.status(), KioskWebAppData::Status::kLoaded);
  EXPECT_EQ(app_data.name(), kAppTitle);
  EXPECT_FALSE(app_data.icon().isNull());
}

IN_PROC_BROWSER_TEST_F(KioskWebAppDataTest, PRE_DownloadedIconPersists) {
  // Start test server.
  net::EmbeddedTestServer test_server;
  test_server.AddDefaultHandlers(GetChromeTestDataDir());
  ASSERT_TRUE(test_server.Start());

  KioskWebAppData app_data =
      CreateTestData(kAppTitle, test_server.GetURL(kIconUrl));
  app_data.LoadFromCache();
  app_data.LoadIcon();
  WaitForAppDataChange(1);

  EXPECT_EQ(app_data.status(), KioskWebAppData::Status::kLoaded);
  EXPECT_EQ(app_data.name(), kAppTitle);
  const std::string* icon_url_string = GetLastIconUrlForAppId();
  ASSERT_TRUE(icon_url_string);
  ASSERT_EQ(*icon_url_string, test_server.GetURL(kIconUrl).spec());
}

IN_PROC_BROWSER_TEST_F(KioskWebAppDataTest, DownloadedIconPersists) {
  // No test server is launched intentionaly to verify that we are using the
  // cached icon.
  // We should still find the correct icon url in order to not initiate a
  // redownload.
  const std::string* icon_url_string = GetLastIconUrlForAppId();
  ASSERT_TRUE(icon_url_string);
  const GURL icon_url = GURL(*icon_url_string);

  KioskWebAppData app_data = CreateTestData(kAppTitle2, icon_url);
  EXPECT_TRUE(app_data.LoadFromCache());
  // Icon is stored in cache.
  EXPECT_EQ(app_data.status(), KioskWebAppData::Status::kLoading);

  app_data.LoadIcon();
  WaitForAppDataChange(2);

  EXPECT_EQ(app_data.status(), KioskWebAppData::Status::kLoaded);
  // The title should not persist.
  EXPECT_EQ(app_data.name(), kAppTitle2);
}

IN_PROC_BROWSER_TEST_F(KioskWebAppDataTest,
                       PRE_RedownloadIconWhenDifferentUrl) {
  // Start test server.
  net::EmbeddedTestServer test_server;
  test_server.AddDefaultHandlers(GetChromeTestDataDir());
  ASSERT_TRUE(test_server.Start());

  KioskWebAppData app_data =
      CreateTestData(kAppTitle, test_server.GetURL(kIconUrl));
  app_data.LoadFromCache();
  app_data.LoadIcon();
  WaitForAppDataChange(1);

  EXPECT_EQ(app_data.status(), KioskWebAppData::Status::kLoaded);
  EXPECT_EQ(app_data.name(), kAppTitle);
}

IN_PROC_BROWSER_TEST_F(KioskWebAppDataTest, RedownloadIconWhenDifferentUrl) {
  // Start test server.
  net::EmbeddedTestServer test_server;
  test_server.AddDefaultHandlers(GetChromeTestDataDir());
  ASSERT_TRUE(test_server.Start());

  KioskWebAppData app_data =
      CreateTestData(kAppTitle2, test_server.GetURL(kIconUrl2));

  EXPECT_FALSE(app_data.LoadFromCache());
  // No icon was loaded from cache because urls are different.
  EXPECT_EQ(app_data.status(), KioskWebAppData::Status::kInit);

  app_data.LoadIcon();
  WaitForAppDataChange(1);

  EXPECT_EQ(app_data.status(), KioskWebAppData::Status::kLoaded);
  EXPECT_EQ(app_data.name(), kAppTitle2);
}

IN_PROC_BROWSER_TEST_F(KioskWebAppDataTest, AlreadyInstalled) {
  SetCached(/*installed=*/true);
  KioskWebAppData app_data = CreateTestData(kAppTitle2);
  app_data.LoadFromCache();
  app_data.LoadIcon();
  WaitForAppDataChange(2);

  EXPECT_EQ(app_data.status(), KioskWebAppData::Status::kInstalled);
  EXPECT_EQ(app_data.name(), kAppTitle);
}

IN_PROC_BROWSER_TEST_F(KioskWebAppDataTest, LaunchableUrl) {
  SetCached(/*installed=*/true);

  // `launch_url` is treated as launchable URL if the app hasn't been installed.
  KioskWebAppData app_data = CreateTestData(kAppTitle);
  EXPECT_NE(app_data.status(), KioskWebAppData::Status::kInstalled);
  EXPECT_EQ(app_data.GetLaunchableUrl(), GURL(kAppUrl));

  // `start_url` is treated as launchable URL if the app has been installed.
  auto app_info =
      web_app::WebAppInstallInfo::CreateWithStartUrlForTesting(GURL(kStartUrl));
  app_data.UpdateFromWebAppInfo(*app_info);
  app_data.LoadFromCache();
  WaitForAppDataChange(1);
  EXPECT_EQ(app_data.status(), KioskWebAppData::Status::kInstalled);
  EXPECT_EQ(app_data.GetLaunchableUrl(), GURL(kStartUrl));
}

IN_PROC_BROWSER_TEST_F(KioskWebAppDataTest,
                       PRE_CanLoadFromCacheAfterUpdatingFromWebAppInfo) {
  // We do not use `icon_url` for loading icon in this test, it is set to
  // correctly test `LoadFromCache` function.
  KioskWebAppData app_data = CreateTestData("", GURL(kIconExampleUrl1));

  EXPECT_EQ(app_data.status(), KioskWebAppData::Status::kInit);
  EXPECT_EQ(app_data.GetLaunchableUrl(), GURL(kAppUrl));
  EXPECT_TRUE(app_data.icon().isNull());

  auto app_info =
      web_app::WebAppInstallInfo::CreateWithStartUrlForTesting(GURL(kStartUrl));
  app_info->title = kAppTitle16;
  PopulateIcon(app_info.get(), kIconExampleUrl1);

  app_data.UpdateFromWebAppInfo(*app_info);
}

IN_PROC_BROWSER_TEST_F(KioskWebAppDataTest,
                       CanLoadFromCacheAfterUpdatingFromWebAppInfo) {
  KioskWebAppData app_data = CreateTestData("", GURL(kIconExampleUrl2));

  EXPECT_TRUE(app_data.LoadFromCache());
}

IN_PROC_BROWSER_TEST_F(KioskWebAppDataTest, InvalidIcon) {
  SetCached(/*installed=*/false, /*icon_valid=*/false);
  KioskWebAppData app_data = CreateTestData();
  base::test::TestFuture<void> waiter;
  app_data.SetOnLoadedCallbackForTesting(waiter.GetCallback());

  app_data.LoadFromCache();
  app_data.LoadIcon();
  EXPECT_TRUE(waiter.Wait());
  EXPECT_EQ(app_data.status(), KioskWebAppData::Status::kLoaded);
  const std::string* icon_url_string = GetLastIconUrlForAppId();
  ASSERT_FALSE(icon_url_string);
}
}  // namespace ash
