// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.base;

import android.content.Context;
import android.os.Bundle;

import com.google.firebase.messaging.FirebaseMessagingService;
import com.google.firebase.messaging.RemoteMessage;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;

/**
 * GcmListenerService base class which will call through to the given {@link Impl}. This class must
 * be present in the base module, while the Impl can be in the chrome module.
 */
@NullMarked
public class SplitCompatGcmListenerService extends FirebaseMessagingService {
    private final String mServiceClassName;
    private Impl mImpl;

    public SplitCompatGcmListenerService(String serviceClassName) {
        mServiceClassName = serviceClassName;
    }

    @Override
    protected void attachBaseContext(Context baseContext) {
        mImpl =
                (Impl)
                        SplitCompatUtils.loadClassAndAdjustContextChrome(
                                baseContext, mServiceClassName);
        mImpl.setService(this);
        super.attachBaseContext(baseContext);
    }

    @Override
    public void onCreate() {
        super.onCreate();
        mImpl.onCreate();
    }

    @Override
    public void onMessageReceived(RemoteMessage message) {
        String from = message.getFrom();
        Bundle data = message.toIntent().getExtras();
        mImpl.onMessageReceived(from, data);
    }

    @Override
    public void onMessageSent(String msgId) {
        mImpl.onMessageSent(msgId);
    }

    @Override
    public void onSendError(String msgId, Exception error) {
        mImpl.onSendError(msgId, error);
    }

    @Override
    public void onDeletedMessages() {
        mImpl.onDeletedMessages();
    }

    @Override
    public void onNewToken(String token) {
        mImpl.onNewToken(token);
    }

    /**
     * Holds the implementation of service logic. Will be called by {@link
     * SplitCompatGcmListenerService}.
     */
    public abstract static class Impl {
        private @Nullable SplitCompatGcmListenerService mService;

        protected final void setService(SplitCompatGcmListenerService service) {
            mService = service;
        }

        protected final @Nullable SplitCompatGcmListenerService getService() {
            return mService;
        }

        public void onCreate() {}

        public void onMessageReceived(@Nullable String from, @Nullable Bundle data) {}

        public void onMessageSent(String msgId) {}

        public void onSendError(String msgId, Exception error) {}

        public void onDeletedMessages() {}

        public void onNewToken(String token) {}
    }
}
