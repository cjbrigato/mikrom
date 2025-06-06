// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/hid/hid_pinned_notification.h"

#include <string>

#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/device_notifications/device_pinned_notification_unittest.h"
#include "chrome/browser/hid/hid_connection_tracker.h"
#include "chrome/browser/hid/hid_connection_tracker_factory.h"
#include "chrome/browser/hid/hid_test_utils.h"
#include "chrome/grit/branded_strings.h"
#include "chrome/test/base/testing_browser_process.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "extensions/common/constants.h"
#include "extensions/common/extension.h"
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)

class HidPinnedNotificationTest : public DevicePinnedNotificationTestBase {
 public:
  HidPinnedNotificationTest()
      : DevicePinnedNotificationTestBase(
            /*device_content_settings_label=*/u"HID settings") {}

  void ResetTestingBrowserProcessSystemTrayIcon() override {
    TestingBrowserProcess::GetGlobal()->SetHidSystemTrayIcon(nullptr);
  }

  std::u16string GetExpectedTitle(size_t num_origins,
                                  size_t num_connections) override {
#if BUILDFLAG(ENABLE_EXTENSIONS)
    // The text might use "Google Chrome" or "Chromium" depending
    // is_chrome_branded in the build config file, hence using l10n_util to get
    // the expected string.
    return l10n_util::GetPluralStringFUTF16(IDS_WEBHID_SYSTEM_TRAY_ICON_TITLE,
                                            static_cast<int>(num_connections));
#else
    NOTREACHED();
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)
  }

  void SetDeviceConnectionTrackerTestingFactory(Profile* profile) override {
    HidConnectionTrackerFactory::GetInstance()->SetTestingFactory(
        profile,
        base::BindRepeating([](content::BrowserContext* browser_context) {
          return static_cast<std::unique_ptr<KeyedService>>(
              std::make_unique<TestHidConnectionTracker>(
                  Profile::FromBrowserContext(browser_context)));
        }));
  }

  DeviceConnectionTracker* GetDeviceConnectionTracker(Profile* profile,
                                                      bool create) override {
    return static_cast<DeviceConnectionTracker*>(
        HidConnectionTrackerFactory::GetForProfile(profile, create));
  }

  MockDeviceConnectionTracker* GetMockDeviceConnectionTracker(
      DeviceConnectionTracker* connection_tracker) override {
    return static_cast<TestHidConnectionTracker*>(connection_tracker)
        ->mock_device_connection_tracker();
  }

  DevicePinnedNotificationRenderer* GetDevicePinnedNotificationRenderer()
      override {
    auto* hid_pinned_notification = static_cast<HidPinnedNotification*>(
        g_browser_process->hid_system_tray_icon());
    return static_cast<DevicePinnedNotificationRenderer*>(
        hid_pinned_notification->GetIconRendererForTesting());
  }

  std::u16string GetExpectedMessage(
      const std::vector<DeviceSystemTrayIconTestBase::OriginItem>& origin_items)
      override {
    auto sorted_origin_items = origin_items;
    // Sort the |origin_items| by origin. This is necessary because the origin
    // items for each profile in the pinned notification are created by
    // iterating through a structure of flat_map<url::Origin, ...>.
    std::ranges::sort(sorted_origin_items);
#if BUILDFLAG(ENABLE_EXTENSIONS)
    std::vector<std::string> extension_names;
    for (const auto& [origin, connection_count, name] : sorted_origin_items) {
      CHECK(origin.scheme() == extensions::kExtensionScheme);
      extension_names.push_back(name);
    }
    CHECK(!extension_names.empty());
    if (extension_names.size() == 1) {
      return base::UTF8ToUTF16(base::StringPrintf("%s is accessing HID devices",
                                                  extension_names[0].c_str()));
    } else if (extension_names.size() == 2) {
      return base::UTF8ToUTF16(base::StringPrintf(
          "Extensions accessing devices: %s, %s", extension_names[0].c_str(),
          extension_names[1].c_str()));
    }
    return base::UTF8ToUTF16(base::StringPrintf(
        "Extensions accessing devices: %s, %s +%zu more",
        extension_names[0].c_str(), extension_names[1].c_str(),
        extension_names.size() - 2));
#else
    NOTREACHED();
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)
  }
};

#if BUILDFLAG(ENABLE_EXTENSIONS)
TEST_F(HidPinnedNotificationTest, SingleProfileEmptyNameExtensionOrigins) {
  // Current TestingProfileManager can't support empty profile name as it uses
  // profile name for profile path. Passing empty would result in a failure in
  // ProfileManager::IsAllowedProfilePath(). Changing the way
  // TestingProfileManager creating profile path like adding "profile" prefix
  // doesn't work either as some tests are written in a way that takes
  // assumption of testing profile path pattern. Hence it creates testing
  // profile with non-empty name and then change the profile name to empty which
  // can still achieve what this file wants to test.
  profile()->set_profile_name("");
  TestSingleProfileExtentionOrigins();
}

TEST_F(HidPinnedNotificationTest, BounceConnectionExtensionOrigins) {
  TestBounceConnectionExtensionOrigins();
}

TEST_F(HidPinnedNotificationTest, SingleProfileNonEmptyNameExtentionOrigins) {
  TestSingleProfileExtentionOrigins();
}

TEST_F(HidPinnedNotificationTest, MultipleProfilesExtentionOrigins) {
  TestMultipleProfilesExtensionOrigins();
}

TEST_F(HidPinnedNotificationTest, ExtensionRemoval) {
  TestExtensionRemoval();
}

// Test message in pinned notification for up to 5 extensions.
TEST_F(HidPinnedNotificationTest, MultipleExtensionsNotificationMessage) {
  TestMultipleExtensionsNotificationMessage();
}
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)
