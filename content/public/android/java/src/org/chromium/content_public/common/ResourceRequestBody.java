// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content_public.common;

import androidx.annotation.VisibleForTesting;

import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;
import org.jni_zero.NativeMethods;

import org.chromium.build.annotations.NullMarked;

/** Wrapper around the native content::ResourceRequestBody. */
@JNINamespace("content")
@NullMarked
public final class ResourceRequestBody {
    /**
     * Result of EncodeResourceRequestBody call from page_state_serialization.h.
     *
     * Note that this is *not* the content of HTTP body (i.e. format of the
     * value of mSerializedFormOfNativeResourceRequestBody is opaque and
     * different from the value passed as an argument of
     * ResourceRequestBody.createFromBytes method below).
     */
    private final byte[] mEncodedNativeForm;

    // ResourceRequestBody Java objects can only be constructed by
    // - ResourceRequestBody::createFromBytes(byte[])
    //   (public - callable from other Java code)
    // - ResourceRequestBody::createFromEncodedNativeForm(byte[])
    //   (private - called only from native code)
    private ResourceRequestBody(byte[] encodedNativeForm) {
        mEncodedNativeForm = encodedNativeForm;
    }

    /**
     * Used by native code to construct ResourceRequestBody.
     *
     * @param encodedNativeForm Result of calling EncodeResourceRequestBody.
     */
    @CalledByNative
    private static ResourceRequestBody createFromEncodedNativeForm(byte[] encodedNativeForm) {
        return new ResourceRequestBody(encodedNativeForm);
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    @CalledByNative
    public byte[] getEncodedNativeForm() {
        return mEncodedNativeForm;
    }

    /**
     * Creates an instance representing HTTP body where the payload
     * is a copy of the specified byte array.
     *
     * @param body the HTTP body
     */
    public static ResourceRequestBody createFromBytes(byte[] httpBody) {
        byte[] encodedNativeForm =
                ResourceRequestBodyJni.get().createResourceRequestBodyFromBytes(httpBody);
        return createFromEncodedNativeForm(encodedNativeForm);
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
    @NativeMethods
    public interface Natives {
        /**
         * Equivalent of the native content::ResourceRequestBody::CreateFromBytes.
         *
         * @param body the HTTP body
         *
         * @return result of a call to EncodeResourceRequestBody on
         * ResourceRequestBody created from |httpBody|.
         */
        byte[] createResourceRequestBodyFromBytes(byte[] httpBody);
    }
}
