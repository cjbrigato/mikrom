// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.share;

import android.graphics.Bitmap;

import org.jni_zero.NativeMethods;

import org.chromium.build.annotations.NullMarked;

/** A Java API for requesting bitmap download from Chrome's download manager service. */
@NullMarked
public class BitmapDownloadRequest {
    public static void downloadBitmap(String fileName, Bitmap bitmap) {
        BitmapDownloadRequestJni.get().downloadBitmap(fileName, bitmap);
    }

    @NativeMethods
    interface Natives {
        void downloadBitmap(String fileName, Bitmap bitmap);
    }
}
