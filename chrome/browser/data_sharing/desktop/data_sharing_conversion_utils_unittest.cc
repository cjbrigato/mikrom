// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/data_sharing/desktop/data_sharing_conversion_utils.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace data_sharing {
class DataSharingConversionUtilsTest : public testing::Test {};

TEST_F(DataSharingConversionUtilsTest, ConvertGroup) {
  auto group = data_sharing::mojom::GroupData::New();
  group->group_id = "123";
  group->display_name = "group";
  group->access_token = "abc";

  auto member = data_sharing::mojom::GroupMember::New();
  member->gaia_id = "456";
  member->role = data_sharing::mojom::MemberRole::kMember;
  member->display_name = "member display";
  member->given_name = "member given";
  member->email = "test@gmail.com";
  member->avatar_url = GURL("example.com");
  member->creation_time = base::Time::FromMillisecondsSinceUnixEpoch(123);
  member->last_updated_time = base::Time::FromMillisecondsSinceUnixEpoch(456);
  group->members.push_back(std::move(member));

  auto former_member = data_sharing::mojom::GroupMember::New();
  former_member->gaia_id = "789";
  former_member->role = data_sharing::mojom::MemberRole::kMember;
  former_member->display_name = "former member display";
  former_member->given_name = "former member given";
  former_member->email = "test@gmail.com";
  former_member->avatar_url = GURL("example.com");
  former_member->creation_time =
      base::Time::FromMillisecondsSinceUnixEpoch(234);
  former_member->last_updated_time =
      base::Time::FromMillisecondsSinceUnixEpoch(345);
  group->former_members.push_back(std::move(former_member));

  auto result = ConvertGroup(group);

  ASSERT_EQ(group->display_name, result.display_name());
  ASSERT_EQ(group->group_id, result.group_id());
  ASSERT_EQ(group->access_token, result.access_token());

  ASSERT_EQ(1, result.members_size());
  ASSERT_EQ(group->members[0]->gaia_id, result.members(0).gaia_id());
  ASSERT_EQ(data_sharing_pb::MEMBER_ROLE_MEMBER, result.members(0).role());
  ASSERT_EQ(group->members[0]->display_name, result.members(0).display_name());
  ASSERT_EQ(group->members[0]->given_name, result.members(0).given_name());
  ASSERT_EQ(group->members[0]->email, result.members(0).email());
  ASSERT_EQ(group->members[0]->avatar_url, result.members(0).avatar_url());
  ASSERT_EQ(group->members[0]->creation_time.InMillisecondsSinceUnixEpoch(),
            result.members(0).creation_time_unix_epoch_millis());
  ASSERT_EQ(group->members[0]->last_updated_time.InMillisecondsSinceUnixEpoch(),
            result.members(0).last_updated_time_unix_epoch_millis());

  ASSERT_EQ(1, result.former_members_size());
  ASSERT_EQ(group->former_members[0]->gaia_id,
            result.former_members(0).gaia_id());
  ASSERT_EQ(data_sharing_pb::MEMBER_ROLE_MEMBER,
            result.former_members(0).role());
  ASSERT_EQ(group->former_members[0]->display_name,
            result.former_members(0).display_name());
  ASSERT_EQ(group->former_members[0]->given_name,
            result.former_members(0).given_name());
  ASSERT_EQ(group->former_members[0]->email, result.former_members(0).email());
  ASSERT_EQ(group->former_members[0]->avatar_url,
            result.former_members(0).avatar_url());
  ASSERT_EQ(
      group->former_members[0]->creation_time.InMillisecondsSinceUnixEpoch(),
      result.former_members(0).creation_time_unix_epoch_millis());
  ASSERT_EQ(group->former_members[0]
                ->last_updated_time.InMillisecondsSinceUnixEpoch(),
            result.former_members(0).last_updated_time_unix_epoch_millis());
}
}  // namespace data_sharing
