// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.feed;

import org.chromium.build.annotations.NullMarked;

/** Interface to listen events about feed loading from FeedStream. */
@NullMarked
public interface FeedContentFirstLoadWatcher {
    /** Called when some actual (non-native) content has loaded for the first time. */
    void nonNativeContentLoaded(@StreamKind int kind);
}
