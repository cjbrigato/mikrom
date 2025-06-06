// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// <if expr="_google_chrome">
import '/data_sharing_sdk.js';
// </if>
// <if expr="not _google_chrome">
import './dummy_data_sharing_sdk.js';

// </if>

import type {DataSharingMemberRole, DataSharingSdkGroupData, DataSharingSdkGroupMember} from './data_sharing_sdk_types.js';
import {DataSharingMemberRoleEnum} from './data_sharing_sdk_types.js';
import type {GroupData, GroupMember} from './group_data.mojom-webui.js';
import {MemberRole} from './group_data.mojom-webui.js';

// Utilities to convert DataSharingSdkGroup in data sharing sdk to GroupData in
// group_data.mojom
export function toMojomGroupData(group: DataSharingSdkGroupData): GroupData {
  const members: GroupMember[] = [];
  for (const member of group.members) {
    members.push(toMojomGroupMember(member));
  }
  const formerMembers: GroupMember[] = [];
  for (const member of group.formerMembers) {
    formerMembers.push(toMojomGroupMember(member));
  }
  return {
    groupId: group.groupId,
    displayName: group.displayName || '',
    accessToken: group.accessToken || '',
    members,
    formerMembers,
  };
}

function toMojomGroupMember(member: DataSharingSdkGroupMember): GroupMember {
  return {
    gaiaId: member.focusObfuscatedGaiaId,
    displayName: member.displayName,
    email: member.email,
    role: toMojomRole(member.role),
    avatarUrl: {url: member.avatarUrl},
    // Bandage for crbug.com/372249284, clean this up after the root cause is
    // addressed.
    givenName: member.givenName || '',
    // TODO(https://crbug.com/420986712): Populate these fields from the
    // javascript library.
    creationTime: new Date(0),
    lastUpdatedTime: new Date(0),
  };
}

function toMojomRole(role: DataSharingMemberRole): MemberRole {
  // Applicant will eventually be added, it's supported by the Data Sharing SDK.
  switch (role) {
    case DataSharingMemberRoleEnum.INVITEE:
      return MemberRole.kInvitee;
    case DataSharingMemberRoleEnum.MEMBER:
      return MemberRole.kMember;
    case DataSharingMemberRoleEnum.OWNER:
      return MemberRole.kOwner;
    case DataSharingMemberRoleEnum.FORMER_MEMBER:
      return MemberRole.kFormerMember;
    default:
      return MemberRole.kUnspecified;
  }
}
