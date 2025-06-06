// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.autofill;

import android.view.accessibility.AccessibilityEvent;

import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;
import org.jni_zero.JniType;

import org.chromium.build.annotations.NullMarked;
import org.chromium.ui.accessibility.AccessibilityState;

/** Helper methods for accessibility. */
@JNINamespace("autofill")
@NullMarked
public class AutofillAccessibilityUtils {
    // Avoid instantiation by accident.
    private AutofillAccessibilityUtils() {}

    @CalledByNative
    private static void announce(@JniType("std::u16string") String message) {
        if (!AccessibilityState.isTouchExplorationEnabled()) return;

        AccessibilityEvent accessibilityEvent = AccessibilityEvent.obtain();
        accessibilityEvent.setEventType(AccessibilityEvent.TYPE_ANNOUNCEMENT);
        accessibilityEvent.getText().add(message);

        AccessibilityState.sendAccessibilityEvent(accessibilityEvent);
    }
}
