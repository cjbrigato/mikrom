// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.android_webview;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.components.embedder_support.util.WebResourceResponseInfo;

/** Abstract base class that implementors of service worker related callbacks derive from. */
@NullMarked
public abstract class AwServiceWorkerClient {
    public abstract @Nullable WebResourceResponseInfo shouldInterceptRequest(
            AwWebResourceRequest request);

    // TODO: add support for onReceivedError and onReceivedHttpError callbacks.
}
