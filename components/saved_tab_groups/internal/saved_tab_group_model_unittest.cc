// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/saved_tab_groups/internal/saved_tab_group_model.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/rand_util.h"
#include "base/test/gtest_util.h"
#include "base/test/scoped_feature_list.h"
#include "base/token.h"
#include "base/uuid.h"
#include "build/build_config.h"
#include "components/saved_tab_groups/internal/saved_tab_group_model_observer.h"
#include "components/saved_tab_groups/internal/saved_tab_group_sync_bridge.h"
#include "components/saved_tab_groups/public/features.h"
#include "components/saved_tab_groups/public/saved_tab_group.h"
#include "components/saved_tab_groups/public/saved_tab_group_tab.h"
#include "components/saved_tab_groups/public/types.h"
#include "components/saved_tab_groups/public/utils.h"
#include "components/saved_tab_groups/test_support/saved_tab_group_test_utils.h"
#include "components/sync/protocol/saved_tab_group_specifics.pb.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_id.h"
#include "google_apis/gaia/gaia_id.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/image/image.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace tab_groups {

namespace {

using testing::IsEmpty;
using testing::Not;
using testing::NotNull;
using testing::Pointee;
using testing::SizeIs;
using testing::UnorderedElementsAre;

MATCHER_P(HasGroupId, guid, "") {
  return arg.saved_guid() == guid;
}

// Serves to test the functions in SavedTabGroupModelObserver.
class SavedTabGroupModelObserverTest : public ::testing::Test,
                                       public SavedTabGroupModelObserver {
 protected:
  SavedTabGroupModelObserverTest() = default;
  ~SavedTabGroupModelObserverTest() override = default;

  void SetUp() override {
    saved_tab_group_model_ = std::make_unique<SavedTabGroupModel>();
    saved_tab_group_model_->AddObserver(this);
  }

  void TearDown() override { saved_tab_group_model_.reset(); }

  void SavedTabGroupAddedLocally(const base::Uuid& guid) override {
    retrieved_group_.emplace_back(*saved_tab_group_model_->Get(guid));
    retrieved_index_ = saved_tab_group_model_->GetIndexOf(guid).value_or(-1);
  }

  void SavedTabGroupRemovedLocally(
      const SavedTabGroup& removed_group) override {
    retrieved_guid_ = removed_group.saved_guid();
  }

  void SavedTabGroupUpdatedLocally(
      const base::Uuid& group_guid,
      const std::optional<base::Uuid>& tab_guid) override {
    retrieved_group_.emplace_back(*saved_tab_group_model_->Get(group_guid));
    retrieved_index_ =
        saved_tab_group_model_->GetIndexOf(group_guid).value_or(-1);
  }

  void SavedTabGroupAddedFromSync(const base::Uuid& guid) override {
    retrieved_group_.emplace_back(*saved_tab_group_model_->Get(guid));
    retrieved_index_ = saved_tab_group_model_->GetIndexOf(guid).value_or(-1);
  }

  void SavedTabGroupRemovedFromSync(
      const SavedTabGroup& removed_group) override {
    retrieved_guid_ = removed_group.saved_guid();
  }

  void SavedTabGroupUpdatedFromSync(
      const base::Uuid& group_guid,
      const std::optional<base::Uuid>& tab_guid) override {
    retrieved_group_.emplace_back(*saved_tab_group_model_->Get(group_guid));
    retrieved_index_ =
        saved_tab_group_model_->GetIndexOf(group_guid).value_or(-1);
  }

  void SavedTabGroupReorderedLocally() override { reordered_called_ = true; }
  void SavedTabGroupTabMovedLocally(const base::Uuid& group_guid,
                                    const base::Uuid& tab_guid) override {
    tabs_reodered_called_ = true;
  }

  void SavedTabGroupTabLastSeenTimeUpdated(const base::Uuid& saved_tab_id,
                                           TriggerSource source) override {
    last_seen_tab_id_ = saved_tab_id;
  }

  void ClearSignals() {
    retrieved_group_.clear();
    retrieved_index_ = -1;
    retrieved_old_index_ = -1;
    retrieved_new_index_ = -1;
    reordered_called_ = false;
    tabs_reodered_called_ = false;
    retrieved_guid_ = base::Uuid::GenerateRandomV4();
    last_seen_tab_id_ = base::Uuid::GenerateRandomV4();
  }

  std::unique_ptr<SavedTabGroupModel> saved_tab_group_model_;
  std::vector<SavedTabGroup> retrieved_group_;
  int retrieved_index_ = -1;
  int retrieved_old_index_ = -1;
  int retrieved_new_index_ = -1;
  bool reordered_called_ = false;
  bool tabs_reodered_called_ = false;

  base::Uuid retrieved_guid_ = base::Uuid::GenerateRandomV4();
  base::Uuid last_seen_tab_id_ = base::Uuid::GenerateRandomV4();
  std::string base_path_ = "file:///c:/tmp/";

  base::test::ScopedFeatureList feature_list_;
};

// Serves to test the functions in SavedTabGroupModel.
class SavedTabGroupModelTest : public ::testing::Test {
 protected:
  SavedTabGroupModelTest()
      : id_1_(base::Uuid::GenerateRandomV4()),
        id_2_(base::Uuid::GenerateRandomV4()),
        id_3_(base::Uuid::GenerateRandomV4()) {}

  ~SavedTabGroupModelTest() override { RemoveTestData(); }

  void SetUp() override {
    saved_tab_group_model_ = std::make_unique<SavedTabGroupModel>();
    AddTestData();
  }

  void TearDown() override {
    RemoveTestData();
    saved_tab_group_model_.reset();
  }

  void AddTestData() {
    const std::u16string title_1 = u"Group One";
    const std::u16string title_2 = u"Another Group";
    const std::u16string title_3 = u"The Three Musketeers";

    const tab_groups::TabGroupColorId& color_1 =
        tab_groups::TabGroupColorId::kGrey;
    const tab_groups::TabGroupColorId& color_2 =
        tab_groups::TabGroupColorId::kRed;
    const tab_groups::TabGroupColorId& color_3 =
        tab_groups::TabGroupColorId::kGreen;

    std::vector<SavedTabGroupTab> group_1_tabs = {test::CreateSavedTabGroupTab(
        "A_Link", u"Only Tab", id_1_, /*position=*/0)};
    std::vector<SavedTabGroupTab> group_2_tabs = {
        test::CreateSavedTabGroupTab("One_Link", u"One Of Two", id_2_,
                                     /*position=*/0),
        test::CreateSavedTabGroupTab("Two_Link", u"Second", id_2_,
                                     /*position=*/1)};
    std::vector<SavedTabGroupTab> group_3_tabs = {
        test::CreateSavedTabGroupTab("Athos", u"All For One", id_3_,
                                     /*position=*/0),
        test::CreateSavedTabGroupTab("Porthos", u"And", id_3_, /*position=*/1),
        test::CreateSavedTabGroupTab("Aramis", u"One For All", id_3_,
                                     /*position=*/2)};

    saved_tab_group_model_->AddedLocally(
        SavedTabGroup(title_1, color_1, group_1_tabs, std::nullopt, id_1_));
    saved_tab_group_model_->AddedLocally(
        SavedTabGroup(title_2, color_2, group_2_tabs, std::nullopt, id_2_));
    saved_tab_group_model_->AddedLocally(
        SavedTabGroup(title_3, color_3, group_3_tabs, std::nullopt, id_3_));
  }

  void RemoveTestData() {
    if (!saved_tab_group_model_) {
      return;
    }
    // Copy ids so we do not remove elements while we are accessing the data.
    std::vector<base::Uuid> saved_tab_group_ids = GetSavedTabGroupIds();
    for (const auto& id : saved_tab_group_ids) {
      saved_tab_group_model_->RemovedLocally(id);
    }
  }

  std::vector<base::Uuid> GetSavedTabGroupIds() {
    std::vector<base::Uuid> saved_tab_group_ids;
    for (const SavedTabGroup& saved_group :
         saved_tab_group_model_->saved_tab_groups()) {
      saved_tab_group_ids.emplace_back(saved_group.saved_guid());
    }
    return saved_tab_group_ids;
  }

  std::unique_ptr<SavedTabGroupModel> saved_tab_group_model_;
  std::string base_path_ = "file:///c:/tmp/";
  base::Uuid id_1_;
  base::Uuid id_2_;
  base::Uuid id_3_;

  base::test::ScopedFeatureList feature_list_;
};

// Tests that SavedTabGroupModel::Count holds 3 elements initially.
TEST_F(SavedTabGroupModelTest, InitialCountThree) {
  EXPECT_EQ(saved_tab_group_model_->Count(), 3);
  EXPECT_EQ(saved_tab_group_model_->saved_tab_groups().size(), 3u);
}

// Tests that SavedTabGroupModel::Contains returns the 3, the number of starting
// ids added to the model.
TEST_F(SavedTabGroupModelTest, InitialGroupsAreSaved) {
  EXPECT_TRUE(saved_tab_group_model_->Contains(id_1_));
  EXPECT_TRUE(saved_tab_group_model_->Contains(id_2_));
  EXPECT_TRUE(saved_tab_group_model_->Contains(id_3_));
  EXPECT_FALSE(
      saved_tab_group_model_->Contains(base::Uuid::GenerateRandomV4()));
}

// Tests that the SavedTabGroupModel::GetIndexOf preserves the order the
// SavedTabGroups were inserted into.
TEST_F(SavedTabGroupModelTest, InitialOrderAdded) {
  EXPECT_EQ(saved_tab_group_model_->GetIndexOf(id_1_), 2);
  EXPECT_EQ(saved_tab_group_model_->GetIndexOf(id_2_), 1);
  EXPECT_EQ(saved_tab_group_model_->GetIndexOf(id_3_), 0);
}

// Tests that the SavedTabGroupModel::IsEmpty has elements and once all elements
// are removed is empty.
TEST_F(SavedTabGroupModelTest, ContainsNoElementsOnRemoval) {
  EXPECT_FALSE(saved_tab_group_model_->IsEmpty());
  RemoveTestData();
  EXPECT_TRUE(saved_tab_group_model_->IsEmpty());
}

// Tests that the SavedTabGroupModel::Remove removes the correct element given
// an id.
TEST_F(SavedTabGroupModelTest, RemovesCorrectElements) {
  saved_tab_group_model_->RemovedLocally(id_3_);
  EXPECT_FALSE(saved_tab_group_model_->Contains(id_3_));
  EXPECT_TRUE(saved_tab_group_model_->Contains(id_2_));
  EXPECT_TRUE(saved_tab_group_model_->Contains(id_1_));
}

// Tests that the SavedTabGroupModel only adds unique TabGroupIds.
TEST_F(SavedTabGroupModelTest, OnlyAddUniqueElements) {
  EXPECT_EQ(saved_tab_group_model_->Count(), 3);
  EXPECT_CHECK_DEATH(AddTestData());
}

// Tests that SavedTabGroupModel::Add adds an extra element into the model and
// keeps the data.
TEST_F(SavedTabGroupModelTest, AddNewElement) {
  base::Uuid id_4 = base::Uuid::GenerateRandomV4();
  const std::u16string title_4 = u"Test Test";
  const tab_groups::TabGroupColorId& color_4 =
      tab_groups::TabGroupColorId::kBlue;

  SavedTabGroupTab tab1 = test::CreateSavedTabGroupTab(
      "4th group", u"First Tab 4th Group", id_4, /*position=*/0);
  SavedTabGroupTab tab2 = test::CreateSavedTabGroupTab(
      "2nd link", u"Second Tab 4th Group", id_4, /*position=*/1);

  std::vector<SavedTabGroupTab> group_4_tabs = {tab1, tab2};
  SavedTabGroup group_4(title_4, color_4, group_4_tabs, std::nullopt, id_4);
  saved_tab_group_model_->AddedLocally(group_4);

  EXPECT_TRUE(saved_tab_group_model_->Contains(id_4));
  EXPECT_EQ(saved_tab_group_model_->GetIndexOf(id_4), true ? 0 : 3);
  EXPECT_EQ(saved_tab_group_model_->Count(), 4);

  const SavedTabGroup* saved_group = saved_tab_group_model_->Get(id_4);
  EXPECT_EQ(saved_group->saved_guid(), id_4);
  EXPECT_EQ(saved_group->title(), title_4);
  EXPECT_EQ(saved_group->color(), color_4);
  test::CompareSavedTabGroupTabs(saved_group->saved_tabs(), group_4_tabs);
}

// Tests that SavedTabGroupModel::Update updates the correct element if the
// title or color are different.
TEST_F(SavedTabGroupModelTest, UpdateElement) {
  const SavedTabGroup* group = saved_tab_group_model_->Get(id_1_);
  const std::u16string original_title = group->title();
  const tab_groups::TabGroupColorId& original_color = group->color();
  saved_tab_group_model_->OnGroupOpenedInTabStrip(
      id_1_, test::GenerateRandomTabGroupID());

  // Should only update the element if title or color are different
  const std::u16string same_title = u"Group One";
  const tab_groups::TabGroupColorId& same_color =
      tab_groups::TabGroupColorId::kGrey;
  const tab_groups::TabGroupVisualData same_visual_data(same_title, same_color,
                                                        /*is_collapsed*/ false);
  saved_tab_group_model_->UpdateVisualDataLocally(
      group->local_group_id().value(), &same_visual_data);
  EXPECT_EQ(group->title(), original_title);
  EXPECT_EQ(group->color(), original_color);

  // Updates both color and title
  const std::u16string new_title = u"New Title";
  const tab_groups::TabGroupColorId& new_color =
      tab_groups::TabGroupColorId::kCyan;
  const tab_groups::TabGroupVisualData new_visual_data(new_title, new_color,
                                                       /*is_collapsed*/ false);
  saved_tab_group_model_->UpdateVisualDataLocally(
      group->local_group_id().value(), &new_visual_data);
  EXPECT_EQ(group->title(), new_title);
  EXPECT_EQ(group->color(), new_color);

  // Update only title
  const std::u16string random_title = u"Random Title";
  const tab_groups::TabGroupVisualData change_title_visual_data(
      random_title, original_color, /*is_collapsed*/ false);
  saved_tab_group_model_->UpdateVisualDataLocally(
      group->local_group_id().value(), &change_title_visual_data);
  EXPECT_EQ(group->title(), random_title);
  EXPECT_EQ(group->color(), original_color);

  // Update only color
  const tab_groups::TabGroupColorId& random_color =
      tab_groups::TabGroupColorId::kGrey;
  const tab_groups::TabGroupVisualData change_color_visual_data(
      original_title, random_color, /*is_collapsed*/ false);
  saved_tab_group_model_->UpdateVisualDataLocally(
      group->local_group_id().value(), &change_color_visual_data);
  EXPECT_EQ(group->title(), original_title);
  EXPECT_EQ(group->color(), random_color);
}

// Tests that the correct tabs are added to the correct position in group 1.
TEST_F(SavedTabGroupModelTest, AddTabToGroup) {
  SavedTabGroupTab tab1 = test::CreateSavedTabGroupTab(
      "4th group", u"First Tab 4th Group", id_1_, /*position=*/0);
  SavedTabGroupTab tab2 = test::CreateSavedTabGroupTab(
      "2nd link", u"Second Tab 4th Group", id_1_, /*position=*/2);

  const SavedTabGroup* group = saved_tab_group_model_->Get(id_1_);
  ASSERT_EQ(group->saved_tabs().size(), size_t(1));

  saved_tab_group_model_->AddTabToGroupLocally(group->saved_guid(), tab1);
  EXPECT_EQ(group->saved_tabs().size(), size_t(2));
  EXPECT_EQ(0, group->GetIndexOfTab(tab1.saved_tab_guid()));
  EXPECT_TRUE(group->ContainsTab(tab1.saved_tab_guid()));
  ASSERT_TRUE(group->GetTab(tab1.saved_tab_guid()));
  test::CompareSavedTabGroupTabs({*group->GetTab(tab1.saved_tab_guid())},
                                 {tab1});

  saved_tab_group_model_->AddTabToGroupLocally(group->saved_guid(), tab2);
  EXPECT_EQ(group->saved_tabs().size(), size_t(3));
  EXPECT_EQ(2, group->GetIndexOfTab(tab2.saved_tab_guid()));
  EXPECT_TRUE(group->ContainsTab(tab2.saved_tab_guid()));
  ASSERT_TRUE(group->GetTab(tab2.saved_tab_guid()));
  test::CompareSavedTabGroupTabs({*group->GetTab(tab2.saved_tab_guid())},
                                 {tab2});
  test::CompareSavedTabGroupTabs(group->saved_tabs(),
                                 {tab1, group->saved_tabs()[1], tab2});
}

// Tests that the correct tabs are removed from the correct position in group 1.
TEST_F(SavedTabGroupModelTest, RemoveTabFromGroup) {
  SavedTabGroupTab tab1 = test::CreateSavedTabGroupTab(
      "4th group", u"First Tab 4th Group", id_1_, /*position=*/0);
  SavedTabGroupTab tab2 = test::CreateSavedTabGroupTab(
      "2nd link", u"Second Tab 4th Group", id_1_, /*position=*/2);

  const SavedTabGroup* group = saved_tab_group_model_->Get(id_1_);
  ASSERT_EQ(group->saved_tabs().size(), size_t(1));

  saved_tab_group_model_->AddTabToGroupLocally(group->saved_guid(), tab1);
  saved_tab_group_model_->AddTabToGroupLocally(group->saved_guid(), tab2);
  EXPECT_EQ(group->saved_tabs().size(), size_t(3));

  saved_tab_group_model_->RemoveTabFromGroupLocally(group->saved_guid(),
                                                    tab1.saved_tab_guid());
  EXPECT_EQ(group->saved_tabs().size(), size_t(2));
  test::CompareSavedTabGroupTabs(group->saved_tabs(),
                                 {group->saved_tabs()[0], tab2});

  saved_tab_group_model_->RemoveTabFromGroupLocally(group->saved_guid(),
                                                    tab2.saved_tab_guid());
  EXPECT_EQ(group->saved_tabs().size(), size_t(1));
  test::CompareSavedTabGroupTabs(group->saved_tabs(), {group->saved_tabs()[0]});
}

TEST_F(SavedTabGroupModelTest, RemoveSharedTabFromGroup) {
  SavedTabGroup shared_group =
      saved_tab_group_model_->Get(id_2_)->CloneAsSharedTabGroup(
          CollaborationId("collaboration"));
  ASSERT_THAT(shared_group.saved_tabs(), SizeIs(2));
  ASSERT_THAT(shared_group.last_removed_tabs_metadata(), IsEmpty());
  saved_tab_group_model_->AddedLocally(shared_group);

  // Remove one shared tab and verify that its metadata is stored in the group.
  GaiaId removed_by("user_id");
  base::Uuid tab_guid_to_remove =
      shared_group.saved_tabs().back().saved_tab_guid();
  saved_tab_group_model_->RemoveTabFromGroupFromSync(
      shared_group.saved_guid(), tab_guid_to_remove, removed_by);

  const std::map<base::Uuid, SavedTabGroup::RemovedTabMetadata>&
      removed_tabs_metadata =
          saved_tab_group_model_->Get(shared_group.saved_guid())
              ->last_removed_tabs_metadata();
  ASSERT_THAT(removed_tabs_metadata,
              UnorderedElementsAre(testing::Key(tab_guid_to_remove)));
  EXPECT_EQ(removed_tabs_metadata.at(tab_guid_to_remove).removed_by,
            removed_by);
}

// Tests that a group is removed from the model when the last tab is removed
// from it.
TEST_F(SavedTabGroupModelTest, RemoveLastTabFromGroupLocally) {
  const SavedTabGroup* group = saved_tab_group_model_->Get(id_1_);
  ASSERT_EQ(1u, group->saved_tabs().size());

  saved_tab_group_model_->RemoveTabFromGroupLocally(
      group->saved_guid(), group->saved_tabs()[0].saved_tab_guid());

  EXPECT_FALSE(saved_tab_group_model_->Contains(id_1_));
}

// Tests that last tab deletion from sync creates a pending NTP.
TEST_F(SavedTabGroupModelTest, PendingNTP_CreatedWhenLastTabRemovedFromSync) {
  const SavedTabGroup* group = saved_tab_group_model_->Get(id_1_);
  ASSERT_EQ(1u, group->saved_tabs().size());

  saved_tab_group_model_->RemoveTabFromGroupFromSync(
      group->saved_guid(), group->saved_tabs()[0].saved_tab_guid());

  // Verify that the group isn't deleted and has a pending NTP.
  group = saved_tab_group_model_->Get(id_1_);
  EXPECT_TRUE(group);
  EXPECT_EQ(1u, group->saved_tabs().size());
  EXPECT_TRUE(group->saved_tabs()[0].is_pending_ntp());
}

// Tests that pending NTP commits when it navigates locally.
TEST_F(SavedTabGroupModelTest,
       PendingNTP_CommitsWhenPendingTabNavigatesLocally) {
  const SavedTabGroup* group = saved_tab_group_model_->Get(id_1_);
  ASSERT_EQ(group->saved_tabs().size(), size_t(1));

  saved_tab_group_model_->RemoveTabFromGroupFromSync(
      group->saved_guid(), group->saved_tabs()[0].saved_tab_guid());

  // Verify that the group isn't deleted and has a pending NTP.
  group = saved_tab_group_model_->Get(id_1_);
  ASSERT_TRUE(group);
  EXPECT_EQ(1u, group->saved_tabs().size());
  EXPECT_TRUE(group->saved_tabs()[0].is_pending_ntp());
}

// Tests that pending NTP commits when another tab is added locally.
TEST_F(SavedTabGroupModelTest, PendingNTP_CommitsWhenAnotherTabAddedLocally) {
  const SavedTabGroup* group = saved_tab_group_model_->Get(id_1_);
  ASSERT_EQ(group->saved_tabs().size(), size_t(1));

  saved_tab_group_model_->RemoveTabFromGroupFromSync(
      group->saved_guid(), group->saved_tabs()[0].saved_tab_guid());

  // Verify that the group isn't deleted and has a pending NTP.
  group = saved_tab_group_model_->Get(id_1_);
  ASSERT_TRUE(group);
  EXPECT_EQ(1u, group->saved_tabs().size());
  EXPECT_TRUE(group->saved_tabs()[0].is_pending_ntp());

  // Add a tab locally.
  SavedTabGroupTab tab2 = test::CreateSavedTabGroupTab(
      "https://xyz", u"First Tab 4th Group", id_1_, /*position=*/0);
  saved_tab_group_model_->AddTabToGroupLocally(group->saved_guid(), tab2);

  // Both tabs should be non-pending.
  group = saved_tab_group_model_->Get(id_1_);
  ASSERT_TRUE(group);
  EXPECT_EQ(2u, group->saved_tabs().size());
  EXPECT_FALSE(group->saved_tabs()[0].is_pending_ntp());
  EXPECT_FALSE(group->saved_tabs()[1].is_pending_ntp());
}

// Tests that pending NTP commits when tabs are received from sync.
TEST_F(SavedTabGroupModelTest,
       PendingNTP_CommitsWhenIncomingNavigationFromSync) {
  const SavedTabGroup* group = saved_tab_group_model_->Get(id_1_);
  ASSERT_EQ(1u, group->saved_tabs().size());

  saved_tab_group_model_->RemoveTabFromGroupFromSync(
      group->saved_guid(), group->saved_tabs()[0].saved_tab_guid());

  // Verify that the group isn't deleted and has a pending NTP.
  group = saved_tab_group_model_->Get(id_1_);
  ASSERT_TRUE(group);
  EXPECT_EQ(1u, group->saved_tabs().size());
  EXPECT_TRUE(group->saved_tabs()[0].is_pending_ntp());

  SavedTabGroupTab pending_ntp = group->saved_tabs()[0];
  LocalTabID local_tab_id_1 = test::GenerateRandomTabID();
  saved_tab_group_model_->UpdateLocalTabId(id_1_, pending_ntp, local_tab_id_1);

  // Add a tab locally.
  SavedTabGroupTab tab2 = test::CreateSavedTabGroupTab(
      "https://xyz", u"First Tab 4th Group", id_1_, /*position=*/0);
  saved_tab_group_model_->AddTabToGroupFromSync(id_1_, tab2);

  // The incoming tab should replace the pending NTP and have the local tab ID
  // copied over from the pending NTP.
  group = saved_tab_group_model_->Get(id_1_);
  ASSERT_EQ(1u, group->saved_tabs().size());
  EXPECT_FALSE(group->saved_tabs()[0].is_pending_ntp());
  EXPECT_EQ(local_tab_id_1, group->saved_tabs()[0].local_tab_id());
  EXPECT_EQ(tab2.saved_tab_guid(), group->saved_tabs()[0].saved_tab_guid());

  tab2.SetLocalTabID(local_tab_id_1);
  test::CompareSavedTabGroupTabs(group->saved_tabs(), {tab2});
}

// Tests updating a tab in a saved group.
TEST_F(SavedTabGroupModelTest, UpdateTabInGroup) {
  const SavedTabGroup* group = saved_tab_group_model_->Get(id_1_);
  ASSERT_EQ(group->saved_tabs().size(), size_t(1));

  // Update the tab by changing the title.
  SavedTabGroupTab tab1 = group->saved_tabs()[0];
  tab1.SetTitle(u"Updated Title");
  saved_tab_group_model_->UpdateTabInGroup(id_1_, tab1,
                                           /*notify_observers=*/false);

  // The group should contain the updated tab.
  test::CompareSavedTabGroupTabs(group->saved_tabs(), {tab1});
}

// Tests that the correct tabs are moved in group 1.
TEST_F(SavedTabGroupModelTest, MoveTabInGroup) {
  SavedTabGroupTab tab1 = test::CreateSavedTabGroupTab(
      "4th group", u"First Tab 4th Group", id_1_, /*position=*/0);
  SavedTabGroupTab tab2 = test::CreateSavedTabGroupTab(
      "2nd link", u"Second Tab 4th Group", id_1_, /*position=*/2);

  const SavedTabGroup* group = saved_tab_group_model_->Get(id_1_);
  ASSERT_EQ(group->saved_tabs().size(), size_t(1));

  saved_tab_group_model_->AddTabToGroupLocally(group->saved_guid(), tab1);
  saved_tab_group_model_->AddTabToGroupLocally(group->saved_guid(), tab2);
  EXPECT_EQ(group->saved_tabs().size(), size_t(3));

  saved_tab_group_model_->MoveTabInGroupTo(group->saved_guid(),
                                           tab1.saved_tab_guid(), 2);
  test::CompareSavedTabGroupTabs(group->saved_tabs(),
                                 {group->saved_tabs()[0], tab2, tab1});

  saved_tab_group_model_->MoveTabInGroupTo(group->saved_guid(),
                                           tab1.saved_tab_guid(), 1);
  test::CompareSavedTabGroupTabs(group->saved_tabs(),
                                 {group->saved_tabs()[0], tab1, tab2});
}

TEST_F(SavedTabGroupModelTest, MoveElement) {
  ASSERT_EQ(0, saved_tab_group_model_->GetIndexOf(id_3_));
  ASSERT_EQ(1, saved_tab_group_model_->GetIndexOf(id_2_));
  ASSERT_EQ(2, saved_tab_group_model_->GetIndexOf(id_1_));
  saved_tab_group_model_->ReorderGroupLocally(id_2_, 2);
  EXPECT_EQ(0, saved_tab_group_model_->GetIndexOf(id_3_));
  EXPECT_EQ(1, saved_tab_group_model_->GetIndexOf(id_1_));
  EXPECT_EQ(2, saved_tab_group_model_->GetIndexOf(id_2_));
  saved_tab_group_model_->ReorderGroupLocally(id_2_, 0);
  EXPECT_EQ(0, saved_tab_group_model_->GetIndexOf(id_2_));
  EXPECT_EQ(1, saved_tab_group_model_->GetIndexOf(id_3_));
  EXPECT_EQ(2, saved_tab_group_model_->GetIndexOf(id_1_));
  saved_tab_group_model_->ReorderGroupLocally(id_2_, 1);
  EXPECT_EQ(0, saved_tab_group_model_->GetIndexOf(id_3_));
  EXPECT_EQ(1, saved_tab_group_model_->GetIndexOf(id_2_));
  EXPECT_EQ(2, saved_tab_group_model_->GetIndexOf(id_1_));
}

TEST_F(SavedTabGroupModelTest, ShouldDistinguishSavedAndSharedGroups) {
  SavedTabGroup shared_group =
      saved_tab_group_model_->Get(id_1_)->CloneAsSharedTabGroup(
          CollaborationId("collaboration"));
  saved_tab_group_model_->AddedLocally(shared_group);

  ASSERT_TRUE(shared_group.is_shared_tab_group());

  ASSERT_FALSE(saved_tab_group_model_->Get(id_1_)->is_shared_tab_group());
  ASSERT_FALSE(saved_tab_group_model_->Get(id_2_)->is_shared_tab_group());
  ASSERT_FALSE(saved_tab_group_model_->Get(id_3_)->is_shared_tab_group());

  EXPECT_THAT(saved_tab_group_model_->GetSavedTabGroupsOnly(),
              UnorderedElementsAre(Pointee(HasGroupId(id_1_)),
                                   Pointee(HasGroupId(id_2_)),
                                   Pointee(HasGroupId(id_3_))));
  EXPECT_THAT(
      saved_tab_group_model_->GetSharedTabGroupsOnly(),
      UnorderedElementsAre(Pointee(HasGroupId(shared_group.saved_guid()))));
}

TEST_F(SavedTabGroupModelTest, LoadStoredEntriesPopulatesModel) {
  std::unique_ptr<SavedTabGroup> group =
      std::make_unique<SavedTabGroup>(*saved_tab_group_model_->Get(id_3_));

  saved_tab_group_model_->RemovedLocally(id_3_);
  ASSERT_FALSE(saved_tab_group_model_->Contains(id_3_));

  saved_tab_group_model_->LoadStoredEntries({*group}, group->saved_tabs());

  EXPECT_TRUE(saved_tab_group_model_->Contains(id_3_));
  EXPECT_EQ(saved_tab_group_model_->GetIndexOf(id_3_), true ? 0 : 2);
  EXPECT_EQ(saved_tab_group_model_->Count(), 3);

  const SavedTabGroup* saved_group = saved_tab_group_model_->Get(id_3_);
  EXPECT_EQ(saved_group->saved_guid(), id_3_);
  EXPECT_EQ(saved_group->title(), group->title());
  EXPECT_EQ(saved_group->color(), group->color());

  // We can not use test::CompareSavedTabGroupTabs because the favicons are not
  // loaded until the tab is opened through the saved group button.
  EXPECT_EQ(saved_group->saved_tabs().size(), group->saved_tabs().size());
}

// Tests that merging a group with the same group_id changes the state of the
// object correctly.
TEST_F(SavedTabGroupModelTest, MergeGroupsFromModel) {
  const SavedTabGroup* group1 = saved_tab_group_model_->Get(id_1_);

  SavedTabGroup group2(*group1);
  group2.SetColor(tab_groups::TabGroupColorId::kPink);
  group2.SetTitle(u"Updated title");
  const SavedTabGroup* merged_group =
      saved_tab_group_model_->MergeRemoteGroupMetadata(
          group2.saved_guid(), group2.title(), group2.color(),
          group2.position(), group2.creator_cache_guid(),
          group2.last_updater_cache_guid(), group2.update_time(),
          /*updated_by=*/GaiaId());

  EXPECT_EQ(group2.title(), merged_group->title());
  EXPECT_EQ(group2.color(), merged_group->color());
  EXPECT_EQ(group2.saved_guid(), merged_group->saved_guid());
  EXPECT_EQ(group2.creation_time(), merged_group->creation_time());
  EXPECT_EQ(group2.update_time(), merged_group->update_time());
}

TEST_F(SavedTabGroupModelTest, MergePinnedGroupRetainPosition) {
  auto guid1 = base::Uuid::GenerateRandomV4();
  auto guid2 = base::Uuid::GenerateRandomV4();

  // Add group 1 at position 0.
  saved_tab_group_model_->AddedLocally(SavedTabGroup(
      u"Title 1", tab_groups::TabGroupColorId::kPink, {}, 0, guid1));

  // Add group 2 at position 0.
  saved_tab_group_model_->AddedLocally(SavedTabGroup(
      u"Title", tab_groups::TabGroupColorId::kPink, {}, 0, guid2));
  const SavedTabGroup* group2 = saved_tab_group_model_->Get(guid2);
  EXPECT_EQ(0, group2->position());

  // Verify group 2 should be the 1st one in the list.
  ASSERT_THAT(GetSavedTabGroupIds(),
              testing::ElementsAre(guid2, guid1, id_3_, id_2_, id_1_));

  // Change group 2 position from 0 to 1.
  SavedTabGroup updated_group2(*group2);
  EXPECT_EQ(0, updated_group2.position());
  updated_group2.SetPosition(1);
  EXPECT_EQ(1, updated_group2.position());

  // Merge the updated group 2 and verify the position is set to 1.
  const SavedTabGroup* merged_group =
      saved_tab_group_model_->MergeRemoteGroupMetadata(
          updated_group2.saved_guid(), updated_group2.title(),
          updated_group2.color(), updated_group2.position(),
          updated_group2.creator_cache_guid(),
          updated_group2.last_updater_cache_guid(),
          updated_group2.update_time(),
          /*updated_by=*/GaiaId());
  EXPECT_EQ(1, merged_group->position());

  // Verify group 2 should be the 2nd one in the list.
  ASSERT_THAT(GetSavedTabGroupIds(),
              testing::ElementsAre(guid1, guid2, id_3_, id_2_, id_1_));
}

TEST_F(SavedTabGroupModelTest, MergeSharedTabGroupAttribution) {
  const GaiaId kCreator("123");
  const GaiaId kUpdater("456");

  SavedTabGroup group(u"Title", tab_groups::TabGroupColorId::kPink, /*urls=*/{},
                      /*position=*/std::nullopt);
  group.SetCollaborationId(CollaborationId("collaboration"));
  group.SetUpdatedByAttribution(kCreator);
  saved_tab_group_model_->AddedLocally(group);

  const SavedTabGroup* model_group =
      saved_tab_group_model_->Get(group.saved_guid());
  ASSERT_THAT(model_group, NotNull());
  ASSERT_EQ(model_group->shared_attribution().created_by, kCreator);
  ASSERT_EQ(model_group->shared_attribution().updated_by, kCreator);

  // Update only the updated by attribution.
  saved_tab_group_model_->MergeRemoteGroupMetadata(
      group.saved_guid(), group.title(), group.color(), group.position(),
      group.creator_cache_guid(), group.last_updater_cache_guid(),
      group.update_time(), kUpdater);

  EXPECT_EQ(model_group->shared_attribution().created_by, kCreator);
  EXPECT_EQ(model_group->shared_attribution().updated_by, kUpdater);
}

// Tests that merging a tab with the same tab_id changes the state of the object
// correctly.
TEST_F(SavedTabGroupModelTest, MergeTabsFromModel) {
  SavedTabGroupTab tab1 = saved_tab_group_model_->Get(id_1_)->saved_tabs()[0];
  SavedTabGroupTab tab2(tab1);
  tab2.SetTitle(u"Updated Title");
  tab2.SetURL(GURL("http://foo.com"));

  const SavedTabGroupTab* merged_tab =
      saved_tab_group_model_->MergeRemoteTab(tab2);

  EXPECT_EQ(tab2.url(), merged_tab->url());
  EXPECT_EQ(tab2.saved_tab_guid(), merged_tab->saved_tab_guid());
  EXPECT_EQ(tab2.saved_group_guid(), merged_tab->saved_group_guid());
  EXPECT_EQ(tab2.creation_time(), merged_tab->creation_time());
  EXPECT_EQ(tab2.update_time(), merged_tab->update_time());
}

TEST_F(SavedTabGroupModelTest, MergeTabsWithUnsupportedURLFromModel) {
  SavedTabGroupTab tab1 = saved_tab_group_model_->Get(id_1_)->saved_tabs()[0];
  SavedTabGroupTab remote_tab(tab1);
  remote_tab.SetTitle(u"Updated Title");
  remote_tab.SetURL(GURL(kChromeSavedTabGroupUnsupportedURL));

  const SavedTabGroupTab* merged_tab =
      saved_tab_group_model_->MergeRemoteTab(remote_tab);

  EXPECT_EQ(tab1.url(), merged_tab->url());
  EXPECT_EQ(remote_tab.saved_tab_guid(), merged_tab->saved_tab_guid());
  EXPECT_EQ(remote_tab.saved_group_guid(), merged_tab->saved_group_guid());
  EXPECT_EQ(remote_tab.creation_time(), merged_tab->creation_time());
  EXPECT_EQ(remote_tab.update_time(), merged_tab->update_time());
}

// Tests that groups inserted in the model are in order stay inserted in sorted
// order.
TEST_F(SavedTabGroupModelTest, GroupsSortedWithInOrderPositions) {
  RemoveTestData();

  // Create an arbitrary number of groups, with the positions the groups should
  // sit in the bookmarks bar.
  SavedTabGroup group_1(u"Group 1", tab_groups::TabGroupColorId::kRed, {}, 0);
  SavedTabGroup group_2(u"Group 2", tab_groups::TabGroupColorId::kOrange, {},
                        1);
  SavedTabGroup group_3(u"Group 3", tab_groups::TabGroupColorId::kYellow, {},
                        2);
  SavedTabGroup group_4(u"Group 4", tab_groups::TabGroupColorId::kGreen, {}, 3);
  SavedTabGroup group_5(u"Group 5", tab_groups::TabGroupColorId::kBlue, {}, 4);
  SavedTabGroup group_6(u"Group 6", tab_groups::TabGroupColorId::kPurple, {},
                        5);

  // This is the order we expect the groups in the model to be.
  std::vector<SavedTabGroup> groups = {group_1, group_2, group_3,
                                       group_4, group_5, group_6};

  // Add the groups into the model in order.
  saved_tab_group_model_->AddedLocally(group_1);
  saved_tab_group_model_->AddedLocally(group_2);
  saved_tab_group_model_->AddedLocally(group_3);
  saved_tab_group_model_->AddedLocally(group_4);
  saved_tab_group_model_->AddedLocally(group_5);
  saved_tab_group_model_->AddedLocally(group_6);

  EXPECT_EQ(saved_tab_group_model_->saved_tab_groups().size(), groups.size());
  for (size_t i = 0; i < groups.size(); ++i) {
    EXPECT_TRUE(test::CompareSavedTabGroups(
        groups[i], saved_tab_group_model_->saved_tab_groups()[i]));
  }
}

// Tests that groups inserted in the model out of order are still inserted in
// sorted order.
TEST_F(SavedTabGroupModelTest, GroupsSortedWithOutOfOrderPositions) {
  RemoveTestData();

  // Create an arbitrary number of groups, with the positions the groups should
  // sit in the bookmarks bar.
  SavedTabGroup group_1(u"Group 1", tab_groups::TabGroupColorId::kRed, {}, 0);
  SavedTabGroup group_2(u"Group 2", tab_groups::TabGroupColorId::kOrange, {},
                        1);
  SavedTabGroup group_3(u"Group 3", tab_groups::TabGroupColorId::kYellow, {},
                        2);
  SavedTabGroup group_4(u"Group 4", tab_groups::TabGroupColorId::kGreen, {}, 3);
  SavedTabGroup group_5(u"Group 5", tab_groups::TabGroupColorId::kBlue, {}, 4);
  SavedTabGroup group_6(u"Group 6", tab_groups::TabGroupColorId::kPurple, {},
                        5);

  // This is the order we expect the groups in the model to be.
  std::vector<SavedTabGroup> groups = {group_1, group_2, group_3,
                                       group_4, group_5, group_6};

  // Add the groups into the model in an arbitrary order.
  saved_tab_group_model_->AddedLocally(group_6);
  saved_tab_group_model_->AddedLocally(group_1);
  saved_tab_group_model_->AddedLocally(group_4);
  saved_tab_group_model_->AddedLocally(group_3);
  saved_tab_group_model_->AddedLocally(group_5);
  saved_tab_group_model_->AddedLocally(group_2);

  EXPECT_EQ(saved_tab_group_model_->saved_tab_groups().size(), groups.size());
  for (size_t i = 0; i < groups.size(); ++i) {
    EXPECT_TRUE(test::CompareSavedTabGroups(
        groups[i], saved_tab_group_model_->saved_tab_groups()[i]));
  }
}

// Tests that groups inserted in the model with gaps between the positions are
// still inserted in sorted order.
TEST_F(SavedTabGroupModelTest, GroupsSortedWithGapsInPositions) {
  RemoveTestData();

  // Create an arbitrary number of groups, with the positions the groups should
  // sit in the bookmarks bar.
  SavedTabGroup group_1(u"Group 1", tab_groups::TabGroupColorId::kRed, {}, 0);
  SavedTabGroup group_2(u"Group 2", tab_groups::TabGroupColorId::kOrange, {},
                        3);
  SavedTabGroup group_3(u"Group 3", tab_groups::TabGroupColorId::kYellow, {},
                        8);
  SavedTabGroup group_4(u"Group 4", tab_groups::TabGroupColorId::kGreen, {},
                        19);
  SavedTabGroup group_5(u"Group 5", tab_groups::TabGroupColorId::kBlue, {}, 21);
  SavedTabGroup group_6(u"Group 6", tab_groups::TabGroupColorId::kPurple, {},
                        34);

  // This is the order we expect the groups in the model to be.
  std::vector<SavedTabGroup> groups = {group_1, group_2, group_3,
                                       group_4, group_5, group_6};

  // Add the groups into the model in an arbitrary order.
  saved_tab_group_model_->AddedLocally(group_6);
  saved_tab_group_model_->AddedLocally(group_1);
  saved_tab_group_model_->AddedLocally(group_4);
  saved_tab_group_model_->AddedLocally(group_3);
  saved_tab_group_model_->AddedLocally(group_5);
  saved_tab_group_model_->AddedLocally(group_2);

  EXPECT_EQ(saved_tab_group_model_->saved_tab_groups().size(), groups.size());
  for (size_t i = 0; i < groups.size(); ++i) {
    EXPECT_TRUE(test::CompareSavedTabGroups(
        groups[i], saved_tab_group_model_->saved_tab_groups()[i]));
  }
}

// Tests that groups inserted in the model with gaps and in decreasing order
// between the positions are still inserted in increasing sorted order.
TEST_F(SavedTabGroupModelTest, GroupsSortedWithDecreasingPositions) {
  RemoveTestData();

  // Create an arbitrary number of groups, with the positions the groups should
  // sit in the bookmarks bar.
  SavedTabGroup group_1(u"Group 1", tab_groups::TabGroupColorId::kRed, {}, 0);
  SavedTabGroup group_2(u"Group 2", tab_groups::TabGroupColorId::kOrange, {},
                        3);
  SavedTabGroup group_3(u"Group 3", tab_groups::TabGroupColorId::kYellow, {},
                        8);
  SavedTabGroup group_4(u"Group 4", tab_groups::TabGroupColorId::kGreen, {},
                        19);
  SavedTabGroup group_5(u"Group 5", tab_groups::TabGroupColorId::kBlue, {}, 21);
  SavedTabGroup group_6(u"Group 6", tab_groups::TabGroupColorId::kPurple, {},
                        34);

  // This is the order we expect the groups in the model to be.
  std::vector<SavedTabGroup> groups = {group_1, group_2, group_3,
                                       group_4, group_5, group_6};

  // Add the groups into the model in an arbitrary order.
  saved_tab_group_model_->AddedLocally(group_6);
  saved_tab_group_model_->AddedLocally(group_5);
  saved_tab_group_model_->AddedLocally(group_4);
  saved_tab_group_model_->AddedLocally(group_3);
  saved_tab_group_model_->AddedLocally(group_2);
  saved_tab_group_model_->AddedLocally(group_1);

  EXPECT_EQ(saved_tab_group_model_->saved_tab_groups().size(), groups.size());
  for (size_t i = 0; i < groups.size(); ++i) {
    EXPECT_TRUE(test::CompareSavedTabGroups(
        groups[i], saved_tab_group_model_->saved_tab_groups()[i]));
  }
}

// Tests that groups inserted in the model with a more recent update time take
// precedence over groups with the same position.
TEST_F(SavedTabGroupModelTest, GroupWithSamePositionSortedByUpdateTime) {
  RemoveTestData();

  // Create an arbitrary number of groups, with the positions the groups should
  // sit in the bookmarks bar.
  SavedTabGroup group_1(u"Group 1", tab_groups::TabGroupColorId::kRed, {}, 0);
  SavedTabGroup group_2(u"Group 2", tab_groups::TabGroupColorId::kOrange, {},
                        0);

  // This is the order we expect the groups in the model to be.
  std::vector<SavedTabGroup> groups = {group_2, group_1};

  // Add the groups into the model in an arbitrary order.
  saved_tab_group_model_->AddedLocally(group_1);
  saved_tab_group_model_->AddedLocally(group_2);

  EXPECT_EQ(saved_tab_group_model_->saved_tab_groups().size(), groups.size());
  for (size_t i = 0; i < groups.size(); ++i) {
    EXPECT_TRUE(test::CompareSavedTabGroups(
        groups[i], saved_tab_group_model_->saved_tab_groups()[i]));
  }
}

// Tests that groups inserted in the model with no position are inserted at the
// back of the model and have their position set to the last index at the time
// they were inserted.
TEST_F(SavedTabGroupModelTest, GroupsWithNoPositionInsertedAtEnd) {
  RemoveTestData();

  // Create an arbitrary number of groups, with the positions the groups should
  // sit in the bookmarks bar.
  SavedTabGroup group_1(u"Group 1", tab_groups::TabGroupColorId::kRed, {}, 0);
  SavedTabGroup group_2(u"Group 2", tab_groups::TabGroupColorId::kOrange, {},
                        1);
  SavedTabGroup group_3(u"Group 3", tab_groups::TabGroupColorId::kYellow, {},
                        2);
  SavedTabGroup group_4(u"Group 4", tab_groups::TabGroupColorId::kGreen, {}, 3);
  SavedTabGroup group_5(u"Group 5", tab_groups::TabGroupColorId::kBlue, {}, 4);
  SavedTabGroup group_6(u"Group 6", tab_groups::TabGroupColorId::kPurple, {},
                        std::nullopt);

  // This is the order we expect the groups in the model to be.
  std::vector<SavedTabGroup> groups = {group_1, group_2, group_3,
                                       group_4, group_5, group_6};

  // Add the groups into the model in an arbitrary order.
  saved_tab_group_model_->AddedLocally(group_1);
  saved_tab_group_model_->AddedLocally(group_2);
  saved_tab_group_model_->AddedLocally(group_3);
  saved_tab_group_model_->AddedLocally(group_4);
  saved_tab_group_model_->AddedLocally(group_5);
  saved_tab_group_model_->AddedLocally(group_6);

  EXPECT_EQ(saved_tab_group_model_->saved_tab_groups().size(), groups.size());

  // Expect the 6th group to have a position of 5 (0-based indexing).
  EXPECT_EQ(saved_tab_group_model_
                ->saved_tab_groups()
                    [saved_tab_group_model_->saved_tab_groups().size() - 1]
                .position(),
            groups[5].position());

  for (size_t i = 0; i < groups.size(); ++i) {
    EXPECT_TRUE(test::CompareSavedTabGroups(
        groups[i], saved_tab_group_model_->saved_tab_groups()[i]));
  }
}

TEST_F(SavedTabGroupModelTest, SetsLastSeenTime) {
  SavedTabGroup saved_group = test::CreateTestSavedTabGroup();
  saved_group.SetCollaborationId(tab_groups::CollaborationId("collab_id"));
  saved_tab_group_model_->AddedLocally(saved_group);
  const base::Uuid group_id = saved_group.saved_guid();

  EXPECT_FALSE(saved_tab_group_model_->Get(group_id)
                   ->saved_tabs()
                   .front()
                   .last_seen_time()
                   .has_value());

  base::Time last_seen_time = base::Time::Now();
  saved_tab_group_model_->UpdateTabLastSeenTime(
      saved_group.saved_guid(),
      saved_group.saved_tabs().front().saved_tab_guid(), last_seen_time,
      TriggerSource::LOCAL);

  EXPECT_TRUE(saved_tab_group_model_->Get(group_id)
                  ->saved_tabs()
                  .front()
                  .last_seen_time()
                  .has_value());
  EXPECT_EQ(last_seen_time, saved_tab_group_model_->Get(group_id)
                                ->saved_tabs()
                                .front()
                                .last_seen_time());

  // Update the last seen time again. But since it's already greater than
  // navigaion time, the redundant update will be ignored.
  base::Time last_seen_time2 = base::Time::Now() + base::Seconds(10);
  saved_tab_group_model_->UpdateTabLastSeenTime(
      saved_group.saved_guid(),
      saved_group.saved_tabs().front().saved_tab_guid(), last_seen_time2,
      TriggerSource::LOCAL);
  EXPECT_EQ(last_seen_time, saved_tab_group_model_->Get(group_id)
                                ->saved_tabs()
                                .front()
                                .last_seen_time());
  SavedTabGroupTab tab =
      saved_tab_group_model_->Get(group_id)->saved_tabs().front();

  // Update navigation time and try updating last seen time again. It should
  // succeed.
  tab.SetNavigationTime(base::Time::Now() + base::Seconds(5));
  saved_tab_group_model_->UpdateTabInGroup(group_id, tab,
                                           /*notify_observers=*/true);
  saved_tab_group_model_->UpdateTabLastSeenTime(
      saved_group.saved_guid(),
      saved_group.saved_tabs().front().saved_tab_guid(), last_seen_time2,
      TriggerSource::LOCAL);
  EXPECT_EQ(last_seen_time2, saved_tab_group_model_->Get(group_id)
                                 ->saved_tabs()
                                 .front()
                                 .last_seen_time());
}

// Tests that SavedTabGroupModelObserver::Added passes the correct element from
// the model.
TEST_F(SavedTabGroupModelObserverTest, AddElement) {
  SavedTabGroup group_4(test::CreateTestSavedTabGroup());
  saved_tab_group_model_->AddedLocally(group_4);

  const int index = retrieved_group_.size() - 1;
  ASSERT_GE(index, 0);

  SavedTabGroup received_group = retrieved_group_[index];
  EXPECT_EQ(group_4.local_group_id(), received_group.local_group_id());
  EXPECT_EQ(group_4.title(), received_group.title());
  EXPECT_EQ(group_4.color(), received_group.color());
  test::CompareSavedTabGroupTabs(group_4.saved_tabs(),
                                 received_group.saved_tabs());
  EXPECT_EQ(saved_tab_group_model_->GetIndexOf(received_group.saved_guid()),
            retrieved_index_);
}

// Tests that SavedTabGroupModelObserver::Removed passes the correct
// element from the model.
TEST_F(SavedTabGroupModelObserverTest, RemovedElement) {
  SavedTabGroup group_4(test::CreateTestSavedTabGroup());
  saved_tab_group_model_->AddedLocally(group_4);
  saved_tab_group_model_->RemovedLocally(group_4.saved_guid());

  EXPECT_EQ(group_4.saved_guid(), retrieved_guid_);
  EXPECT_FALSE(saved_tab_group_model_->Contains(retrieved_guid_));

  // The model will have already removed and sent the index our element was at
  // before it was removed from the model. As such, we should get -1 when
  // checking the model and 0 for the retrieved index.
  EXPECT_EQ(saved_tab_group_model_->GetIndexOf(retrieved_guid_), std::nullopt);
}

// Tests that SavedTabGroupModelObserver::Updated passes the correct
// element from the model.
TEST_F(SavedTabGroupModelObserverTest, UpdatedElement) {
  SavedTabGroup group_4(test::CreateTestSavedTabGroup());
  group_4.SetLocalGroupId(test::GenerateRandomTabGroupID());
  saved_tab_group_model_->AddedLocally(group_4);

  const std::u16string new_title = u"New Title";
  const tab_groups::TabGroupColorId& new_color =
      tab_groups::TabGroupColorId::kBlue;

  const tab_groups::TabGroupVisualData new_visual_data(new_title, new_color,
                                                       /*is_collapsed*/ false);
  saved_tab_group_model_->UpdateVisualDataLocally(
      group_4.local_group_id().value(), &new_visual_data);

  const int index = retrieved_group_.size() - 1;
  ASSERT_GE(index, 0);

  SavedTabGroup received_group = retrieved_group_[index];
  EXPECT_EQ(group_4.local_group_id(), received_group.local_group_id());
  EXPECT_EQ(new_title, received_group.title());
  EXPECT_EQ(new_color, received_group.color());
  test::CompareSavedTabGroupTabs(group_4.saved_tabs(),
                                 received_group.saved_tabs());
  EXPECT_EQ(saved_tab_group_model_->GetIndexOf(received_group.saved_guid()),
            retrieved_index_);
}

// Tests that SavedTabGroupModelObserver::AddedFromSync passes the correct
// element from the model.
TEST_F(SavedTabGroupModelObserverTest, AddElementFromSync) {
  SavedTabGroup group_4(test::CreateTestSavedTabGroup());
  group_4.SetPosition(0);
  saved_tab_group_model_->AddedFromSync(group_4);

  const int index = retrieved_group_.size() - 1;
  ASSERT_GE(index, 0);

  SavedTabGroup received_group = retrieved_group_[index];
  EXPECT_EQ(group_4.local_group_id(), received_group.local_group_id());
  EXPECT_EQ(group_4.title(), received_group.title());
  EXPECT_EQ(group_4.color(), received_group.color());
  test::CompareSavedTabGroupTabs(group_4.saved_tabs(),
                                 received_group.saved_tabs());
  EXPECT_EQ(saved_tab_group_model_->GetIndexOf(received_group.saved_guid()),
            retrieved_index_);
}

// Tests that SavedTabGroupModelObserver::RemovedFromSync passes the correct
// element from the model.
TEST_F(SavedTabGroupModelObserverTest, RemovedElementFromSync) {
  SavedTabGroup group_4(test::CreateTestSavedTabGroup());
  saved_tab_group_model_->AddedLocally(group_4);
  saved_tab_group_model_->RemovedFromSync(group_4.saved_guid());

  EXPECT_EQ(group_4.saved_guid(), retrieved_guid_);
  EXPECT_FALSE(saved_tab_group_model_->Contains(retrieved_guid_));

  // The model will have already removed and sent the index our element was at
  // before it was removed from the model. As such, we should get -1 when
  // checking the model and 0 for the retrieved index.
  EXPECT_EQ(saved_tab_group_model_->GetIndexOf(retrieved_guid_), std::nullopt);
}

// Tests that SavedTabGroupModelObserver::UpdatedFromSync passes the correct
// element from the model.
TEST_F(SavedTabGroupModelObserverTest, UpdatedElementFromSync) {
  SavedTabGroup group_4(test::CreateTestSavedTabGroup());
  saved_tab_group_model_->AddedLocally(group_4);

  const std::u16string new_title = u"New Title";
  const tab_groups::TabGroupColorId& new_color =
      tab_groups::TabGroupColorId::kBlue;

  const tab_groups::TabGroupVisualData new_visual_data(new_title, new_color,
                                                       /*is_collapsed*/ false);
  saved_tab_group_model_->UpdatedVisualDataFromSync(group_4.saved_guid(),
                                                    &new_visual_data);

  const int index = retrieved_group_.size() - 1;
  ASSERT_GE(index, 0);

  SavedTabGroup received_group = retrieved_group_[index];
  EXPECT_EQ(group_4.local_group_id(), received_group.local_group_id());
  EXPECT_EQ(new_title, received_group.title());
  EXPECT_EQ(new_color, received_group.color());
  test::CompareSavedTabGroupTabs(group_4.saved_tabs(),
                                 received_group.saved_tabs());
  EXPECT_EQ(saved_tab_group_model_->GetIndexOf(received_group.saved_guid()),
            retrieved_index_);
}

// Verify that SavedTabGroupModel::OnGroupClosedInTabStrip passes the correct
// index.
TEST_F(SavedTabGroupModelObserverTest, OnGroupClosedInTabStrip) {
  SavedTabGroup group_4 = test::CreateTestSavedTabGroup();
  LocalTabGroupID tab_group_id = test::GenerateRandomTabGroupID();
  group_4.SetLocalGroupId(tab_group_id);
  saved_tab_group_model_->AddedLocally(group_4);
  const int index =
      saved_tab_group_model_->GetIndexOf(group_4.saved_guid()).value();
  ASSERT_GE(index, 0);

  // Expect the saved group that calls update is the one that was removed from
  // the tabstrip.
  saved_tab_group_model_->OnGroupClosedInTabStrip(
      group_4.local_group_id().value());
  EXPECT_EQ(index, retrieved_index_);

  // Expect the removal of group_4 from the tabstrip makes GetIndexOf not return
  // a valid index when searched by tab group id, but does return the right
  // index when searched by saved guid.
  saved_tab_group_model_->OnGroupClosedInTabStrip(tab_group_id);
  EXPECT_EQ(saved_tab_group_model_->GetIndexOf(tab_group_id), std::nullopt);
  EXPECT_EQ(saved_tab_group_model_->GetIndexOf(group_4.saved_guid()), index);
}

// Tests that SavedTabGroupModelObserver::Moved passes the correct
// element from the model.
TEST_F(SavedTabGroupModelObserverTest, MoveElement) {
  SavedTabGroup stg_1(std::u16string(u"stg_1"),
                      tab_groups::TabGroupColorId::kGrey, {}, std::nullopt,
                      base::Uuid::GenerateRandomV4());
  SavedTabGroup stg_2(std::u16string(u"stg_2"),
                      tab_groups::TabGroupColorId::kGrey, {}, std::nullopt,
                      base::Uuid::GenerateRandomV4());
  SavedTabGroup stg_3(std::u16string(u"stg_3"),
                      tab_groups::TabGroupColorId::kGrey, {}, std::nullopt,
                      base::Uuid::GenerateRandomV4());

  saved_tab_group_model_->AddedLocally(stg_1);
  saved_tab_group_model_->AddedLocally(stg_2);
  saved_tab_group_model_->AddedLocally(stg_3);

  saved_tab_group_model_->ReorderGroupLocally(stg_2.saved_guid(), 2);

  EXPECT_TRUE(reordered_called_);
  EXPECT_EQ(0, saved_tab_group_model_->GetIndexOf(stg_3.saved_guid()));
  EXPECT_EQ(1, saved_tab_group_model_->GetIndexOf(stg_1.saved_guid()));
  EXPECT_EQ(2, saved_tab_group_model_->GetIndexOf(stg_2.saved_guid()));
}

TEST_F(SavedTabGroupModelObserverTest, ReordedTabsUpdatePositions) {
  SavedTabGroup group = test::CreateTestSavedTabGroup();
  base::Uuid group_id = group.saved_guid();
  base::Uuid tab1_id = group.saved_tabs()[0].saved_tab_guid();
  base::Uuid tab2_id = group.saved_tabs()[1].saved_tab_guid();
  saved_tab_group_model_->AddedLocally(group);

  // Move the first tab to the second position.
  saved_tab_group_model_->MoveTabInGroupTo(group_id, tab1_id, 1);

  EXPECT_TRUE(tabs_reodered_called_);
  EXPECT_EQ(0, saved_tab_group_model_->Get(group_id)->GetIndexOfTab(tab2_id));
  EXPECT_EQ(1, saved_tab_group_model_->Get(group_id)->GetIndexOfTab(tab1_id));
}

TEST_F(SavedTabGroupModelObserverTest, GetGroupContainingTab) {
  // Add a non matching SavedTabGroup.
  saved_tab_group_model_->AddedLocally(test::CreateTestSavedTabGroup());

  // Add a matching group/tab and save the ids used for GetGroupContainingTab.
  SavedTabGroup matching_group = test::CreateTestSavedTabGroup();
  base::Uuid matching_group_guid = matching_group.saved_guid();

  base::Uuid matching_tab_guid = base::Uuid::GenerateRandomV4();
  LocalTabID matching_local_tab_id = test::GenerateRandomTabID();

  SavedTabGroupTab tab(GURL(url::kAboutBlankURL), std::u16string(u"title"),
                       matching_group.saved_guid(), /*position=*/std::nullopt,
                       matching_tab_guid, matching_local_tab_id);
  matching_group.AddTabLocally(std::move(tab));
  saved_tab_group_model_->AddedLocally(std::move(matching_group));

  // Add another non matching SavedTabGroup.
  saved_tab_group_model_->AddedLocally(test::CreateTestSavedTabGroup());
  ASSERT_EQ(3, saved_tab_group_model_->Count());

  // call GetGroupContainingTab with the 2 ids and expect them to return.
  EXPECT_EQ(saved_tab_group_model_->Get(matching_group_guid),
            saved_tab_group_model_->GetGroupContainingTab(matching_tab_guid));
  EXPECT_EQ(
      saved_tab_group_model_->Get(matching_group_guid),
      saved_tab_group_model_->GetGroupContainingTab(matching_local_tab_id));

  // Expect GetGroupContainingTab to return null when there is no match.
  EXPECT_EQ(nullptr, saved_tab_group_model_->GetGroupContainingTab(
                         base::Uuid::GenerateRandomV4()));
  EXPECT_EQ(nullptr,
            saved_tab_group_model_->GetGroupContainingTab(LocalTabID()));
}

TEST_F(SavedTabGroupModelObserverTest, UpdateLocalCacheGuid) {
  base::Uuid group_1_id = base::Uuid::GenerateRandomV4();
  base::Uuid group_2_id = base::Uuid::GenerateRandomV4();
  base::Uuid group_3_id = base::Uuid::GenerateRandomV4();
  base::Uuid group_4_id = base::Uuid::GenerateRandomV4();

  const std::string edit_to_cache_guid = "edit_to_cache_guid";
  const std::string dont_edit_cache_guid = "dont_edit_cache_guid";
  const std::string second_edit_cache_guid = "second_edit_cache_guid";
  SavedTabGroup group1(u"Tab Group 1", tab_groups::TabGroupColorId::kRed, {},
                       std::nullopt /*position*/, group_1_id /*saved_guid*/,
                       std::nullopt /*local_group_id*/,
                       std::nullopt /*creator_cache_guid*/,
                       std::nullopt /*last_updater_cache_guid*/);
  saved_tab_group_model_->AddedLocally(std::move(group1));
  SavedTabGroup group2(u"Tab Group 2", tab_groups::TabGroupColorId::kRed, {},
                       std::nullopt /*position*/, group_2_id /*saved_guid*/,
                       std::nullopt /*local_group_id*/,
                       std::nullopt /*creator_cache_guid*/,
                       std::nullopt /*last_updater_cache_guid*/);
  saved_tab_group_model_->AddedLocally(group2);
  SavedTabGroup group3(u"Tab Group 3", tab_groups::TabGroupColorId::kRed, {},
                       std::nullopt /*position*/, group_3_id /*saved_guid*/,
                       std::nullopt /*local_group_id*/,
                       dont_edit_cache_guid /*creator_cache_guid*/,
                       std::nullopt /*last_updater_cache_guid*/);
  saved_tab_group_model_->AddedLocally(group3);
  SavedTabGroup group4(u"Tab Group 4", tab_groups::TabGroupColorId::kRed, {},
                       std::nullopt /*position*/, group_4_id /*saved_guid*/,
                       std::nullopt /*local_group_id*/,
                       second_edit_cache_guid /*creator_cache_guid*/,
                       std::nullopt /*last_updater_cache_guid*/);
  saved_tab_group_model_->AddedLocally(group4);

  saved_tab_group_model_->UpdateLocalCacheGuid(std::nullopt,
                                               edit_to_cache_guid);
  EXPECT_EQ(saved_tab_group_model_->Get(group_1_id)->creator_cache_guid(),
            edit_to_cache_guid);
  EXPECT_EQ(saved_tab_group_model_->Get(group_2_id)->creator_cache_guid(),
            edit_to_cache_guid);
  EXPECT_EQ(saved_tab_group_model_->Get(group_3_id)->creator_cache_guid(),
            dont_edit_cache_guid);
  EXPECT_EQ(saved_tab_group_model_->Get(group_4_id)->creator_cache_guid(),
            second_edit_cache_guid);

  saved_tab_group_model_->UpdateLocalCacheGuid(second_edit_cache_guid,
                                               edit_to_cache_guid);
  EXPECT_EQ(saved_tab_group_model_->Get(group_1_id)->creator_cache_guid(),
            edit_to_cache_guid);
  EXPECT_EQ(saved_tab_group_model_->Get(group_2_id)->creator_cache_guid(),
            edit_to_cache_guid);
  EXPECT_EQ(saved_tab_group_model_->Get(group_3_id)->creator_cache_guid(),
            dont_edit_cache_guid);
  EXPECT_EQ(saved_tab_group_model_->Get(group_4_id)->creator_cache_guid(),
            edit_to_cache_guid);
}

TEST_F(SavedTabGroupModelObserverTest, UpdateLocalCacheGuidForTabs) {
  const std::string cache_guid1 = "cache_guid1";
  const std::string cache_guid2 = "cache_guid2";
  const std::string cache_guid_tab2 = "cache_guid_tab2";

  SavedTabGroup group = test::CreateTestSavedTabGroup();
  base::Uuid group_id = group.saved_guid();
  SavedTabGroupTab tab1(GURL(url::kAboutBlankURL), std::u16string(u"title"),
                        group.saved_guid(), /*position=*/std::nullopt,
                        std::nullopt, std::nullopt);
  SavedTabGroupTab tab2(GURL(url::kAboutBlankURL), std::u16string(u"title"),
                        group.saved_guid(), /*position=*/std::nullopt,
                        std::nullopt, std::nullopt);
  tab2.SetCreatorCacheGuid(cache_guid_tab2);
  group.AddTabLocally(tab1);
  group.AddTabLocally(tab2);
  saved_tab_group_model_->AddedLocally(group);

  base::Uuid tab1_id = tab1.saved_tab_guid();
  base::Uuid tab2_id = tab2.saved_tab_guid();
  const SavedTabGroup* retrieved_group = saved_tab_group_model_->Get(group_id);
  const SavedTabGroupTab* retrieved_tab1 = retrieved_group->GetTab(tab1_id);
  const SavedTabGroupTab* retrieved_tab2 = retrieved_group->GetTab(tab2_id);

  saved_tab_group_model_->UpdateLocalCacheGuid(std::nullopt, cache_guid1);

  EXPECT_EQ(retrieved_group->creator_cache_guid(), cache_guid1);
  EXPECT_EQ(retrieved_tab1->creator_cache_guid(), cache_guid1);
  EXPECT_EQ(retrieved_tab2->creator_cache_guid(), cache_guid_tab2);

  saved_tab_group_model_->UpdateLocalCacheGuid(cache_guid1, cache_guid2);
  EXPECT_EQ(retrieved_group->creator_cache_guid(), cache_guid2);
  EXPECT_EQ(retrieved_tab1->creator_cache_guid(), cache_guid2);
  EXPECT_EQ(retrieved_tab2->creator_cache_guid(), cache_guid_tab2);

  saved_tab_group_model_->UpdateLocalCacheGuid(cache_guid_tab2, std::nullopt);
  EXPECT_EQ(retrieved_group->creator_cache_guid(), cache_guid2);
  EXPECT_EQ(retrieved_tab1->creator_cache_guid(), cache_guid2);
  EXPECT_EQ(retrieved_tab2->creator_cache_guid(), std::nullopt);
}

TEST_F(SavedTabGroupModelObserverTest,
       ShouldMarkSharedTabGroupsAsTransitioned) {
  SavedTabGroup saved_group = test::CreateTestSavedTabGroup();
  saved_tab_group_model_->AddedLocally(saved_group);

  SavedTabGroup shared_group =
      saved_group.CloneAsSharedTabGroup(CollaborationId("collaboration"));
  saved_tab_group_model_->AddedLocally(shared_group);
  ASSERT_TRUE(saved_tab_group_model_->Get(shared_group.saved_guid())
                  ->is_transitioning_to_shared());

  ClearSignals();
  ASSERT_THAT(retrieved_group_, IsEmpty());

  saved_tab_group_model_->MarkTransitionedToShared(shared_group.saved_guid());
  EXPECT_FALSE(saved_tab_group_model_->Get(shared_group.saved_guid())
                   ->is_transitioning_to_shared());
  EXPECT_THAT(retrieved_group_,
              UnorderedElementsAre(HasGroupId(shared_group.saved_guid())));
}

TEST_F(SavedTabGroupModelObserverTest,
       TriggersObserverWhenSettingTabLastSeenTime) {
  SavedTabGroup saved_group = test::CreateTestSavedTabGroup();
  saved_group.SetCollaborationId(tab_groups::CollaborationId("collab_id"));
  saved_tab_group_model_->AddedLocally(saved_group);
  const base::Uuid group_id = saved_group.saved_guid();

  EXPECT_FALSE(saved_tab_group_model_->Get(group_id)
                   ->saved_tabs()
                   .front()
                   .last_seen_time()
                   .has_value());

  base::Time last_seen_time = base::Time::Now();
  saved_tab_group_model_->UpdateTabLastSeenTime(
      saved_group.saved_guid(),
      saved_group.saved_tabs().front().saved_tab_guid(), last_seen_time,
      TriggerSource::LOCAL);

  // Observer method was called.
  EXPECT_EQ(last_seen_tab_id_, saved_tab_group_model_->Get(group_id)
                                   ->saved_tabs()
                                   .front()
                                   .saved_tab_guid());
  EXPECT_TRUE(saved_tab_group_model_->Get(group_id)
                  ->saved_tabs()
                  .front()
                  .last_seen_time()
                  .has_value());
  EXPECT_EQ(last_seen_time, saved_tab_group_model_->Get(group_id)
                                ->saved_tabs()
                                .front()
                                .last_seen_time());
}

TEST_F(SavedTabGroupModelTest, UpdatePositionForSharedGroupFromSyncFromSync) {
  RemoveTestData();

  // Create some tab groups with initial orders.
  SavedTabGroup group_1(u"Group 1", tab_groups::TabGroupColorId::kRed, {}, 0);
  SavedTabGroup group_2(u"Group 2", tab_groups::TabGroupColorId::kOrange, {},
                        1);
  SavedTabGroup group_3(u"Group 3", tab_groups::TabGroupColorId::kYellow, {},
                        2);
  SavedTabGroup group_4(u"Group 4", tab_groups::TabGroupColorId::kGreen, {},
                        std::nullopt);

  group_1.SetCollaborationId(CollaborationId("collaboration"));
  group_2.SetCollaborationId(CollaborationId("collaboration"));
  group_3.SetCollaborationId(CollaborationId("collaboration"));
  group_4.SetCollaborationId(CollaborationId("collaboration"));

  // This is the order we expect the groups in the model to be.
  std::vector<SavedTabGroup> groups = {group_3, group_2, group_4, group_1};

  // Add the groups into the model in an arbitrary order.
  saved_tab_group_model_->AddedLocally(group_1);
  saved_tab_group_model_->AddedLocally(group_2);
  saved_tab_group_model_->AddedLocally(group_3);
  saved_tab_group_model_->AddedLocally(group_4);

  // Change the order of groups.
  saved_tab_group_model_->UpdatePositionForSharedGroupFromSync(
      group_1.saved_guid(), std::nullopt);
  saved_tab_group_model_->UpdatePositionForSharedGroupFromSync(
      group_2.saved_guid(), 1);
  saved_tab_group_model_->UpdatePositionForSharedGroupFromSync(
      group_3.saved_guid(), 0);
  saved_tab_group_model_->UpdatePositionForSharedGroupFromSync(
      group_4.saved_guid(), 2);

  EXPECT_EQ(saved_tab_group_model_->saved_tab_groups().size(), groups.size());
  for (size_t i = 0; i < groups.size(); ++i) {
    EXPECT_EQ(groups[i].saved_guid(),
              saved_tab_group_model_->saved_tab_groups()[i].saved_guid());
  }
}

}  // namespace

}  // namespace tab_groups
