// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/unified/notification_icons_controller.h"

#include "ash/constants/ash_constants.h"
#include "ash/constants/ash_pref_names.h"
#include "ash/constants/notifier_catalogs.h"
#include "ash/public/cpp/notification_utils.h"
#include "ash/public/cpp/shelf_prefs.h"
#include "ash/public/cpp/shelf_types.h"
#include "ash/public/cpp/vm_camera_mic_constants.h"
#include "ash/shelf/shelf.h"
#include "ash/strings/grit/ash_strings.h"
#include "ash/system/notification_center/notification_center_tray.h"
#include "ash/system/tray/tray_item_view.h"
#include "ash/system/unified/notification_counter_view.h"
#include "ash/test/ash_test_base.h"
#include "base/strings/string_number_conversions.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/message_center/message_center.h"
#include "ui/message_center/public/cpp/notification.h"
#include "ui/views/accessibility/view_accessibility.h"

namespace ash {

namespace {
const char kCapsLockNotifierId[] = "ash.caps-lock";
const char kBatteryNotificationNotifierId[] = "ash.battery";
const char kUsbNotificationNotifierId[] = "ash.power";
constexpr int kIconsViewDisplaySizeThreshold = 768;
}  // namespace

class NotificationIconsControllerTest : public AshTestBase {
 public:
  NotificationIconsControllerTest() = default;
  ~NotificationIconsControllerTest() override = default;

  std::string AddNotification(bool is_pinned,
                              bool is_critical_warning,
                              const std::string& notifier_id = "app") {
    std::string id = base::NumberToString(notification_id_++);

    auto warning_level =
        is_critical_warning
            ? message_center::SystemNotificationWarningLevel::CRITICAL_WARNING
            : message_center::SystemNotificationWarningLevel::NORMAL;
    message_center::RichNotificationData rich_notification_data;
    rich_notification_data.pinned = is_pinned;

    message_center::MessageCenter::Get()->AddNotification(
        CreateSystemNotificationPtr(
            message_center::NOTIFICATION_TYPE_SIMPLE, id, u"test_title",
            u"test message", std::u16string() /*display_source */,
            GURL() /* origin_url */,
            message_center::NotifierId(
                message_center::NotifierType::SYSTEM_COMPONENT, notifier_id,
                NotificationCatalogName::kTestCatalogName),
            rich_notification_data, nullptr /* delegate */,
            gfx::VectorIcon::EmptyIcon(), warning_level));
    notification_id_++;

    return id;
  }

  // Sets the shelf to always auto-hide and simulates logging in to a new user
  // session with that preference set by returning a pointer to an instance of
  // `NotificationIconsController` created after setting the preference.
  std::unique_ptr<NotificationIconsController>
  CreateControllerWithAutoHideShelf() {
    // Clear all user sessions.
    ClearLogin();

    // Log in.
    constexpr char kUserEmail[] = "user@gmail.com";
    SimulateUserLogin({kUserEmail});

    // Set the user's shelf auto-hide preference to always hide.
    auto accountId = AccountId::FromUserEmail(kUserEmail);
    auto* prefs = GetSessionControllerClient()->GetUserPrefService(accountId);
    CHECK(prefs);
    prefs->SetString(prefs::kShelfAutoHideBehaviorLocal,
                     kShelfAutoHideBehaviorAlways);
    prefs->SetString(prefs::kShelfAutoHideBehavior,
                     kShelfAutoHideBehaviorAlways);

    // Verify that the shelf auto-hides by creating and showing a window.
    auto window =
        CreateTestWidget(views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET);
    window->SetBounds(gfx::Rect(0, 0, 100, 100));
    CHECK(GetPrimaryShelf()->GetAutoHideState() ==
          ShelfAutoHideState::SHELF_AUTO_HIDE_HIDDEN);

    // Create a local instance of `NotificationIconsController`. This allows the
    // `NotificationIconsController` constructor to run after the shelf
    // auto-hide preference has been set, which simulates logging into a new
    // user session where that shelf preference is already set.
    return std::make_unique<NotificationIconsController>(
        GetPrimaryShelf(),
        GetPrimaryShelf()->GetStatusAreaWidget()->notification_center_tray());
  }

 protected:
  NotificationIconsController* GetNotificationIconsController() {
    auto* status_area_widget = GetPrimaryShelf()->status_area_widget();
    return status_area_widget->notification_center_tray()
        ->notification_icons_controller_.get();
  }
  int notification_id_ = 0;
};

// Tests `icons_view_visible_` initialization behavior for the case where a
// user logs in and the shelf is already set to auto-hide. It should initialize
// to true when the display size meets or exceeds the threshold.
TEST_F(NotificationIconsControllerTest,
       IconsViewVisibleInitializationForAutoHiddenShelfAndLargeDisplay) {
  // Verify that the display size meets the threshold.
  auto display_size = GetPrimaryDisplay().size();
  ASSERT_GE(std::max(display_size.width(), display_size.height()),
            kIconsViewDisplaySizeThreshold);

  // Set the shelf to always auto-hide and get a new instance of
  // `NotificationIconsController` that was created after setting that
  // preference.
  auto notification_icons_controller = CreateControllerWithAutoHideShelf();

  // Verify that `icons_view_visible_` is true.
  EXPECT_TRUE(notification_icons_controller->icons_view_visible());
}

// Tests `icons_view_visible_` initialization behavior for the case where a
// user logs in and the shelf is already set to auto-hide. It should initialize
// to false when the display size is smaller than the threshold.
TEST_F(NotificationIconsControllerTest,
       IconsViewVisibleInitializationForAutoHiddenShelfAndSmallDisplay) {
  // Update the display to have a size smaller than the threshold.
  UpdateDisplay(base::NumberToString(kIconsViewDisplaySizeThreshold - 1) + "x" +
                base::NumberToString(kIconsViewDisplaySizeThreshold - 2));

  // Verify that the display size does not meet the threshold.
  auto display_size = GetPrimaryDisplay().size();
  ASSERT_LT(std::max(display_size.width(), display_size.height()),
            kIconsViewDisplaySizeThreshold);

  // Set the shelf to always auto-hide and get a new instance of
  // `NotificationIconsController` that was created after setting that
  // preference.
  auto notification_icons_controller = CreateControllerWithAutoHideShelf();

  // Verify that `icons_view_visible_` is false.
  EXPECT_FALSE(notification_icons_controller->icons_view_visible());
}

TEST_F(NotificationIconsControllerTest, DisplayChanged) {
  AddNotification(true /* is_pinned */, false /* is_critical_warning */);
  AddNotification(false /* is_pinned */, false /* is_critical_warning */);

  // Icons get added from RTL, so we check the end of the vector first.

  // Notification icons should be shown in medium screen size.
  UpdateDisplay("800x700");
  EXPECT_TRUE(
      GetNotificationIconsController()->tray_items().back()->GetVisible());

  // Notification icons should not be shown in small screen size.
  UpdateDisplay("600x500");
  EXPECT_FALSE(
      GetNotificationIconsController()->tray_items().back()->GetVisible());

  // Notification icons should be shown in large screen size.
  UpdateDisplay("1680x800");
  EXPECT_TRUE(
      GetNotificationIconsController()->tray_items().back()->GetVisible());
}

TEST_F(NotificationIconsControllerTest, ShowNotificationIcons) {
  UpdateDisplay("800x700");

  // Icons get added from RTL, so we check the end of the vector first.
  const int end = GetNotificationIconsController()->tray_items().size() - 1;

  // Ensure that the indexes that will be accessed exist.
  ASSERT_TRUE(GetNotificationIconsController()->tray_items().size() >= 2);

  // If there's no notification, no notification icons should be shown.
  EXPECT_FALSE(
      GetNotificationIconsController()->tray_items()[end]->GetVisible());
  EXPECT_FALSE(
      GetNotificationIconsController()->tray_items()[end - 1]->GetVisible());

  // Same case for non pinned or non critical warning notification.
  AddNotification(false /* is_pinned */, false /* is_critical_warning */);
  EXPECT_FALSE(
      GetNotificationIconsController()->tray_items()[end]->GetVisible());
  EXPECT_FALSE(
      GetNotificationIconsController()->tray_items()[end - 1]->GetVisible());

  // Notification icons should be shown when pinned or critical warning
  // notification is added.
  std::string id0 =
      AddNotification(true /* is_pinned */, false /* is_critical_warning */);
  EXPECT_TRUE(
      GetNotificationIconsController()->tray_items()[end]->GetVisible());
  EXPECT_FALSE(
      GetNotificationIconsController()->tray_items()[end - 1]->GetVisible());

  std::string id1 =
      AddNotification(false /* is_pinned */, true /* is_critical_warning */);
  EXPECT_TRUE(
      GetNotificationIconsController()->tray_items()[end]->GetVisible());
  EXPECT_TRUE(
      GetNotificationIconsController()->tray_items()[end - 1]->GetVisible());

  // Remove the critical warning notification should make the tray show only one
  // icon.
  message_center::MessageCenter::Get()->RemoveNotification(id1,
                                                           false /* by_user */);
  EXPECT_TRUE(
      GetNotificationIconsController()->tray_items()[end]->GetVisible());
  EXPECT_FALSE(
      GetNotificationIconsController()->tray_items()[end - 1]->GetVisible());

  // Remove the pinned notification, no icon is shown.
  message_center::MessageCenter::Get()->RemoveNotification(id0,
                                                           false /* by_user */);
  EXPECT_FALSE(
      GetNotificationIconsController()->tray_items()[end]->GetVisible());
  EXPECT_FALSE(
      GetNotificationIconsController()->tray_items()[end - 1]->GetVisible());
}

TEST_F(NotificationIconsControllerTest, NotShowNotificationIcons) {
  UpdateDisplay("800x700");

  // Icons get added from RTL, so we check the end of the vector first.

  EXPECT_FALSE(
      GetNotificationIconsController()->tray_items().back()->GetVisible());

  AddNotification(true /* is_pinned */, false /* is_critical_warning */,
                  kBatteryNotificationNotifierId);
  // Battery notification should not be shown.
  EXPECT_FALSE(
      GetNotificationIconsController()->tray_items().back()->GetVisible());

  // Notification count does update for this notification.
  GetNotificationIconsController()->notification_counter_view()->Update();
  EXPECT_EQ(1, GetNotificationIconsController()
                   ->notification_counter_view()
                   ->count_for_display_for_testing());

  AddNotification(true /* is_pinned */, false /* is_critical_warning */,
                  kUsbNotificationNotifierId);
  // Usb charging notification should not be shown.
  EXPECT_FALSE(
      GetNotificationIconsController()->tray_items().back()->GetVisible());

  // Notification count does update for this notification.
  GetNotificationIconsController()->notification_counter_view()->Update();
  EXPECT_EQ(2, GetNotificationIconsController()
                   ->notification_counter_view()
                   ->count_for_display_for_testing());

  AddNotification(true /* is_pinned */, false /* is_critical_warning */,
                  kVmCameraMicNotifierId);

  // VM camera/mic notification should not be shown.
  EXPECT_FALSE(
      GetNotificationIconsController()->tray_items().back()->GetVisible());

  // Notification count does not update for this notification (since there's
  // another tray item for this).
  GetNotificationIconsController()->notification_counter_view()->Update();
  EXPECT_EQ(2, GetNotificationIconsController()
                   ->notification_counter_view()
                   ->count_for_display_for_testing());

  AddNotification(true /* is_pinned */, false /* is_critical_warning */,
                  kPrivacyIndicatorsNotifierId);

  // Privacy indicator notification should not be shown.
  EXPECT_FALSE(
      GetNotificationIconsController()->tray_items().back()->GetVisible());

  // Notification count does not update for this notification (since there's
  // another tray item for this).
  GetNotificationIconsController()->notification_counter_view()->Update();
  EXPECT_EQ(2, GetNotificationIconsController()
                   ->notification_counter_view()
                   ->count_for_display_for_testing());
}

TEST_F(NotificationIconsControllerTest, NotificationItemInQuietMode) {
  UpdateDisplay("800x700");
  message_center::MessageCenter::Get()->SetQuietMode(true);

  // Icons get added from RTL, so we check the end of the vector first. At
  // first, no icons should be shown.
  EXPECT_FALSE(
      GetNotificationIconsController()->tray_items().back()->GetVisible());

  // In quiet mode, notification other than capslock notification should not
  // show an item in the tray.
  auto id1 = AddNotification(/*is_pinned=*/true, /*is_critical_warning=*/false);
  EXPECT_FALSE(
      GetNotificationIconsController()->tray_items().back()->GetVisible());

  auto id2 = AddNotification(/*is_pinned=*/true, /*is_critical_warning=*/false,
                             kCapsLockNotifierId);
  EXPECT_TRUE(
      GetNotificationIconsController()->tray_items().back()->GetVisible());
  EXPECT_EQ(id2, GetNotificationIconsController()
                     ->tray_items()
                     .back()
                     ->GetNotificationId());

  message_center::MessageCenter::Get()->RemoveNotification(id2,
                                                           /*by_user=*/false);
  EXPECT_FALSE(
      GetNotificationIconsController()->tray_items().back()->GetVisible());
}

// This is a large test that captures the nominal cases that can impact the name
// of the NotificationCenterTray. This test lives in
// NotificationIconsControllerTest and not NotificationCenterTrayTest because
// most of the logic to calculate the tray's accessible name lives in
// |NotificationIconsController::GetAccessibleNameString|. This test looks at
// each part of that function and tests the logic.
TEST_F(NotificationIconsControllerTest, NotificationCenterTrayAccessibleName) {
  NotificationCenterTray* tray =
      GetPrimaryShelf()->GetStatusAreaWidget()->notification_center_tray();
  NotificationIconsController* controller = GetNotificationIconsController();

  // In quiet mode, the tray's accessible name should be the same as the
  // QuietModeView's tooltip text.
  UpdateDisplay("800x700");
  message_center::MessageCenter::Get()->SetQuietMode(true);
  EXPECT_TRUE(controller->quiet_mode_view()->GetVisible());
  {
    ui::AXNodeData node_data;
    tray->GetViewAccessibility().GetAccessibleNodeData(&node_data);
    EXPECT_EQ(
        node_data.GetString16Attribute(ax::mojom::StringAttribute::kName),
        l10n_util::GetStringUTF16(IDS_ASH_STATUS_TRAY_QUIET_MODE_TOOLTIP));
  }

  // Keep quiet_mode_view object, but set it invisible. Since there are no
  // notifications and the NotificationCounterView is not visible, the
  // accessible name should be set to the NotificationCenterTray's default
  // string option.
  message_center::MessageCenter::Get()->SetQuietMode(false);
  EXPECT_FALSE(controller->quiet_mode_view()->GetVisible());
  EXPECT_FALSE(controller->notification_counter_view()->GetVisible());
  EXPECT_FALSE(controller->TrayItemHasNotification());
  {
    ui::AXNodeData node_data;
    tray->GetViewAccessibility().GetAccessibleNodeData(&node_data);
    EXPECT_EQ(
        node_data.GetString16Attribute(ax::mojom::StringAttribute::kName),
        l10n_util::GetStringUTF16(IDS_ASH_MESSAGE_CENTER_ACCESSIBLE_NAME));
  }

  // Ensure that the accessible name is correctly set when there is a
  // notification but the NotificationCounterView is not visible because there
  // are fewer than 3 notifications.
  AddNotification(true /* is_pinned */, false /* is_critical_warning */);
  EXPECT_TRUE(controller->TrayItemHasNotification());
  EXPECT_TRUE(controller->TrayNotificationIconsCount() == 1);
  // The status object will hold all the status information that will be
  // concatenated into the accessible name. This series of logic is following
  // and testing the logic in
  // NotificationIconsController::GetAccessibleNameString().
  std::vector<std::u16string> status;
  status.push_back(l10n_util::GetPluralStringFUTF16(
      IDS_ASH_STATUS_TRAY_NOTIFICATIONS_IMPORTANT_COUNT_ACCESSIBLE_NAME,
      controller->TrayNotificationIconsCount()));
  for (NotificationIconTrayItemView* tray_item : controller->tray_items()) {
    status.push_back(tray_item->GetAccessibleNameString());
  }
  // This is an empty string because NotificationCounterView is not visible.
  status.push_back(std::u16string());
  {
    ui::AXNodeData node_data;
    tray->GetViewAccessibility().GetAccessibleNodeData(&node_data);
    EXPECT_EQ(node_data.GetString16Attribute(ax::mojom::StringAttribute::kName),
              l10n_util::GetStringFUTF16(
                  IDS_ASH_STATUS_TRAY_NOTIFICATIONS_ICONS_ACCESSIBLE_NAME,
                  status, nullptr));
  }

  // Add more notifications, which will make the NotificationCounterView
  // visible, to test that the accessible name will be set correctly.
  AddNotification(true /* is_pinned */, false /* is_critical_warning */);
  AddNotification(true /* is_pinned */, false /* is_critical_warning */);
  AddNotification(true /* is_pinned */, false /* is_critical_warning */);
  controller->notification_counter_view()->Update();
  // Follow the logic in NotificationIconsController::GetAccessibleNameString().
  {
    status = std::vector<std::u16string>();
    status.push_back(l10n_util::GetPluralStringFUTF16(
        IDS_ASH_STATUS_TRAY_NOTIFICATIONS_IMPORTANT_COUNT_ACCESSIBLE_NAME,
        controller->TrayNotificationIconsCount()));
    for (NotificationIconTrayItemView* tray_item : controller->tray_items()) {
      status.push_back(tray_item->GetAccessibleNameString());
    }
    status.push_back(l10n_util::GetPluralStringFUTF16(
        IDS_ASH_STATUS_TRAY_NOTIFICATIONS_HIDDEN_COUNT_TOOLTIP, 2));
    ui::AXNodeData node_data;
    tray->GetViewAccessibility().GetAccessibleNodeData(&node_data);
    EXPECT_EQ(node_data.GetString16Attribute(ax::mojom::StringAttribute::kName),
              l10n_util::GetStringFUTF16(
                  IDS_ASH_STATUS_TRAY_NOTIFICATIONS_ICONS_ACCESSIBLE_NAME,
                  status, nullptr));
  }

  // Update the tray item tooltip texts for test purposes.
  for (NotificationIconTrayItemView* tray_item : controller->tray_items()) {
    tray_item->image_view()->SetTooltipText(u"test");
  }

  // Follow the logic in NotificationIconsController::GetAccessibleNameString().
  {
    status = std::vector<std::u16string>();
    status.push_back(l10n_util::GetPluralStringFUTF16(
        IDS_ASH_STATUS_TRAY_NOTIFICATIONS_IMPORTANT_COUNT_ACCESSIBLE_NAME,
        controller->TrayNotificationIconsCount()));
    for (NotificationIconTrayItemView* tray_item : controller->tray_items()) {
      status.push_back(tray_item->GetAccessibleNameString());
    }
    status.push_back(l10n_util::GetPluralStringFUTF16(
        IDS_ASH_STATUS_TRAY_NOTIFICATIONS_HIDDEN_COUNT_TOOLTIP, 2));
    ui::AXNodeData node_data;
    tray->GetViewAccessibility().GetAccessibleNodeData(&node_data);
    EXPECT_EQ(node_data.GetString16Attribute(ax::mojom::StringAttribute::kName),
              l10n_util::GetStringFUTF16(
                  IDS_ASH_STATUS_TRAY_NOTIFICATIONS_ICONS_ACCESSIBLE_NAME,
                  status, nullptr));
  }

  message_center::MessageCenter::Get()->RemoveAllNotifications(
      /*by_user=*/false, message_center::MessageCenter::RemoveType::ALL);
  EXPECT_FALSE(controller->TrayItemHasNotification());

  // Force visibility of the notification_counter_view() for the purposes of the
  // next test.
  controller->notification_counter_view()->SetVisible(true);
  EXPECT_TRUE(controller->notification_counter_view()->GetVisible());

  // If there are no notifications but the NotificationCounterView is visible,
  // the accessible name should be the same as the NotificationCounterView's
  // image tooltip text.
  {
    ui::AXNodeData node_data;
    tray->GetViewAccessibility().GetAccessibleNodeData(&node_data);
    EXPECT_EQ(node_data.GetString16Attribute(ax::mojom::StringAttribute::kName),
              controller->notification_counter_view()
                  ->image_view()
                  ->GetTooltipText());
  }

  // If the NotificationCounterView's image tooltip text is updated, the tray
  // accessible name should match.
  controller->notification_counter_view()->image_view()->SetTooltipText(
      u"Test");
  {
    ui::AXNodeData node_data;
    tray->GetViewAccessibility().GetAccessibleNodeData(&node_data);
    EXPECT_EQ(node_data.GetString16Attribute(ax::mojom::StringAttribute::kName),
              u"Test");
  }
}

}  // namespace ash
