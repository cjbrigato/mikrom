// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.notifications;

import static org.chromium.chrome.browser.base.SplitCompatApplication.CHROME_SPLIT_NAME;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

import org.chromium.base.BundleUtils;
import org.chromium.build.annotations.IdentifierNameString;
import org.chromium.build.annotations.NullMarked;
import org.chromium.chrome.browser.base.SplitCompatIntentService;

/** See {@link NotificationServiceImpl}. */
@NullMarked
public class NotificationService extends SplitCompatIntentService {
    private static final String TAG = NotificationService.class.getSimpleName();

    @SuppressWarnings("FieldCanBeFinal") // @IdentifierNameString requires non-final
    private static @IdentifierNameString String sImplClassName =
            "org.chromium.chrome.browser.notifications.NotificationServiceImpl";

    public NotificationService() {
        super(sImplClassName, TAG);
    }

    /**
     * This receiver forwards the onReceive() call to the implementation version. This is needed to
     * handle pending intents referring to the old receiver name.
     */
    public static class Receiver extends BroadcastReceiver {
        @Override
        public void onReceive(Context context, Intent intent) {
            BroadcastReceiver receiver =
                    (BroadcastReceiver)
                            BundleUtils.newInstance(
                                    "org.chromium.chrome.browser.notifications.NotificationServiceImpl$Receiver",
                                    CHROME_SPLIT_NAME);
            receiver.onReceive(context, intent);
        }
    }
}
