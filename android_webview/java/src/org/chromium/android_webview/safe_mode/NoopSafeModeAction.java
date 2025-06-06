// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.android_webview.safe_mode;

import org.chromium.android_webview.common.Lifetime;
import org.chromium.android_webview.common.SafeModeAction;
import org.chromium.android_webview.common.SafeModeActionIds;
import org.chromium.base.Log;
import org.chromium.build.annotations.NullMarked;

/** A {@link SafeModeAction} that has no effect. */
@Lifetime.Singleton
@NullMarked
public class NoopSafeModeAction implements SafeModeAction {
    private static final String TAG = "WebViewSafeMode";
    // This ID should not be changed or reused.
    private static final String ID = SafeModeActionIds.NOOP;

    @Override
    public String getId() {
        return ID;
    }

    @Override
    public boolean execute() {
        // This is intentionally no operation as this action is meant for testing purposes only.
        Log.i(TAG, "NOOP action executed");
        return true;
    }
}
