// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/data_sharing/desktop/data_sharing_conversion_utils.h"

namespace data_sharing {

data_sharing_pb::MemberRole ConvertMemberRole(
    data_sharing::mojom::MemberRole role) {
  switch (role) {
    case data_sharing::mojom::MemberRole::kOwner:
      return data_sharing_pb::MEMBER_ROLE_OWNER;
    case data_sharing::mojom::MemberRole::kMember:
      return data_sharing_pb::MEMBER_ROLE_MEMBER;
    case data_sharing::mojom::MemberRole::kInvitee:
      return data_sharing_pb::MEMBER_ROLE_INVITEE;
    case data_sharing::mojom::MemberRole::kFormerMember:
      return data_sharing_pb::MEMBER_ROLE_FORMER_MEMBER;
    default:
      return data_sharing_pb::MEMBER_ROLE_UNSPECIFIED;
  }
}

data_sharing_pb::GroupMember ConvertGroupMember(
    const data_sharing::mojom::GroupMemberPtr& member) {
  data_sharing_pb::GroupMember result;
  result.set_gaia_id(member->gaia_id);
  result.set_given_name(member->given_name);
  result.set_display_name(member->display_name);
  result.set_email(member->email);
  result.set_avatar_url(member->avatar_url.spec());
  result.set_role(ConvertMemberRole(member->role));
  result.set_creation_time_unix_epoch_millis(
      member->creation_time.InMillisecondsSinceUnixEpoch());
  result.set_last_updated_time_unix_epoch_millis(
      member->last_updated_time.InMillisecondsSinceUnixEpoch());
  return result;
}

data_sharing_pb::GroupData ConvertGroup(
    const data_sharing::mojom::GroupDataPtr& group_data) {
  data_sharing_pb::GroupData result;
  result.set_group_id(group_data->group_id);
  result.set_display_name(group_data->display_name);
  result.set_access_token(group_data->access_token);
  for (auto& member : group_data->members) {
    *result.add_members() = ConvertGroupMember(member);
  }
  for (auto& former_member : group_data->former_members) {
    *result.add_former_members() = ConvertGroupMember(former_member);
  }
  return result;
}

}  // namespace data_sharing
