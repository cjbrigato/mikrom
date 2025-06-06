// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.net.impl;

import android.content.Context;

import org.chromium.net.CronetEngine;
import org.chromium.net.CronetProvider;
import org.chromium.net.ExperimentalCronetEngine;
import org.chromium.net.ICronetEngineBuilder;

import java.util.Arrays;

/**
 * Implementation of {@link CronetProvider} that creates {@link CronetEngine.Builder} for building
 * the Java-based implementation of {@link CronetEngine}.
 */
public class JavaCronetProvider extends CronetProvider {
    public static final String FORCE_HTTPENGINE_FLAG = "Cronet_ForceHttpEngineInFallback";

    /**
     * Constructor.
     *
     * @param context Android context to use.
     */
    public JavaCronetProvider(Context context) {
        super(context);
    }

    private boolean shouldUseHttpEngine() {
        if (!HttpEngineNativeProvider.isHttpEngineAvailable()) return false;
        var shouldForceHttpEngine =
                HttpFlagsForImpl.getHttpFlags(mContext).flags().get(FORCE_HTTPENGINE_FLAG);
        return shouldForceHttpEngine != null && shouldForceHttpEngine.getBoolValue();
    }

    @Override
    public CronetEngine.Builder createBuilder() {
        if (shouldUseHttpEngine()) {
            return new HttpEngineNativeProvider(mContext).createBuilder();
        } else {
            ICronetEngineBuilder impl = new JavaCronetEngineBuilderImpl(mContext);
            return new ExperimentalCronetEngine.Builder(impl);
        }
    }

    @Override
    public String getName() {
        return CronetProvider.PROVIDER_NAME_FALLBACK;
    }

    @Override
    public String getVersion() {
        return ImplVersion.getCronetVersion();
    }

    @Override
    public boolean isEnabled() {
        return true;
    }

    @Override
    public int hashCode() {
        return Arrays.hashCode(new Object[] {JavaCronetProvider.class, mContext});
    }

    @Override
    public boolean equals(Object other) {
        return other == this
                || (other instanceof JavaCronetProvider
                        && this.mContext.equals(((JavaCronetProvider) other).mContext));
    }
}
