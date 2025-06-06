// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.push_messaging;

import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;

import org.chromium.base.ThreadUtils;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;

/**
 * Observes events and changes in the PushMessagingService.
 *
 * <p>Threading model: UI thread only.
 *
 * <p>TODO(peter): Delete this class once delivery receipts are implemented and use those instead.
 * This really only exists for test purposes.
 */
@JNINamespace("chrome::android")
@NullMarked
public class PushMessagingServiceObserver {
    /**
     * The listener that needs to be notified of events and changes observed by the
     * PushMessagingServiceObserver. May be null.
     */
    private static @Nullable Listener sListener;

    /**
     * Interface for the listener that needs to be notified of events and changes observed by the
     * PushMessagingServiceObserver.
     */
    public interface Listener {
        /** Called when a push message has been handled. */
        void onMessageHandled();
    }

    public static void setListenerForTesting(@Nullable Listener listener) {
        ThreadUtils.assertOnUiThread();
        sListener = listener;
    }

    @CalledByNative
    private static void onMessageHandled() {
        ThreadUtils.assertOnUiThread();
        if (sListener != null) {
            sListener.onMessageHandled();
        }
    }

    private PushMessagingServiceObserver() {} // Private. Not meant to be instantiated.
}
