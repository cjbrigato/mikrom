// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.supplier;

import org.chromium.base.Callback;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.NullUnmarked;
import org.chromium.build.annotations.Nullable;

import java.util.function.Function;

/**
 * Useful when two observable suppliers are chained together. The client class may only want to know
 * the value of the second, or "target", supplier. But to track this the client needs to observe the
 * first, or "parent", supplier, and then [un]observe the current target. Instead this class is a
 * single observable supplier that holds the current target value, greatly simplifying the client's
 * job.
 *
 * <p>Attempts to only maintain observers on the relevant observers when there's an observer on this
 * class. Clients should still remove themselves as observers from this class when done.
 *
 * @param <P> The parent object that's holding a reference to the target.
 * @param <T> The target type that the client wants to observe.
 */
@NullMarked
public class TransitiveObservableSupplier<P extends @Nullable Object, T extends @Nullable Object>
        implements ObservableSupplier<T> {
    // Used to hold observers and current state. However the current value is only valid when there
    // are observers, otherwise is may be stale.
    private final ObservableSupplierImpl<T> mDelegateSupplier = new ObservableSupplierImpl<>();
    private final Callback<P> mOnParentSupplierChangeCallback = this::onParentSupplierChange;
    private final Callback<T> mOnTargetSupplierChangeCallback = this::onTargetSupplierChange;
    private final ObservableSupplier<P> mParentSupplier;
    private final Function<P, ObservableSupplier<T>> mUnwrapFunction;

    // When this is set, then mOnTargetSupplierChangeCallback is an observer of the object
    // referenced by mCurrentTargetSupplier. When this value is changed, the observer must be
    // removed.
    private @Nullable ObservableSupplier<T> mCurrentTargetSupplier;

    public TransitiveObservableSupplier(
            ObservableSupplier<P> parentSupplier,
            Function<P, ObservableSupplier<T>> unwrapFunction) {
        mParentSupplier = parentSupplier;
        mUnwrapFunction = unwrapFunction;
    }

    @Override
    public @Nullable T addObserver(Callback<T> obs, @NotifyBehavior int behavior) {
        if (!mDelegateSupplier.hasObservers()) {
            onParentSupplierChange(
                    mParentSupplier.addSyncObserver(mOnParentSupplierChangeCallback));
        }
        return mDelegateSupplier.addObserver(obs, behavior);
    }

    @Override
    public void removeObserver(Callback<T> obs) {
        mDelegateSupplier.removeObserver(obs);
        if (!mDelegateSupplier.hasObservers()) {
            mParentSupplier.removeObserver(mOnParentSupplierChangeCallback);
            if (mCurrentTargetSupplier != null) {
                mCurrentTargetSupplier.removeObserver(mOnTargetSupplierChangeCallback);
                mCurrentTargetSupplier = null;
            }
        }
    }

    @Override
    @SuppressWarnings("NullAway") // https://github.com/uber/NullAway/issues/1209
    public @Nullable T get() {
        if (mDelegateSupplier.hasObservers()) {
            return mDelegateSupplier.get();
        }

        // If we have no observers, the value stored by mDelegateSupplier might not be current.
        P parentValue = mParentSupplier.get();
        if (parentValue != null) {
            ObservableSupplier<T> targetSupplier = mUnwrapFunction.apply(parentValue);
            if (targetSupplier != null) {
                return targetSupplier.get();
            }
        }
        return null;
    }

    /**
     * Conceptually this just removes our observer from the old target supplier, and our observer to
     * to the new target supplier. In practice this is full of null checks. We also have to make
     * sure we keep our delegate supplier's value up to date, which is also what drives client
     * observations.
     */
    @NullUnmarked // Needs to work where P is non-null or nullable.
    private void onParentSupplierChange(@Nullable P parentValue) {
        if (mCurrentTargetSupplier != null) {
            mCurrentTargetSupplier.removeObserver(mOnTargetSupplierChangeCallback);
        }

        // Keep track of the current target supplier, because if this ever changes, we'll need to
        // remove our observer from it.
        mCurrentTargetSupplier = parentValue == null ? null : mUnwrapFunction.apply(parentValue);

        @Nullable T targetValue = null;
        if (mCurrentTargetSupplier != null) {
            // While addObserver will call us if a value is already set, we do not want to depend on
            // that for two reasons. If there is no value set, we need to null out our supplier now.
            // And if there is a value set, we're going to get invoked asynchronously, which means
            // our delegate supplier could be in an intermediately incorrect state. By just setting
            // our delegate eagerly we avoid both problems.
            targetValue = mCurrentTargetSupplier.addSyncObserver(mOnTargetSupplierChangeCallback);
        }
        onTargetSupplierChange(targetValue);
    }

    @NullUnmarked // Needs to work where T is non-null or nullable.
    private void onTargetSupplierChange(T targetValue) {
        mDelegateSupplier.set(targetValue);
    }
}
