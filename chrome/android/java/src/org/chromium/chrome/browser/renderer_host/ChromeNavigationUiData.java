// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.renderer_host;

import org.jni_zero.NativeMethods;

import org.chromium.build.annotations.NullMarked;

/** Provides a way to attach chrome-specific navigation ui data from java. */
@NullMarked
public class ChromeNavigationUiData {
    private long mBookmarkId;

    /**
     * Reconstructs the native NavigationUIData for this Java NavigationUIData, returning its native
     * pointer and transferring ownership to the calling function.
     */
    public long createUnownedNativeCopy() {
        return ChromeNavigationUiDataJni.get()
                .createUnownedNativeCopy(ChromeNavigationUiData.this, mBookmarkId);
    }

    /** Set the bookmark id on this navigation. */
    public void setBookmarkId(long bookmarkId) {
        mBookmarkId = bookmarkId;
    }

    @NativeMethods
    interface Natives {
        long createUnownedNativeCopy(ChromeNavigationUiData caller, long bookmarkId);
    }
}
