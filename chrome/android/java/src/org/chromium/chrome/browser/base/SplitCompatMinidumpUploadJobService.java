// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.base;

import android.content.Context;
import android.os.PersistableBundle;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.components.minidump_uploader.MinidumpUploadJob;
import org.chromium.components.minidump_uploader.MinidumpUploadJobService;

/**
 * MinidumpUploadJobService base class which will call through to the given {@link Impl}. This class
 * must be present in the base module, while the Impl can be in the chrome module.
 */
@NullMarked
public class SplitCompatMinidumpUploadJobService extends MinidumpUploadJobService {
    private final String mServiceClassName;
    private Impl mImpl;

    public SplitCompatMinidumpUploadJobService(String serviceClassName) {
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
    protected MinidumpUploadJob createMinidumpUploadJob(PersistableBundle permissions) {
        return mImpl.createMinidumpUploadJob(permissions);
    }

    /**
     * Holds the implementation of service logic. Will be called by {@link
     * SplitCompatMinidumpUploadJobService}.
     */
    public abstract static class Impl {
        private @Nullable SplitCompatMinidumpUploadJobService mService;

        protected final void setService(SplitCompatMinidumpUploadJobService service) {
            mService = service;
        }

        protected final @Nullable MinidumpUploadJobService getService() {
            return mService;
        }

        protected abstract MinidumpUploadJob createMinidumpUploadJob(PersistableBundle extras);
    }
}
