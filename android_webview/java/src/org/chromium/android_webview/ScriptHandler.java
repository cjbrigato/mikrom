// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.android_webview;

import org.chromium.base.ThreadUtils;
import org.chromium.build.annotations.NullMarked;

import java.lang.ref.WeakReference;

/** Used for Js Java interaction, to delete the document start JavaScript snippet. */
@NullMarked
public class ScriptHandler {
    private final WeakReference<AwContents> mAwContentsRef;
    private final int mScriptId;

    public ScriptHandler(AwContents awContents, int scriptId) {
        assert scriptId >= 0;
        mAwContentsRef = new WeakReference(awContents);
        mScriptId = scriptId;
    }

    // Must be called on UI thread.
    public void remove() {
        ThreadUtils.checkUiThread();

        AwContents awContents = mAwContentsRef.get();
        if (awContents == null) return;
        awContents.removeDocumentStartJavaScript(mScriptId);
    }
}
