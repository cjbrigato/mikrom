// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.test.util;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

/**
 * An annotation for listing restrictions for a test method. For example, if a test method is only
 * applicable on a low-end phone: <code>
 *     \@Restriction({DeviceFormFactor.PHONE, RESTRICTION_TYPE_LOW_END_DEVICE})
 * </code> Test classes are free to define restrictions and enforce them using reflection at
 * runtime. If the test is temporarily failing in some configurations, use {@link DisableIf}
 * instead.
 */
@Target({ElementType.METHOD, ElementType.TYPE})
@Retention(RetentionPolicy.RUNTIME)
public @interface Restriction {
    /** Specifies the test is only valid on low end devices that have less memory. */
    String RESTRICTION_TYPE_LOW_END_DEVICE = "Low_End_Device";

    /** Specifies the test is only valid on non-low end devices. */
    String RESTRICTION_TYPE_NON_LOW_END_DEVICE = "Non_Low_End_Device";

    /** Specifies the test is only valid on a device that can reach the internet. */
    String RESTRICTION_TYPE_INTERNET = "Internet";

    /** Specifies the test is only valid on a device that has a camera. */
    String RESTRICTION_TYPE_HAS_CAMERA = "Has_Camera";

    /** Specifies the test is only valid for non-Chrome branded builds. */
    String RESTRICTION_TYPE_NON_CHROME_BRANDED = "Non_Chrome_Branded";

    /**
     * @return A list of restrictions.
     */
    String[] value();
}
