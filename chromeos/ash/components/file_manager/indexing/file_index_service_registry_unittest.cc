// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/ash/components/file_manager/indexing/file_index_service_registry.h"

#include <memory>

#include "ash/constants/ash_features.h"
#include "ash/constants/ash_switches.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "chromeos/ash/components/browser_context_helper/browser_context_helper.h"
#include "chromeos/ash/components/browser_context_helper/fake_browser_context_helper_delegate.h"
#include "components/account_id/account_id.h"
#include "components/prefs/testing_pref_service.h"
#include "components/user_manager/fake_user_manager.h"
#include "components/user_manager/scoped_user_manager.h"
#include "components/user_manager/test_helper.h"
#include "components/user_manager/user_manager.h"
#include "google_apis/gaia/gaia_id.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {
constexpr char kTestAccount[] = "primary-test@gmail.com";
constexpr GaiaId::Literal kFakeGaia("fakegaia");
constexpr char kTestAccount2[] = "secondary-test@gmail.com";
constexpr GaiaId::Literal kFakeGaia2("fakegaia2");
}  // namespace

namespace ash::file_manager {
class FileIndexServiceRegistryTest : public testing::Test {
 public:
  void SetUp() override {
    user_manager::UserManager::RegisterPrefs(local_state_.registry());
    browser_context_helper_ = std::make_unique<ash::BrowserContextHelper>(
        std::make_unique<ash::FakeBrowserContextHelperDelegate>());
    fake_user_manager_.Reset(
        std::make_unique<user_manager::FakeUserManager>(&local_state_));
    registry_ =
        std::make_unique<FileIndexServiceRegistry>(fake_user_manager_->Get());
    primary_account_id_ =
        AccountId::FromUserEmailGaiaId(kTestAccount, kFakeGaia);
    fake_user_manager()->AddGaiaUser(primary_account_id_,
                                     user_manager::UserType::kRegular);
  }

  void TearDown() override {
    registry_->Shutdown();
    registry_.reset();

    fake_user_manager_.Reset();
    browser_context_helper_.reset();
  }

  void LoginUser(const AccountId& account_id) {
    fake_user_manager()->UserLoggedIn(
        account_id, user_manager::TestHelper::GetFakeUsernameHash(account_id));
    fake_user_manager()->OnUserProfileCreated(account_id, nullptr);
  }

  user_manager::FakeUserManager* fake_user_manager() const {
    return fake_user_manager_.Get();
  }

  const AccountId& primary_account_id() const { return primary_account_id_; }

 private:
  base::test::ScopedFeatureList features{
      ::ash::features::kFilesMaterializedViews};

  AccountId primary_account_id_;
  std::unique_ptr<FileIndexServiceRegistry> registry_;

  TestingPrefServiceSimple local_state_;
  std::unique_ptr<ash::BrowserContextHelper> browser_context_helper_;
  user_manager::TypedScopedUserManager<user_manager::FakeUserManager>
      fake_user_manager_;
  base::test::TaskEnvironment task_environment_;
};

TEST_F(FileIndexServiceRegistryTest, PrimaryUser) {
  auto* registry = FileIndexServiceRegistry::Get();
  EXPECT_NE(registry, nullptr);

  // Null before login.
  EXPECT_EQ(registry->GetFileIndexService(primary_account_id()), nullptr);
  LoginUser(primary_account_id());
  auto* primary_index = registry->GetFileIndexService(primary_account_id());

  // Get an instance after login.
  EXPECT_NE(primary_index, nullptr);

  // Callign multiple times gets the same instance
  EXPECT_EQ(registry->GetFileIndexService(primary_account_id()), primary_index);
}

TEST_F(FileIndexServiceRegistryTest, SecondaryUser) {
  auto* registry = FileIndexServiceRegistry::Get();
  EXPECT_NE(registry, nullptr);

  LoginUser(primary_account_id());
  auto* primary_index = registry->GetFileIndexService(primary_account_id());
  EXPECT_NE(primary_index, nullptr);

  auto secondary_account_id =
      AccountId::FromUserEmailGaiaId(kTestAccount2, kFakeGaia2);
  fake_user_manager()->AddGaiaUser(secondary_account_id,
                                   user_manager::UserType::kRegular);
  EXPECT_EQ(registry->GetFileIndexService(secondary_account_id), nullptr);
  LoginUser(secondary_account_id);
  auto* secondary_index = registry->GetFileIndexService(secondary_account_id);
  EXPECT_NE(secondary_index, nullptr);

  // Primary and secondary users get different instances.
  EXPECT_NE(secondary_index, primary_index);
}

}  // namespace ash::file_manager
