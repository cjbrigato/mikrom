// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.test.transit;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;

/**
 * {@link RuntimeException}s thrown by Public Transit transitions; the message of the wrapping
 * Exception give context to when the underlying Exception happened.
 */
@NullMarked
public class TravelException extends RuntimeException {

    // Private, call one of the public factory methods instead.
    private TravelException(@Nullable String message, @Nullable Throwable cause) {
        super(message, cause);
    }

    /**
     * Factory method for TravelException from a raw String message.
     *
     * <p>Notifies PublicTransitConfig to maybe pause execution or execute debugging callbacks.
     *
     * @param message the error message
     * @return a new TravelException instance
     */
    public static TravelException newTravelException(@Nullable String message) {
        return newTravelException(message, /* cause= */ null);
    }

    /**
     * Factory method for TravelException from a raw String message with an underlying cause.
     *
     * <p>Notifies PublicTransitConfig to maybe pause execution or execute debugging callbacks.
     *
     * @param message the error message
     * @param cause the root cause
     * @return a new TravelException instance
     */
    public static TravelException newTravelException(
            @Nullable String message, @Nullable Throwable cause) {
        TravelException travelException = new TravelException(message, cause);
        PublicTransitConfig.onTravelException(travelException);
        return travelException;
    }
}
