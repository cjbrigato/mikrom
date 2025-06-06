// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import android.os.Bundle;

import androidx.annotation.VisibleForTesting;

import com.google.errorprone.annotations.DoNotMock;

import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;
import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;

/** Java counterpart to the native base::Token. A {@link Token} is a random 128-bit integer. */
@NullMarked
@JNINamespace("base::android")
@DoNotMock("This is a simple value object.")
public final class Token extends TokenBase {
    private static final String KEY_LOW = "low";
    private static final String KEY_HIGH = "high";

    /** Returns a new random token using the native implementation. */
    public static Token createRandom() {
        return TokenJni.get().createRandom();
    }

    /**
     * Create a token from a high and low values. This is intended to be used when passing a token
     * up from native or when creating a token from a serialized pair of longs not for new tokens.
     *
     * @param high The high portion of the token.
     * @param low The low portion of the token.
     */
    @CalledByNative
    public Token(long high, long low) {
        super(high, low);
    }

    /** Returns whether the Token is 0. */
    public boolean isZero() {
        return mHigh == 0 && mLow == 0;
    }

    /** Returns the high portion of the token. */
    public long getHigh() {
        return mHigh;
    }

    /** Returns the low portion of the token. */
    public long getLow() {
        return mLow;
    }

    @Override
    public String toString() {
        return String.format("%016X%016X", mHigh, mLow);
    }

    /**
     * Converts this Token's data into a bundle {@link Bundle}.
     *
     * @return the bundle {@link Bundle} stores the Token's high and low values.
     */
    public Bundle toBundle() {
        Bundle bundle = new Bundle();
        bundle.putLong(KEY_HIGH, getHigh());
        bundle.putLong(KEY_LOW, getLow());
        return bundle;
    }

    /**
     * @param bundle Bundle to be parsed.
     * @return the deserialized Token object or null if the bundle is invalid.
     */
    public static @Nullable Token maybeCreateFromBundle(@Nullable Bundle bundle) {
        if (bundle == null) return null;
        if (!bundle.containsKey(KEY_HIGH) || !bundle.containsKey(KEY_LOW)) return null;
        return new Token(bundle.getLong(KEY_HIGH), bundle.getLong(KEY_LOW));
    }

    @NativeMethods
    @VisibleForTesting
    public interface Natives {
        @JniType("base::Token")
        Token createRandom();
    }
}
