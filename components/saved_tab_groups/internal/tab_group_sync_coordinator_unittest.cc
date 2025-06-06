// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/uuid.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/testing_pref_service.h"
#include "components/saved_tab_groups/delegate/tab_group_sync_delegate.h"
#include "components/saved_tab_groups/internal/tab_group_sync_coordinator_impl.h"
#include "components/saved_tab_groups/public/pref_names.h"
#include "components/saved_tab_groups/public/saved_tab_group.h"
#include "components/saved_tab_groups/public/types.h"
#include "components/saved_tab_groups/test_support/mock_tab_group_sync_delegate.h"
#include "components/saved_tab_groups/test_support/mock_tab_group_sync_service.h"
#include "components/saved_tab_groups/test_support/saved_tab_group_test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::Eq;
using testing::Return;

namespace tab_groups {
namespace {

MATCHER_P(UuidEq, uuid, "") {
  return arg.saved_guid() == uuid;
}

}  // namespace

class TabGroupSyncCoordinatorTest : public testing::Test {
 public:
  TabGroupSyncCoordinatorTest()
      : local_group_id_1_(test::GenerateRandomTabGroupID()),
        group_1_(test::CreateTestSavedTabGroup()) {}

  ~TabGroupSyncCoordinatorTest() override = default;

  void SetUp() override {
    pref_service_.registry()->RegisterBooleanPref(
        prefs::kDidSyncTabGroupsInLastSession, true);
    service_ = std::make_unique<MockTabGroupSyncService>();
    auto delegate = std::make_unique<MockTabGroupSyncDelegate>();
    delegate_ = delegate.get();
    coordinator_ = std::make_unique<TabGroupSyncCoordinatorImpl>(
        std::move(delegate), service_.get(), &pref_service_);
  }

  void TearDown() override { delegate_ = nullptr; }

 protected:
  TestingPrefServiceSimple pref_service_;
  std::unique_ptr<MockTabGroupSyncService> service_;
  raw_ptr<MockTabGroupSyncDelegate> delegate_;
  std::unique_ptr<TabGroupSyncCoordinatorImpl> coordinator_;
  LocalTabGroupID local_group_id_1_;
  SavedTabGroup group_1_;
};

TEST_F(TabGroupSyncCoordinatorTest, HandleOpenTabGroupRequest) {
  base::Uuid sync_id = base::Uuid::GenerateRandomV4();
  auto context = std::make_unique<TabGroupActionContext>();

  EXPECT_CALL(*delegate_, HandleOpenTabGroupRequest(sync_id, testing::_))
      .Times(1);
  coordinator_->HandleOpenTabGroupRequest(sync_id, std::move(context));
}

TEST_F(TabGroupSyncCoordinatorTest, ConnectLocalTabGroup) {
  SavedTabGroup group(test::CreateTestSavedTabGroup());
  base::Uuid sync_id = group.saved_guid();
  LocalTabGroupID local_id = test::GenerateRandomTabGroupID();

  EXPECT_CALL(*service_, GetGroup(sync_id))
      .Times(2)
      .WillRepeatedly(Return(group));
  EXPECT_CALL(*delegate_, ConnectLocalTabGroup(UuidEq(sync_id))).Times(1);

  coordinator_->ConnectLocalTabGroup(sync_id, local_id);
}

TEST_F(TabGroupSyncCoordinatorTest, OnTabGroupAdded) {
  SavedTabGroup group(test::CreateTestSavedTabGroup());

  EXPECT_CALL(*delegate_, CreateLocalTabGroup(UuidEq(group.saved_guid())))
      .Times(1);
  coordinator_->OnTabGroupAdded(group, TriggerSource::REMOTE);
}

TEST_F(TabGroupSyncCoordinatorTest, OnTabGroupAdded_DoesNotReopenClosedGroup) {
  SavedTabGroup group(test::CreateTestSavedTabGroup());

  ON_CALL(*service_, WasTabGroupClosedLocally(group.saved_guid()))
      .WillByDefault(Return(true));

  // Since the group is marked as locally-closed, it should *not* get opened!
  EXPECT_CALL(*delegate_, CreateLocalTabGroup(UuidEq(group.saved_guid())))
      .Times(0);
  coordinator_->OnTabGroupAdded(group, TriggerSource::REMOTE);
}

TEST_F(TabGroupSyncCoordinatorTest, OnTabGroupUpdated) {
  SavedTabGroup group(test::CreateTestSavedTabGroup());

  EXPECT_CALL(*delegate_, UpdateLocalTabGroup(UuidEq(group.saved_guid())))
      .Times(1);
  coordinator_->OnTabGroupUpdated(group, TriggerSource::REMOTE);
}

TEST_F(TabGroupSyncCoordinatorTest, OnTabGroupRemoved) {
  LocalTabGroupID local_id = test::GenerateRandomTabGroupID();

  EXPECT_CALL(
      *delegate_,
      CloseLocalTabGroup(testing::TypedEq<const LocalTabGroupID&>(local_id)))
      .Times(1);
  coordinator_->OnTabGroupRemoved(local_id, TriggerSource::REMOTE);
}

TEST_F(TabGroupSyncCoordinatorTest, OnTabGroupRemovedWithSyncId) {
  base::Uuid sync_id = base::Uuid::GenerateRandomV4();

  EXPECT_CALL(*delegate_, CloseLocalTabGroup(testing::_)).Times(0);
  coordinator_->OnTabGroupRemoved(sync_id, TriggerSource::REMOTE);
}

TEST_F(TabGroupSyncCoordinatorTest, OnTabGroupAddedFromLocal) {
  SavedTabGroup group(test::CreateTestSavedTabGroup());

  EXPECT_CALL(*delegate_, CreateLocalTabGroup(_)).Times(0);
  coordinator_->OnTabGroupAdded(group, TriggerSource::LOCAL);
}

TEST_F(TabGroupSyncCoordinatorTest, OnTabGroupUpdatedFromLocal) {
  SavedTabGroup group(test::CreateTestSavedTabGroup());

  EXPECT_CALL(*delegate_, UpdateLocalTabGroup(_)).Times(0);
  coordinator_->OnTabGroupUpdated(group, TriggerSource::LOCAL);
}

TEST_F(TabGroupSyncCoordinatorTest, OnTabGroupRemovedFromLocal) {
  LocalTabGroupID local_id = test::GenerateRandomTabGroupID();

  EXPECT_CALL(*delegate_, CloseLocalTabGroup(_)).Times(0);
  coordinator_->OnTabGroupRemoved(local_id, TriggerSource::LOCAL);
}

TEST_F(TabGroupSyncCoordinatorTest, ReconcileGroupsToSync) {
  group_1_.SetLocalGroupId(local_group_id_1_);
  std::vector<const SavedTabGroup*> groups = {&group_1_};

  EXPECT_CALL(*service_, ReadAllGroups()).WillRepeatedly(Return(groups));
  EXPECT_CALL(*service_, GetGroup(group_1_.saved_guid()))
      .WillRepeatedly(Return(group_1_));

  EXPECT_CALL(*delegate_, UpdateLocalTabGroup(_)).Times(1);

  coordinator_->OnInitialized();
}

// Desktop Platforms do not use Startup Helper to perform initializing
// actions.
#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
#define MAYBE_SaveUnsavedLocalGroupsOnStartupForFirstTimeFeatureLaunch \
  SaveUnsavedLocalGroupsOnStartupForFirstTimeFeatureLaunch
#else
#define MAYBE_SaveUnsavedLocalGroupsOnStartupForFirstTimeFeatureLaunch \
  DISABLED_SaveUnsavedLocalGroupsOnStartupForFirstTimeFeatureLaunch
#endif
TEST_F(TabGroupSyncCoordinatorTest,
       MAYBE_SaveUnsavedLocalGroupsOnStartupForFirstTimeFeatureLaunch) {
  pref_service_.SetBoolean(prefs::kDidSyncTabGroupsInLastSession, false);
  EXPECT_CALL(*service_, ReadAllGroups())
      .WillRepeatedly(Return(std::vector<const SavedTabGroup*>()));

  LocalTabGroupID local_id = test::GenerateRandomTabGroupID();
  EXPECT_CALL(*service_,
              GetGroup(testing::TypedEq<const LocalTabGroupID&>(local_id)))
      .WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(*delegate_, GetLocalTabGroupIds())
      .WillRepeatedly(Return(std::vector<LocalTabGroupID>({local_id})));

  auto saved_tab_group_1 = std::make_unique<SavedTabGroup>(group_1_);
  EXPECT_CALL(*delegate_, CreateSavedTabGroupFromLocalGroup(Eq(local_id)))
      .WillOnce(Return(std::move(saved_tab_group_1)));
  EXPECT_CALL(*service_, AddGroup(_)).Times(1);
  coordinator_->OnInitialized();
}

// Desktop Platforms do not use Startup Helper to perform initializing
// actions.
#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
#define MAYBE_CloseUnsavedLocalGroupsOnStartup CloseUnsavedLocalGroupsOnStartup
#else
#define MAYBE_CloseUnsavedLocalGroupsOnStartup \
  DISABLED_CloseUnsavedLocalGroupsOnStartup
#endif
TEST_F(TabGroupSyncCoordinatorTest, MAYBE_CloseUnsavedLocalGroupsOnStartup) {
  pref_service_.SetBoolean(prefs::kDidSyncTabGroupsInLastSession, true);
  EXPECT_CALL(*service_, ReadAllGroups())
      .WillRepeatedly(Return(std::vector<const SavedTabGroup*>()));

  LocalTabGroupID local_id = test::GenerateRandomTabGroupID();
  EXPECT_CALL(*service_,
              GetGroup(testing::TypedEq<const LocalTabGroupID&>(local_id)))
      .WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(*delegate_, GetLocalTabGroupIds())
      .WillRepeatedly(Return(std::vector<LocalTabGroupID>({local_id})));

  EXPECT_CALL(*delegate_, CloseLocalTabGroup(Eq(local_id))).Times(1);
  coordinator_->OnInitialized();
}

}  // namespace tab_groups
