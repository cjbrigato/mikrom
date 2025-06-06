// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.test.transit;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;

/**
 * The return value of {@link ConditionWithResult#resolveWithSuppliers()}.
 *
 * <p>Includes whether the condition is fulfilled, an optional message and the timestamp of the
 * check, and the result.
 *
 * <p>Use {@link ConditionStatus#withResult(Object)} or {@link ConditionStatus#withoutResult()} to
 * instantiate, e.g.:
 *
 * <pre>
 *     return fulfilled().withResult(n);
 * </pre>
 *
 * @param <ResultT> The type of the result.
 */
@NullMarked
public class ConditionStatusWithResult<ResultT> {

    private final ConditionStatus mStatus;
    private final @Nullable ResultT mResult;

    ConditionStatusWithResult(ConditionStatus status, @Nullable ResultT result) {
        mStatus = status;
        mResult = result;
    }

    public ConditionStatus getStatus() {
        return mStatus;
    }

    public @Nullable ResultT getResult() {
        return mResult;
    }
}
