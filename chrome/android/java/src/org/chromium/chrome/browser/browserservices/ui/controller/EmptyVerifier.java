// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.browserservices.ui.controller;

import androidx.annotation.Nullable;

import org.chromium.base.Promise;

/** Empty {@link Verifier} implementation. */
public class EmptyVerifier implements Verifier {
    public EmptyVerifier() {}

    @Override
    public final Promise<Boolean> verify(String url) {
        return Promise.fulfilled(false);
    }

    @Override
    public boolean wasPreviouslyVerified(String url) {
        return false;
    }

    @Nullable
    @Override
    public String getVerifiedScope(String url) {
        return url;
    }

    @Override
    public boolean shouldIgnoreExternalIntentHandlers(String url) {
        return false;
    }
}
