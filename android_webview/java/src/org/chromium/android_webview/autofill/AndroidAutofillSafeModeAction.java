// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.android_webview.autofill;

import org.chromium.android_webview.common.Lifetime;
import org.chromium.android_webview.common.SafeModeAction;
import org.chromium.android_webview.common.SafeModeActionIds;
import org.chromium.build.annotations.NullMarked;

/**
 * A {@link SafeModeAction} to disable autofill provided by Android framework.
 *
 * <p>This action does not itself do any work. AwContents checks if this action is enabled, to
 * decide whether to initialize android autofill or not.
 */
@Lifetime.Singleton
@NullMarked
public class AndroidAutofillSafeModeAction implements SafeModeAction {
    // This ID should not be changed or reused.
    private static final String ID = SafeModeActionIds.DISABLE_ANDROID_AUTOFILL;

    private static boolean sIsAndroidAutofillDisabled;

    @Override
    public String getId() {
        return ID;
    }

    @Override
    public boolean execute() {
        sIsAndroidAutofillDisabled = true;
        return true;
    }

    public static boolean isAndroidAutofillDisabled() {
        return sIsAndroidAutofillDisabled;
    }
}
