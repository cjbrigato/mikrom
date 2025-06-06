// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.data_sharing;

import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;

import java.util.List;

/** Information about a group. */
@JNINamespace("data_sharing")
@NullMarked
public class GroupData {
    public final String displayName;
    public final @Nullable List<GroupMember> members;
    public final GroupToken groupToken;

    public GroupData(
            String groupId,
            String displayName,
            GroupMember @Nullable [] members,
            String accessToken) {
        this.displayName = displayName;
        this.members = members == null ? null : List.of(members);
        this.groupToken = new GroupToken(groupId, accessToken);
    }

    @CalledByNative
    private static GroupData createGroupData(
            String groupId,
            String displayName,
            GroupMember @Nullable [] members,
            String accessToken) {
        return new GroupData(groupId, displayName, members, accessToken);
    }
}
