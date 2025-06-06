// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.lifetime;

import static org.chromium.build.NullUtil.assumeNonNull;

import androidx.annotation.VisibleForTesting;

import org.chromium.base.task.PostTask;
import org.chromium.build.BuildConfig;
import org.chromium.build.annotations.CheckDiscard;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;

import java.lang.ref.PhantomReference;
import java.lang.ref.ReferenceQueue;
import java.util.Collections;
import java.util.HashSet;
import java.util.Set;

/**
 * Used to assert that clean-up logic has been run before an object is GC'ed.
 *
 * <p>Usage:
 *
 * <pre>
 * class MyClassWithCleanup {
 *     private final LifetimeAssert mLifetimeAssert = LifetimeAssert.create(this);
 *
 *     public void destroy() {
 *         // If mLifetimeAssert is GC'ed before this is called, it will throw an exception
 *         // with a stack trace showing the stack during LifetimeAssert.create().
 *         LifetimeAssert.destroy(mLifetimeAssert, true);
 *     }
 * }
 * </pre>
 */
@NullMarked
@CheckDiscard("Lifetime assertions aren't used when DCHECK is off.")
public class LifetimeAssert {
    interface TestHook {
        void onCleaned(WrappedReference ref, @Nullable String msg);
    }

    /** Thrown for failed assertions. */
    static class LifetimeAssertException extends RuntimeException {
        LifetimeAssertException(String msg, Throwable causedBy) {
            super(msg, causedBy);
        }
    }

    /** For capturing where objects were created. */
    private static class CreationException extends RuntimeException {
        CreationException() {
            super("vvv This is where object was created. vvv");
        }
    }

    // Used only for unit test.
    static @Nullable TestHook sTestHook;

    @VisibleForTesting final WrappedReference mWrapper;

    private final Object mTarget;

    @VisibleForTesting
    static class WrappedReference extends PhantomReference<Object> {
        boolean mSafeToGc;
        final Class<?> mTargetClass;
        final CreationException mCreationException;

        private WrappedReference(
                Object target, CreationException creationException, boolean safeToGc) {
            super(target, sReferenceQueue);
            mCreationException = PostTask.maybeAddTaskOrigin(creationException);
            mSafeToGc = safeToGc;
            mTargetClass = target.getClass();
            sActiveWrappers.add(this);
        }

        private static final ReferenceQueue<Object> sReferenceQueue = new ReferenceQueue<>();
        private static final Set<WrappedReference> sActiveWrappers =
                Collections.synchronizedSet(new HashSet<>());

        static {
            new Thread("GcStateAssertQueue") {
                {
                    setDaemon(true);
                    start();
                }

                @Override
                public void run() {
                    while (true) {
                        try {
                            // This sleeps until a wrapper is available.
                            WrappedReference wrapper = (WrappedReference) sReferenceQueue.remove();
                            if (!sActiveWrappers.remove(wrapper)) {
                                // The reference was not a part of the active set. The reference was
                                // cleared by resetForTesting().
                                continue;
                            }
                            if (!wrapper.mSafeToGc) {
                                String msg =
                                        String.format(
                                                "Object of type %s was GC'ed without cleanup. Refer"
                                                        + " to \"Caused by\" for where object was"
                                                        + " created.",
                                                wrapper.mTargetClass.getName());
                                if (sTestHook != null) {
                                    sTestHook.onCleaned(wrapper, msg);
                                } else {
                                    throw new LifetimeAssertException(
                                            msg, wrapper.mCreationException);
                                }
                            } else if (sTestHook != null) {
                                sTestHook.onCleaned(wrapper, null);
                            }
                        } catch (InterruptedException e) {
                            throw new RuntimeException(e);
                        }
                    }
                }
            };
        }
    }

    private LifetimeAssert(WrappedReference wrapper, Object target) {
        mWrapper = wrapper;
        mTarget = target;
    }

    public static @Nullable LifetimeAssert create(Object target) {
        if (!BuildConfig.ENABLE_ASSERTS) {
            return null;
        }
        return new LifetimeAssert(
                new WrappedReference(target, new CreationException(), /* safeToGc= */ false),
                target);
    }

    public static @Nullable LifetimeAssert create(Object target, boolean safeToGc) {
        if (!BuildConfig.ENABLE_ASSERTS) {
            return null;
        }
        return new LifetimeAssert(
                new WrappedReference(target, new CreationException(), safeToGc), target);
    }

    /** Most clients should probably use destroy() to ensure exactly 1 call. */
    public static void setSafeToGc(@Nullable LifetimeAssert asserter, boolean value) {
        if (BuildConfig.ENABLE_ASSERTS) {
            assert asserter != null;
            // This guarantees that the target object is reachable until after mSafeToGc value
            // is updated here. See comment on Reference.reachabilityFence and review comments
            // on https://chromium-review.googlesource.com/c/chromium/src/+/1887151 for a
            // problematic example. This synchronized is used instead of calling
            // reachabilityFence because robolectric has problems mocking out that method,
            // and this should work for all Android versions.
            synchronized (asserter.mTarget) {
                // asserter is never null when ENABLE_ASSERTS.
                asserter.mWrapper.mSafeToGc = value;
            }
        }
    }

    /** Asserts that mSafeToGc == false. */
    public static void assertNotDestroyed(@Nullable LifetimeAssert asserter) {
        if (BuildConfig.ENABLE_ASSERTS) {
            assert !assumeNonNull(asserter).mWrapper.mSafeToGc;
        }
    }

    /** Shorthand for assertNotDestroyed(_) + setSafeToGc(_, true). */
    public static void destroy(@Nullable LifetimeAssert asserter) {
        assertNotDestroyed(asserter);
        setSafeToGc(asserter, true);
    }

    /**
     * Asserts that the remaining objects used with LifetimeAssert do not need to be destroyed and
     * can be garbage collected. Always clears the set of tracked object, so consecutive invocations
     * won't throw with the same cause.
     */
    public static void assertAllInstancesDestroyedForTesting() {
        if (!BuildConfig.ENABLE_ASSERTS) {
            return;
        }
        // Synchronized set requires manual synchronization when iterating over it.
        synchronized (WrappedReference.sActiveWrappers) {
            try {
                for (WrappedReference ref : WrappedReference.sActiveWrappers) {
                    if (!ref.mSafeToGc) {
                        String msg =
                                String.format(
                                        "Object of type %s was not destroyed after test completed."
                                                + " Refer to \"Caused by\" for where object was"
                                                + " created.",
                                        ref.mTargetClass.getName());
                        throw new LifetimeAssertException(msg, ref.mCreationException);
                    }
                }
            } finally {
                WrappedReference.sActiveWrappers.clear();
            }
        }
    }

    /** Clears the set of tracked references. */
    public static void resetForTesting() {
        if (!BuildConfig.ENABLE_ASSERTS) {
            return;
        }
        WrappedReference.sActiveWrappers.clear();
    }
}
