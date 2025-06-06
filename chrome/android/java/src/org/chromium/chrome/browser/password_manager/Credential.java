// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.password_manager;


import android.graphics.drawable.Drawable;

import org.jni_zero.CalledByNative;
import org.jni_zero.JniType;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;

/**
 * Credential type which is used to represent credential which will be shown in account chooser
 * infobar.
 */
@NullMarked
public class Credential {
    private final String mUsername;
    private final String mDisplayName;
    private final String mOriginUrl;
    private final String mFederation;
    private final int mIndex;
    private @Nullable Drawable mAvatar;

    /**
     * @param username username which is used to authenticate user.
     *                 The value is PasswordForm::username_value.
     * @param displayName user friendly name to show in the UI. It can be empty.
     *                    The value is PasswordForm::display_name.
     * @param originUrl The origin URL for this credential, only set for PSL matches.
     * @param federation Identity provider name for this credential (empty for local credentials).
     * @param index position in array of credentials.
     */
    public Credential(
            String username, String displayName, String originUrl, String federation, int index) {
        mUsername = username;
        mDisplayName = displayName;
        mOriginUrl = originUrl;
        mFederation = federation;
        mIndex = index;
        mAvatar = null;
    }

    public String getUsername() {
        return mUsername;
    }

    public String getDisplayName() {
        return mDisplayName;
    }

    public String getOriginUrl() {
        return mOriginUrl;
    }

    public String getFederation() {
        return mFederation;
    }

    public int getIndex() {
        return mIndex;
    }

    public @Nullable Drawable getAvatar() {
        return mAvatar;
    }

    public void setAvatar(Drawable avatar) {
        mAvatar = avatar;
    }

    @CalledByNative
    private static Credential createCredential(
            @JniType("std::u16string") String username,
            @JniType("std::u16string") String displayName,
            @JniType("std::string") String originUrl,
            @JniType("std::string") String federation,
            int index) {
        return new Credential(username, displayName, originUrl, federation, index);
    }

    @CalledByNative
    private static Credential[] createCredentialArray(int size) {
        return new Credential[size];
    }
}
