// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.data_sharing.configs;

import android.graphics.Bitmap;

import org.chromium.base.Callback;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.components.data_sharing.GroupToken;
import org.chromium.components.data_sharing.SharedDataPreview;
import org.chromium.components.sync.protocol.GroupData;

/** Config class for the Data Sharing Join UI. */
@NullMarked
public class DataSharingJoinUiConfig {

    // --- Group related Info ---
    private final @Nullable GroupToken mGroupToken;

    // --- Tab Group Details ---
    private final @Nullable Bitmap mPreviewImage;
    private final @Nullable SharedDataPreview mSharedDataPreview;

    // --- Join Usage Config ---
    private final @Nullable JoinCallback mJoinCallback;
    private final @Nullable DataSharingUiConfig mCommonConfig;

    /** Callback interface for data sharing join UI events. */
    public interface JoinCallback {
        // DEPRECATED: use the version with callback, to close the loading UI.
        default void onGroupJoined(GroupData groupData) {}

        // Called when group is joined, the `onSessionFinished` should be called
        // when any processing is done.
        default void onGroupJoinedWithWait(GroupData groupData, Callback<Boolean> onJoinFinished) {}

        default void onPreviewTabGroupDetailsClicked(String groupId) {}

        // This will always be called when user joins the group, ui closes, or
        // session is finished.
        default void onSessionFinished() {}
    }

    private DataSharingJoinUiConfig(Builder builder) {
        this.mGroupToken = builder.mGroupToken;
        this.mPreviewImage = builder.mPreviewImage;
        this.mSharedDataPreview = builder.mSharedDataPreview;
        this.mJoinCallback = builder.mJoinCallback;
        this.mCommonConfig = builder.mCommonConfig;
    }

    public @Nullable GroupToken getGroupToken() {
        return mGroupToken;
    }

    public @Nullable Bitmap getPreviewImage() {
        return mPreviewImage;
    }

    public @Nullable SharedDataPreview getSharedDataPreview() {
        return mSharedDataPreview;
    }

    public @Nullable JoinCallback getJoinCallback() {
        return mJoinCallback;
    }

    public @Nullable DataSharingUiConfig getCommonConfig() {
        return mCommonConfig;
    }

    // Builder class
    public static class Builder {
        private @Nullable GroupToken mGroupToken;
        private @Nullable Bitmap mPreviewImage;
        private @Nullable SharedDataPreview mSharedDataPreview;
        private @Nullable JoinCallback mJoinCallback;
        private @Nullable DataSharingUiConfig mCommonConfig;

        /**
         * Sets the group token for the data sharing group.
         *
         * @param groupToken The token for the data sharing group includes groupId and token.
         */
        public Builder setGroupToken(GroupToken groupToken) {
            this.mGroupToken = groupToken;
            return this;
        }

        /**
         * Sets the preview image for the tab group.
         *
         * @param previewImage The preview image of tab group.
         */
        public Builder setPreviewImage(Bitmap previewImage) {
            this.mPreviewImage = previewImage;
            return this;
        }

        /**
         * Sets the shared data preview for tab group.
         *
         * @param sharedDataPreview Details about the tab group.
         */
        public Builder setSharedDataPreview(SharedDataPreview sharedDataPreview) {
            this.mSharedDataPreview = sharedDataPreview;
            return this;
        }

        /**
         * Sets the callback for join UI events.
         *
         * @param joinCallback The callback to use for join UI events.
         */
        public Builder setJoinCallback(JoinCallback joinCallback) {
            this.mJoinCallback = joinCallback;
            return this;
        }

        /**
         * Sets the data sharing common UI config.
         *
         * @param commonConfig The common UI config.
         */
        public Builder setCommonConfig(DataSharingUiConfig commonConfig) {
            this.mCommonConfig = commonConfig;
            return this;
        }

        public DataSharingJoinUiConfig build() {
            return new DataSharingJoinUiConfig(this);
        }
    }
}
