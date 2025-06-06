// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.android_webview;

import android.net.Uri;

import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;
import org.jni_zero.JniType;

import org.chromium.android_webview.common.Lifetime;
import org.chromium.build.annotations.NullMarked;
import org.chromium.content_public.browser.MessagePayload;
import org.chromium.content_public.browser.MessagePort;

/**
 * Holds the {@link WebMessageListener} instance so that C++ could interact with the {@link
 * WebMessageListener}.
 */
@Lifetime.Temporary
@JNINamespace("android_webview")
@NullMarked
public class WebMessageListenerHolder {
    private final WebMessageListener mListener;

    public WebMessageListenerHolder(WebMessageListener listener) {
        mListener = listener;
    }

    @CalledByNative
    public void onPostMessage(
            MessagePayload payload,
            @JniType("std::string") String topLevelOrigin,
            @JniType("std::string") String sourceOrigin,
            boolean isMainFrame,
            MessagePort[] ports,
            JsReplyProxy replyProxy) {
        AwThreadUtils.postToCurrentLooper(
                () -> {
                    mListener.onPostMessage(
                            payload,
                            Uri.parse(topLevelOrigin),
                            Uri.parse(sourceOrigin),
                            isMainFrame,
                            replyProxy,
                            ports);
                });
    }

    public WebMessageListener getListener() {
        return mListener;
    }
}
