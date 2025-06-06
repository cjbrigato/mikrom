// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.android_webview.selection;

import android.os.Build;

import org.chromium.android_webview.common.Lifetime;
import org.chromium.build.annotations.NullMarked;
import org.chromium.components.autofill.AutofillSelectionActionMenuDelegate;

/** Class responsible to create SelectionActionMenuDelegate as required. */
@Lifetime.Singleton
@NullMarked
public class SelectionActionMenuDelegateProvider {

    private SelectionActionMenuDelegateProvider() {}

    public static AutofillSelectionActionMenuDelegate getSelectionActionMenuDelegate() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU
                && SamsungSelectionActionMenuDelegate.shouldUseSamsungMenuItemOrdering()) {
            return new SamsungSelectionActionMenuDelegate();
        }
        return new AwSelectionActionMenuDelegate();
    }
}
