// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/glanceables/glanceables_keyed_service.h"

#include <memory>
#include <string>

#include "ash/glanceables/glanceables_controller.h"
#include "ash/shell.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "chrome/test/base/testing_profile_manager.h"
#include "google_apis/gaia/gaia_id.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ash {
namespace {

constexpr char kPrimaryProfileName[] = "primary_profile@example.com";
constexpr char kSecondaryProfileName[] = "secondary_profile@example.com";
constexpr GaiaId::Literal kFakeGaia2("fakegaia2");

}  // namespace

class GlanceablesKeyedServiceTest : public BrowserWithTestWindowTest {
 public:
  // BrowserWithTestWindowTest:
  std::optional<std::string> GetDefaultProfileName() override {
    return kPrimaryProfileName;
  }

  // BrowserWithTestWindowTest:
  TestingProfile* CreateProfile(const std::string& profile_name) override {
    EXPECT_EQ(kPrimaryProfileName, profile_name);
    return profile_manager()->CreateTestingProfile(profile_name);
  }
};

TEST_F(GlanceablesKeyedServiceTest, RegistersClientsInAsh) {
  auto* const controller = Shell::Get()->glanceables_controller();
  EXPECT_FALSE(controller->GetClassroomClient());
  EXPECT_FALSE(controller->GetTasksClient());

  auto service = std::make_unique<GlanceablesKeyedService>(profile());
  EXPECT_TRUE(controller->GetClassroomClient());
  EXPECT_TRUE(controller->GetTasksClient());

  service->Shutdown();
  EXPECT_FALSE(controller->GetClassroomClient());
  EXPECT_FALSE(controller->GetTasksClient());
}

TEST_F(GlanceablesKeyedServiceTest, RegisterClientsInAshForNonPrimaryUser) {
  auto* const controller = Shell::Get()->glanceables_controller();
  auto service = std::make_unique<GlanceablesKeyedService>(profile());
  auto* const classroom_client_primary = controller->GetClassroomClient();
  auto* const tasks_client_primary = controller->GetTasksClient();
  EXPECT_TRUE(classroom_client_primary);
  EXPECT_TRUE(tasks_client_primary);

  LogIn(kSecondaryProfileName, kFakeGaia2);
  auto* secondary_profile =
      profile_manager()->CreateTestingProfile(kSecondaryProfileName);
  SwitchActiveUser(kSecondaryProfileName);
  auto service_secondary =
      std::make_unique<GlanceablesKeyedService>(secondary_profile);

  auto* const classroom_client_secondary = controller->GetClassroomClient();
  auto* const tasks_client_secondary = controller->GetTasksClient();
  EXPECT_TRUE(classroom_client_secondary);
  EXPECT_TRUE(tasks_client_secondary);
  EXPECT_NE(classroom_client_primary, classroom_client_secondary);
  EXPECT_NE(tasks_client_primary, tasks_client_secondary);

  SwitchActiveUser(kPrimaryProfileName);
  EXPECT_EQ(classroom_client_primary, controller->GetClassroomClient());
  EXPECT_EQ(tasks_client_primary, controller->GetTasksClient());
}

}  // namespace ash
