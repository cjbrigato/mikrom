// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.notifications;

import android.graphics.Bitmap;

import org.jni_zero.CalledByNative;
import org.jni_zero.JniType;

import org.chromium.build.annotations.NullMarked;

/** Helper class for passing notification action information over the JNI. */
@NullMarked
class ActionInfo {
    public final String title;
    public final Bitmap icon;
    public final int type;
    public final String placeholder;

    private ActionInfo(String title, Bitmap icon, int type, String placeholder) {
        this.title = title;
        this.icon = icon;
        this.type = type;
        this.placeholder = placeholder;
    }

    @CalledByNative
    private static ActionInfo createActionInfo(
            @JniType("std::u16string") String title,
            Bitmap icon,
            int type,
            @JniType("std::u16string") String placeholder) {
        return new ActionInfo(title, icon, type, placeholder);
    }
}
