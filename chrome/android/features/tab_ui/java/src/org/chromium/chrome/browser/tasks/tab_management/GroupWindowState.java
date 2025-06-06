// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import androidx.annotation.IntDef;

import org.chromium.build.annotations.NullMarked;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

/**
 * State enum to track where a group lives. It can either be in the current tab
 * model/window/activity, in the current activity and closing, in another one, or hidden. Hidden
 * means only the sync side know about it. Everything is assumed to be non-incognito. Tab groups in
 * other tab models are difficult to work with, since often tha tab model is not even loaded into
 * memory. For currently closing groups we need to special case the behavior to properly undo or
 * commit the pending operations.
 *
 * <p>Emitted to UMA histograms, make sure to update when adding new values.
 */
@IntDef({
    GroupWindowState.IN_CURRENT,
    GroupWindowState.IN_CURRENT_CLOSING,
    GroupWindowState.IN_ANOTHER,
    GroupWindowState.HIDDEN,
    GroupWindowState.COUNT,
})
@Retention(RetentionPolicy.SOURCE)
@Target({ElementType.TYPE_USE})
@NullMarked
@interface GroupWindowState {
    int IN_CURRENT = 0;
    int IN_CURRENT_CLOSING = 1;
    int IN_ANOTHER = 2;
    int HIDDEN = 3;
    int COUNT = 4;
}
