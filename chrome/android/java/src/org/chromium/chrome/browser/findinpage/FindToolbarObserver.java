// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.findinpage;

import org.chromium.build.annotations.NullMarked;

/** Observer for find in page actions. */
@NullMarked
public interface FindToolbarObserver {
    /** Notified when the find in page toolbar has been shown. */
    void onFindToolbarShown();

    /** Notified when the find in page toolbar has been hidden. */
    void onFindToolbarHidden();
}
