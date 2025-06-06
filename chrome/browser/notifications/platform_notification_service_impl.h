// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NOTIFICATIONS_PLATFORM_NOTIFICATION_SERVICE_IMPL_H_
#define CHROME_BROWSER_NOTIFICATIONS_PLATFORM_NOTIFICATION_SERVICE_IMPL_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

#include "base/gtest_prod_util.h"
#include "base/memory/raw_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "chrome/browser/notifications/notification_common.h"
#include "chrome/browser/notifications/notification_trigger_scheduler.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/buildflags.h"
#include "components/content_settings/core/browser/content_settings_observer.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/webapps/common/web_app_id.h"
#include "content/public/browser/platform_notification_service.h"
#include "services/metrics/public/cpp/ukm_source_id.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/message_center/public/cpp/notification.h"

class GURL;
class Profile;

namespace blink {
struct NotificationResources;
}  // namespace blink

// The platform notification service is the profile-specific entry point through
// which Web Notifications can be controlled.
class PlatformNotificationServiceImpl
    : public content::PlatformNotificationService,
      public content_settings::Observer,
      public KeyedService {
 public:
  explicit PlatformNotificationServiceImpl(Profile* profile);
  PlatformNotificationServiceImpl(const PlatformNotificationServiceImpl&) =
      delete;
  PlatformNotificationServiceImpl& operator=(
      const PlatformNotificationServiceImpl&) = delete;
  ~PlatformNotificationServiceImpl() override;

  // Register profile-specific prefs.
  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

  // Returns whether the notification identified by |notification_id| was
  // closed programmatically through ClosePersistentNotification().
  bool WasClosedProgrammatically(const std::string& notification_id);

  // content::PlatformNotificationService implementation.
  void DisplayNotification(
      const std::string& notification_id,
      const GURL& origin,
      const GURL& document_url,
      const blink::PlatformNotificationData& notification_data,
      const blink::NotificationResources& notification_resources) override;
  void DisplayPersistentNotification(
      const std::string& notification_id,
      const GURL& service_worker_scope,
      const GURL& origin,
      const blink::PlatformNotificationData& notification_data,
      const blink::NotificationResources& notification_resources) override;
  void CloseNotification(const std::string& notification_id) override;
  void ClosePersistentNotification(const std::string& notification_id) override;
  void GetDisplayedNotifications(
      DisplayedNotificationsCallback callback) override;
  void GetDisplayedNotificationsForOrigin(
      const GURL& origin,
      DisplayedNotificationsCallback callback) override;
  void ScheduleTrigger(base::Time timestamp) override;
  base::Time ReadNextTriggerTimestamp() override;
  int64_t ReadNextPersistentNotificationId() override;
  void RecordNotificationUkmEvent(
      const content::NotificationDatabaseData& data) override;

  void set_ukm_recorded_closure_for_testing(base::OnceClosure closure) {
    ukm_recorded_closure_for_testing_ = std::move(closure);
  }

  NotificationTriggerScheduler* GetNotificationTriggerScheduler();

 private:
  friend class NotificationTriggerSchedulerTest;
  friend class PersistentNotificationHandlerTest;
  friend class PlatformNotificationServiceBrowserTest;
  friend class PlatformNotificationServiceTest;
  friend class PushMessagingBrowserTest;
  FRIEND_TEST_ALL_PREFIXES(PlatformNotificationServiceTest,
                           CreateNotificationFromData);
  FRIEND_TEST_ALL_PREFIXES(PlatformNotificationServiceTest_WebApps,
                           CreateNotificationFromData);
  FRIEND_TEST_ALL_PREFIXES(PlatformNotificationServiceTest,
                           DisplayNameForContextMessage);
  FRIEND_TEST_ALL_PREFIXES(PlatformNotificationServiceTest,
                           RecordNotificationUkmEvent);
  FRIEND_TEST_ALL_PREFIXES(PlatformNotificationServiceTest_WebApps,
                           IncomingCallWebApp);
  FRIEND_TEST_ALL_PREFIXES(
      PlatformNotificationServiceTest_WebAppNotificationIconAndTitle,
      FindWebAppIconAndTitle_NoApp);
  FRIEND_TEST_ALL_PREFIXES(
      PlatformNotificationServiceTest_WebAppNotificationIconAndTitle,
      FindWebAppIconAndTitle);
  FRIEND_TEST_ALL_PREFIXES(
      PlatformNotificationServiceTest_ReportNotificationContentDetectionData,
      UpdateNotificationDatabaseMetadata);

  struct WebAppIconAndTitle {
    gfx::ImageSkia icon;
    std::u16string title;
  };

  // KeyedService implementation.
  void Shutdown() override;

  // content_settings::Observer implementation.
  void OnContentSettingChanged(
      const ContentSettingsPattern& primary_pattern,
      const ContentSettingsPattern& secondary_pattern,
      ContentSettingsTypeSet content_type_set) override;

  static void RecordNotificationUkmEventWithSourceId(
      base::OnceClosure recorded_closure,
      const content::NotificationDatabaseData& data,
      ukm::SourceId source_id);

  // Creates a new Web Notification-based Notification object. Should only be
  // called when the notification is first shown. |web_app_hint_url| is used to
  // find a corresponding web app, it can be a service worker scope or document
  // url.
  message_center::Notification CreateNotificationFromData(
      const GURL& origin,
      const std::string& notification_id,
      const blink::PlatformNotificationData& notification_data,
      const blink::NotificationResources& notification_resources,
      const GURL& web_app_hint_url) const;

  // Returns a display name for an origin, to be used in the context message
  std::u16string DisplayNameForContextMessage(const GURL& origin) const;

  // Finds the AppId associated with |web_app_hint_url| when this is part of
  // an installed experience, and the notification can be attributed as such.
  std::optional<webapps::AppId> FindWebAppId(
      const GURL& web_app_hint_url) const;

  // Finds the icon and title associated with |web_app_id| when this
  // is part of an installed experience, and the notification can be attributed
  // as such.
  std::optional<WebAppIconAndTitle> FindWebAppIconAndTitle(
      const GURL& web_app_hint_url) const;

  // Identifies whether the notification was sent from an installed web app or
  // not.
  bool IsActivelyInstalledWebAppScope(const GURL& web_app_url) const;

  // Clears |closed_notifications_|. Should only be used for testing purposes.
  void ClearClosedNotificationsForTesting() { closed_notifications_.clear(); }

  // Update the notification entry in the `NotificationDatabase` with
  // `serialized_content_detection_metadata` for possible MQLS logging later.
  // Update `persistent_metadata`, given the value of `should_show_warning`, to
  // tell the front end whether to display the notification or the warning.
  void UpdatePersistentMetadataThenDisplay(
      const message_center::Notification& notification,
      std::unique_ptr<PersistentNotificationMetadata> persistent_metadata,
      bool should_show_warning,
      std::optional<std::string> serialized_content_detection_metadata);

  // Logs metrics when displaying a persistent notification.
  void LogPersistentNotificationShownMetrics(
      const blink::PlatformNotificationData& notification_data,
      const GURL& origin,
      const GURL& notification_origin);

  // Returns true if the user tapped "Always allow" on a notification warning
  // for `origin`.
  bool AreSuspiciousNotificationsAllowlistedByUser(const GURL& origin);

  // `WriteResourcesResultCallback` callback that updates the
  // `persistent_metadata` and displays the notification with a call to
  // `DoUpdatePersistentMetadataThenDisplay`, after updating the notification
  // database with serialized metadata. Note the `success` value is currently
  // unused.
  void DidUpdatePersistentMetadata(
      std::unique_ptr<PersistentNotificationMetadata> persistent_metadata,
      message_center::Notification notification,
      bool should_show_warning,
      bool success);

  // Helper method for updating `persistent_metadata`, given the value of
  // `should_show_warning` then displaying the notification.
  void DoUpdatePersistentMetadataThenDisplay(
      std::unique_ptr<PersistentNotificationMetadata> persistent_metadata,
      message_center::Notification notification,
      bool should_show_warning);

  // The profile for this instance or NULL if the initial profile has been
  // shutdown already.
  raw_ptr<Profile> profile_;

  // Tracks the id of persistent notifications that have been closed
  // programmatically to avoid dispatching close events for them.
  std::unordered_set<std::string> closed_notifications_;

  // Scheduler for notifications with a trigger.
  std::unique_ptr<NotificationTriggerScheduler> trigger_scheduler_;

  // Testing-only closure to observe when a UKM event has been recorded.
  base::OnceClosure ukm_recorded_closure_for_testing_;

  base::WeakPtrFactory<PlatformNotificationServiceImpl> weak_ptr_factory_{this};
};

#endif  // CHROME_BROWSER_NOTIFICATIONS_PLATFORM_NOTIFICATION_SERVICE_IMPL_H_
